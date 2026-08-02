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

namespace mog::cli
{
namespace
{

std::atomic<bool> g_stop{false};

extern "C" void OnStopSignal(int)
{
    g_stop.store(true);
}

const char *const kServeHelp =
    "Usage: mog serve [DIRECTORY] [OPTIONS]\n"
    "\n"
    "Serve a directory over HTTP/S (defaults to the current directory).\n"
    "\n"
    "Options:\n"
    "  --port N            Port to listen on (default 8000)\n"
    "  --bind ADDR         Address to bind (default 127.0.0.1)\n"
    "  --threads N         Worker threads (0 = auto)\n"
    "  --self-signed       Serve HTTPS with an ephemeral self-signed certificate\n"
    "  --tls-cert FILE     TLS certificate chain (PEM) for HTTPS\n"
    "  --tls-key FILE      TLS private key (PEM) for HTTPS\n"
    "  --no-listing        Disable directory listings\n"
    "  -h, --help          Show this help\n";

// Split "--opt=value" into name and value; returns false when there is no '='.
bool SplitInline(const std::string &arg, std::string &name, std::string &value)
{
    const auto eq = arg.find('=');
    if (eq == std::string::npos)
    {
        return false;
    }
    name = arg.substr(0, eq);
    value = arg.substr(eq + 1);
    return true;
}

} // namespace

// Hand-rolled parser: a leading positional directory plus a few long options.
// This avoids CLI11's platform-specific argv handling for a small, well-defined
// argument set, and keeps `mog serve` behavior identical on every OS.
Result<ServeArgs> ParseServeArgv(int argc, char **argv)
{
    ServeArgs args;
    bool directory_set = false;

    // Fetch the value for an option, supporting both "--opt value" and inline
    // values already split out by the caller.
    auto take_value = [&](int &i, const std::string &name, const std::string &inline_value,
                          bool has_inline, std::string &out) -> Result<void> {
        if (has_inline)
        {
            out = inline_value;
            return Result<void>::Ok();
        }
        if (i + 1 >= argc || argv[i + 1] == nullptr)
        {
            return Result<void>::Err(
                Error{ErrorCode::InvalidArgument, "missing value for " + name});
        }
        out = argv[++i];
        return Result<void>::Ok();
    };

    for (int i = 2; i < argc; ++i) // skip argv[0] (program) and argv[1] ("serve")
    {
        const std::string raw = argv[i] != nullptr ? argv[i] : "";
        std::string name = raw;
        std::string inline_value;
        const bool has_inline = SplitInline(raw, name, inline_value);

        if (name == "-h" || name == "--help")
        {
            std::cout << kServeHelp;
            return Result<ServeArgs>::Err(Error{ErrorCode::InvalidArgument, kServeHelpShown});
        }
        else if (name == "--self-signed")
        {
            args.self_signed = true;
        }
        else if (name == "--no-listing")
        {
            args.no_listing = true;
        }
        else if (name == "--bind")
        {
            auto v = take_value(i, name, inline_value, has_inline, args.bind_address);
            if (!v)
            {
                return Result<ServeArgs>::Err(v.error());
            }
        }
        else if (name == "--tls-cert")
        {
            auto v = take_value(i, name, inline_value, has_inline, args.tls_cert);
            if (!v)
            {
                return Result<ServeArgs>::Err(v.error());
            }
        }
        else if (name == "--tls-key")
        {
            auto v = take_value(i, name, inline_value, has_inline, args.tls_key);
            if (!v)
            {
                return Result<ServeArgs>::Err(v.error());
            }
        }
        else if (name == "--port" || name == "--threads")
        {
            std::string value;
            auto v = take_value(i, name, inline_value, has_inline, value);
            if (!v)
            {
                return Result<ServeArgs>::Err(v.error());
            }
            try
            {
                const unsigned long parsed = std::stoul(value);
                if (name == "--port")
                {
                    if (parsed > 65535UL)
                    {
                        return Result<ServeArgs>::Err(
                            Error{ErrorCode::InvalidArgument, "--port out of range: " + value});
                    }
                    args.port = static_cast<std::uint16_t>(parsed);
                }
                else
                {
                    args.threads = static_cast<unsigned>(parsed);
                }
            }
            catch (...)
            {
                return Result<ServeArgs>::Err(
                    Error{ErrorCode::InvalidArgument, "invalid number for " + name + ": " + value});
            }
        }
        else if (!name.empty() && name[0] == '-')
        {
            return Result<ServeArgs>::Err(
                Error{ErrorCode::InvalidArgument, "unknown option: " + raw});
        }
        else if (!directory_set)
        {
            args.directory = raw;
            directory_set = true;
        }
        else
        {
            return Result<ServeArgs>::Err(
                Error{ErrorCode::InvalidArgument, "unexpected argument: " + raw});
        }
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
