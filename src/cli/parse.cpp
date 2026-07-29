/**
 * @file parse.cpp
 * @brief CLI11 argv parsing (front-end only; no HTTP).
 */

#include "mog/cli.hpp"
#include "mog/version.hpp"

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11

#include <CLI/CLI.hpp>

namespace mog::cli
{
namespace
{

void AddCommonOptions(CLI::App *app, Args &cli)
{
    // type_size(1): one token per -H/-F occurrence (repeatable). Without it,
    // vector options use TakeAll and swallow the positional URL.
    app->add_option("-H,--header", cli.headers, "HTTP header (Name: value)")->type_size(1);
    app->add_option("-d,--data", cli.data, "Request body (prefix @ to read a file)");
    app->add_option("--json", cli.json, "JSON body; sets Content-Type (prefix @ for file)");
    app->add_option("-F,--form", cli.form_fields, "Form field name=value (urlencoded)")
        ->type_size(1);
    app->add_option("-o,--output", cli.output, "Write body to file");
    app->add_option("-D,--dump-header", cli.dump_header, "Write response headers to file");
    app->add_option("-u,--user", cli.user, "Basic auth user:password");
    app->add_option("--bearer", cli.bearer, "Bearer token");
    app->add_option("-A,--user-agent", cli.user_agent, "User-Agent header");
    app->add_option("-e,--referer", cli.referer, "Referer header");
    app->add_option("-b,--cookie", cli.cookie, "Cookie header (name=value; ...)");
    app->add_option("-x,--proxy", cli.proxy, "HTTP proxy URL (http://host:port)");
    app->add_option("--cacert", cli.ca_bundle, "PEM CA bundle path");
    app->add_option("--backend", cli.backend,
                    "Backend: auto|embedded|curl|winhttp|native (overrides MOG_BACKEND)");
    app->add_option("--timeout", cli.timeout_sec, "Timeout in seconds")->default_val(30.0);
    app->add_option("--connect-timeout", cli.connect_timeout_sec, "Connect timeout in seconds");
    app->add_option("--max-redirs", cli.max_redirs, "Maximum redirects")->default_val(5);
    app->add_option("-w,--write-out", cli.write_out,
                    "Format string: %{http_code} %{url_effective} %{time_total} "
                    "%{size_download} %{num_redirects}");
    app->add_flag("-k,--insecure", cli.insecure, "Disable TLS certificate verification");
    app->add_flag("-v,--verbose", cli.verbose, "Debug logging (spdlog level=debug)");
    app->add_option("--log-level", cli.log_level,
                    "Log level: trace|debug|info|warn|error|critical|off "
                    "(overrides -v/-s; default info)");
    app->add_flag("-i,--include", cli.include_headers, "Include response headers in output");
    app->add_flag("-f,--fail", cli.fail_on_error, "Exit non-zero on HTTP 4xx/5xx");
    app->add_flag("--no-location", cli.no_location, "Do not follow redirects");
    app->add_flag("-G,--get", cli.get_with_data, "Send -d data as query string on GET");
    app->add_flag("-s,--silent", cli.silent, "Silent mode (log level off; body still printed)");
    app->add_flag("-S,--show-error", cli.show_error, "Show errors even with --silent");
    app->add_flag("--no-decompress", cli.no_decompress,
                  "Do not advertise or decode Content-Encoding gzip/deflate");
}

void BuildApp(CLI::App &app, Args &cli)
{
    app.set_version_flag("-V,--version", std::string{mog::Version()});
    app.require_subcommand(0, 1);

    auto add_sub = [&](CLI::App *sub, const char *default_method) {
        // Not required() here: CLI11 vector TakeAll can swallow the URL into -H/-F;
        // we recover and validate after parse.
        sub->add_option("url", cli.url, "Request URL");
        sub->callback([&, method = std::string{default_method}]() { cli.method = method; });
        AddCommonOptions(sub, cli);
    };

    add_sub(app.add_subcommand("get", "HTTP GET"), "GET");
    add_sub(app.add_subcommand("post", "HTTP POST"), "POST");
    add_sub(app.add_subcommand("put", "HTTP PUT"), "PUT");
    add_sub(app.add_subcommand("patch", "HTTP PATCH"), "PATCH");
    add_sub(app.add_subcommand("delete", "HTTP DELETE"), "DELETE");
    add_sub(app.add_subcommand("head", "HTTP HEAD"), "HEAD");

    app.add_option("url", cli.url, "Request URL");
    app.add_option("-X,--request", cli.method, "HTTP method")->default_val("GET");
    app.add_flag("-I,--head", cli.head, "Issue a HEAD request");
    AddCommonOptions(&app, cli);
}

} // namespace

Result<Args> ParseArgv(int argc, char **argv)
{
    CLI::App app{"mog — lightweight HTTP/S client (embedded backend by default)"};
    Args cli;
    BuildApp(app, cli);

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        return Result<Args>::Err(Error{ErrorCode::InvalidArgument, e.what()});
    }

    // CLI11 vector options use MultiOptionPolicy::TakeAll: a bare-form trailing URL
    // after repeated -H/-F can be swallowed into those vectors. Recover it when the
    // last captured value looks like an absolute URL.
    auto recover_url = [](std::string &url, std::vector<std::string> &bucket) {
        if (!url.empty() || bucket.empty())
        {
            return;
        }
        const std::string &last = bucket.back();
        if (last.find("://") != std::string::npos)
        {
            url = last;
            bucket.pop_back();
        }
    };
    recover_url(cli.url, cli.headers);
    recover_url(cli.url, cli.form_fields);

    if (cli.url.empty())
    {
        return Result<Args>::Err(
            Error{ErrorCode::InvalidArgument, "URL required (try: mog get https://example.com)"});
    }
    return Result<Args>::Ok(std::move(cli));
}

Result<Args> ParseArgv(const std::vector<std::string> &args)
{
    std::vector<std::string> storage = args;
    if (storage.empty())
    {
        storage.emplace_back("mog");
    }
    std::vector<char *> argv;
    argv.reserve(storage.size() + 1);
    for (auto &s : storage)
    {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    return ParseArgv(static_cast<int>(storage.size()), argv.data());
}

int RunArgv(int argc, char **argv, Streams streams)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i] != nullptr ? argv[i] : "";
        if (a == "-V" || a == "--version")
        {
            if (streams.out != nullptr)
            {
                *streams.out << mog::Version() << '\n';
            }
            return 0;
        }
    }

    auto parsed = ParseArgv(argc, argv);
    if (!parsed)
    {
        if (streams.err != nullptr)
        {
            const std::string msg{parsed.error().message()};
            if (msg.find("URL required") != std::string::npos)
            {
                *streams.err << "mog: " << msg << '\n';
            }
            else
            {
                *streams.err << msg << '\n';
            }
        }
        return 2;
    }
    return Run(*parsed, streams);
}

} // namespace mog::cli

#endif // MOG_HAS_CLI11
