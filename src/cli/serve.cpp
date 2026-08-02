/**
 * @file serve.cpp
 * @brief `mog serve`: a simple static file server (HTTP/S) over mog::Server.
 */
#include "mog/cli.hpp"
#include "mog/server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace mog::cli
{

// Sentinel error message meaning "--help was printed; exit cleanly". Shared with
// the RunArgv dispatch so it can return 0 instead of treating help as an error.
const char *const kServeHelpShown = "\x01mog-serve-help-shown";

Result<ServerOptions> BuildServeOptions(const ServeArgs &args)
{
    ServerOptions opt;
    opt.bind_address = args.bind_address;
    opt.port = args.port;
    opt.threads = args.threads;

    const bool have_files = !args.tls_cert.empty() || !args.tls_key.empty();
    if (args.self_signed && have_files)
    {
        return Result<ServerOptions>::Err(
            Error{ErrorCode::InvalidArgument,
                  "--self-signed cannot be combined with --tls-cert/--tls-key"});
    }
    if (args.self_signed)
    {
        auto tls = TlsServerConfig::SelfSigned("localhost");
        if (!tls)
        {
            return Result<ServerOptions>::Err(tls.error());
        }
        opt.tls = *tls;
    }
    else if (have_files)
    {
        if (args.tls_cert.empty() || args.tls_key.empty())
        {
            return Result<ServerOptions>::Err(Error{
                ErrorCode::InvalidArgument, "--tls-cert and --tls-key must be given together"});
        }
        auto tls = TlsServerConfig::FromFiles(args.tls_cert, args.tls_key);
        if (!tls)
        {
            return Result<ServerOptions>::Err(tls.error());
        }
        opt.tls = *tls;
    }
    return Result<ServerOptions>::Ok(std::move(opt));
}

} // namespace mog::cli

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11

#include <CLI/CLI.hpp>

namespace mog::cli
{
namespace
{

std::atomic<bool> g_stop{false};

extern "C" void OnStopSignal(int)
{
    g_stop.store(true);
}

} // namespace

Result<ServeArgs> ParseServeArgv(int argc, char **argv)
{
    // argv is the full process argv with argv[1] == "serve". Re-parse the tail
    // (everything after the "serve" token) with a dedicated CLI11 app.
    ServeArgs args;
    CLI::App app{"mog serve — serve a directory over HTTP/S"};
    app.add_option("directory", args.directory, "Directory to serve (default: current directory)");
    app.add_option("--port", args.port, "Port to listen on")->default_val(8000);
    app.add_option("--bind", args.bind_address, "Address to bind")->default_val("127.0.0.1");
    app.add_option("--threads", args.threads, "Worker threads (0 = auto)")->default_val(0);
    app.add_flag("--self-signed", args.self_signed,
                 "Serve HTTPS with an ephemeral self-signed certificate");
    app.add_option("--tls-cert", args.tls_cert, "TLS certificate chain (PEM) for HTTPS");
    app.add_option("--tls-key", args.tls_key, "TLS private key (PEM) for HTTPS");
    app.add_flag("--no-listing", args.no_listing, "Disable directory listings");

    std::vector<char *> shifted;
    shifted.reserve(static_cast<std::size_t>(argc));
    if (argc > 0)
    {
        shifted.push_back(argv[0]);
    }
    for (int i = 2; i < argc; ++i) // skip argv[1] == "serve"
    {
        shifted.push_back(argv[i]);
    }

    try
    {
        app.parse(static_cast<int>(shifted.size()), shifted.data());
    }
    catch (const CLI::CallForHelp &)
    {
        std::cout << app.help();
        // Sentinel: help was printed; the caller should exit 0 without an error.
        return Result<ServeArgs>::Err(Error{ErrorCode::InvalidArgument, kServeHelpShown});
    }
    catch (const CLI::ParseError &e)
    {
        return Result<ServeArgs>::Err(Error{ErrorCode::InvalidArgument, e.what()});
    }
    return Result<ServeArgs>::Ok(std::move(args));
}

int RunServe(const ServeArgs &args, Streams streams)
{
    auto opt = BuildServeOptions(args);
    if (!opt)
    {
        if (streams.err != nullptr)
        {
            *streams.err << "mog: " << opt.error().message() << '\n';
        }
        return 2;
    }

    Server server(*opt);
    StaticOptions static_opt;
    static_opt.directory_listing = !args.no_listing;
    server.serve_files("/", args.directory, static_opt);

    auto started = server.start();
    if (!started)
    {
        if (streams.err != nullptr)
        {
            *streams.err << "mog: cannot start server: " << started.error().message() << '\n';
        }
        return 1;
    }

    const char *scheme = opt->tls.enabled ? "https" : "http";
    if (streams.out != nullptr)
    {
        *streams.out << "Serving " << args.directory << " at " << scheme << "://"
                     << args.bind_address << ":" << server.port() << "/  (Ctrl-C to stop)\n";
        streams.out->flush();
    }

    g_stop.store(false);
    std::signal(SIGINT, OnStopSignal);
#if defined(SIGTERM)
    std::signal(SIGTERM, OnStopSignal);
#endif

    while (!g_stop.load() && server.running())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    server.stop();
    if (streams.out != nullptr)
    {
        *streams.out << "\nStopped.\n";
    }
    return 0;
}

} // namespace mog::cli

#endif // MOG_HAS_CLI11
