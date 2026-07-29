/**
 * @file cli.cpp
 * @brief CLI argument mapping and (optional) CLI11 parsing.
 */

#include "mog/cli.hpp"

#include "mog/backend.hpp"
#include "mog/util.hpp"
#include "mog/version.hpp"

#include <chrono>
#include <map>
#include <sstream>

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11
#include <CLI/CLI.hpp>
#endif

namespace mog::cli
{
namespace
{

void ParseCookieHeader(std::string_view cookie, std::map<std::string, std::string> &out)
{
    std::string remaining{cookie};
    while (!remaining.empty())
    {
        const auto semi = remaining.find(';');
        std::string part = semi == std::string::npos ? remaining : remaining.substr(0, semi);
        if (semi == std::string::npos)
        {
            remaining.clear();
        }
        else
        {
            remaining = remaining.substr(semi + 1);
        }
        while (!part.empty() && part.front() == ' ')
        {
            part.erase(part.begin());
        }
        const auto eq = part.find('=');
        if (eq == std::string::npos || eq == 0)
        {
            continue;
        }
        out[part.substr(0, eq)] = part.substr(eq + 1);
    }
}

void ParseQueryString(std::string_view qs, std::map<std::string, std::string> &params)
{
    std::size_t pos = 0;
    const std::string s{qs};
    while (pos < s.size())
    {
        const auto amp = s.find('&', pos);
        const std::string part =
            amp == std::string::npos ? s.substr(pos) : s.substr(pos, amp - pos);
        const auto eq = part.find('=');
        if (eq == std::string::npos)
        {
            params[part] = "";
        }
        else
        {
            params[part.substr(0, eq)] = part.substr(eq + 1);
        }
        if (amp == std::string::npos)
        {
            break;
        }
        pos = amp + 1;
    }
}

void ReplaceAll(std::string &haystack, std::string_view token, const std::string &value)
{
    for (;;)
    {
        const auto pos = haystack.find(token);
        if (pos == std::string::npos)
        {
            break;
        }
        haystack.replace(pos, token.size(), value);
    }
}

} // namespace

LogLevel ResolveLogLevel(const Args &args, bool *ok)
{
    if (ok != nullptr)
    {
        *ok = true;
    }
    if (!args.log_level.empty())
    {
        LogLevel level = LogLevel::Info;
        if (!ParseLogLevel(args.log_level, level))
        {
            if (ok != nullptr)
            {
                *ok = false;
            }
            return LogLevel::Info;
        }
        return level;
    }
    if (args.silent)
    {
        return LogLevel::Off;
    }
    if (args.verbose)
    {
        return LogLevel::Debug;
    }
    return LogLevel::Info;
}

Result<std::string> LoadDataArg(std::string_view data)
{
    if (!data.empty() && data.front() == '@')
    {
        return ReadFile(data.substr(1));
    }
    return Result<std::string>::Ok(std::string{data});
}

Result<Prepared> PrepareRequest(const Args &args)
{
    Prepared prepared;
    prepared.url = args.url;
    prepared.output = args.output;
    prepared.dump_header = args.dump_header;
    prepared.write_out = args.write_out;
    prepared.include_headers = args.include_headers;
    prepared.fail_on_error = args.fail_on_error;
    prepared.verbose = args.verbose;
    prepared.silent = args.silent;
    prepared.show_error = args.show_error;

    Options &options = prepared.options;
    options.timeout = std::chrono::milliseconds{static_cast<int>(args.timeout_sec * 1000.0)};
    if (args.connect_timeout_sec >= 0.0)
    {
        options.connect_timeout =
            std::chrono::milliseconds{static_cast<int>(args.connect_timeout_sec * 1000.0)};
    }
    options.verify_tls = !args.insecure;
    options.allow_redirects = !args.no_location;
    options.max_redirects = args.max_redirs;

    if (!args.backend.empty())
    {
        auto parsed = ParseBackend(args.backend);
        if (!parsed)
        {
            return Result<Prepared>::Err(Error{
                ErrorCode::InvalidArgument, "unknown backend '" + args.backend +
                                                "' (expected auto|embedded|curl|winhttp|native)"});
        }
        options.backend = *parsed;
    }

    if (!args.user_agent.empty())
    {
        options.user_agent = args.user_agent;
    }
    if (!args.referer.empty())
    {
        options.headers["Referer"] = args.referer;
    }
    if (!args.proxy.empty())
    {
        options.proxy = args.proxy;
    }
    if (!args.ca_bundle.empty())
    {
        options.ca_bundle = args.ca_bundle;
    }

    if (!args.user.empty())
    {
        const auto colon = args.user.find(':');
        if (colon == std::string::npos)
        {
            WithBasicAuth(options, args.user, "");
        }
        else
        {
            WithBasicAuth(options, args.user.substr(0, colon), args.user.substr(colon + 1));
        }
    }
    if (!args.bearer.empty())
    {
        WithBearerToken(options, args.bearer);
    }

    if (!args.cookie.empty())
    {
        ParseCookieHeader(args.cookie, options.cookies);
    }

    for (const auto &h : args.headers)
    {
        const auto colon = h.find(':');
        if (colon == std::string::npos)
        {
            return Result<Prepared>::Err(
                Error{ErrorCode::InvalidArgument, "invalid header (expected Name: value): " + h});
        }
        std::string name = h.substr(0, colon);
        std::string value = h.substr(colon + 1);
        while (!value.empty() && value.front() == ' ')
        {
            value.erase(value.begin());
        }
        options.headers[std::move(name)] = std::move(value);
    }

    if (!args.json.empty())
    {
        auto loaded = LoadDataArg(args.json);
        if (!loaded)
        {
            return Result<Prepared>::Err(loaded.error());
        }
        options.json = std::move(*loaded);
    }
    else if (!args.form_fields.empty())
    {
        for (const auto &f : args.form_fields)
        {
            const auto eq = f.find('=');
            if (eq == std::string::npos)
            {
                return Result<Prepared>::Err(Error{
                    ErrorCode::InvalidArgument, "invalid form field (expected name=value): " + f});
            }
            options.form[f.substr(0, eq)] = f.substr(eq + 1);
        }
    }
    else if (!args.data.empty())
    {
        auto loaded = LoadDataArg(args.data);
        if (!loaded)
        {
            return Result<Prepared>::Err(loaded.error());
        }
        if (args.get_with_data)
        {
            ParseQueryString(*loaded, options.params);
        }
        else
        {
            options.body = std::move(*loaded);
        }
    }

    std::string method_text = args.method;
    if (args.head)
    {
        method_text = "HEAD";
    }
    if ((options.json.has_value() || !options.form.empty() ||
         (!options.body.empty() && !args.get_with_data)) &&
        method_text == "GET" && !args.head)
    {
        method_text = "POST";
    }

    auto method = ParseMethod(method_text);
    if (!method)
    {
        return Result<Prepared>::Err(
            Error{ErrorCode::InvalidArgument, "unknown method '" + method_text + "'"});
    }
    prepared.method = *method;
    return Result<Prepared>::Ok(std::move(prepared));
}

std::string FormatWriteOut(std::string_view format, const Response &response)
{
    std::string fmt{format};
    ReplaceAll(fmt, "%{http_code}", std::to_string(response.status_code));
    ReplaceAll(fmt, "%{url_effective}", response.url);
    ReplaceAll(fmt, "%{time_total}",
               std::to_string(static_cast<double>(response.elapsed.count()) / 1000.0));
    ReplaceAll(fmt, "%{size_download}", std::to_string(response.body.size()));
    ReplaceAll(fmt, "%{num_redirects}", std::to_string(response.history_len));
    return fmt;
}

int ExitCodeForResponse(const Prepared &prepared, const Response &response)
{
    if (prepared.fail_on_error && response.status_code >= 400)
    {
        return 22;
    }
    return 0;
}

int ExitCodeForError(const Prepared & /*prepared*/)
{
    return 1;
}

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11

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
}

} // namespace

namespace
{

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
        // argc/argv form strips argv[0] as the program name (required).
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
    // Convert to classic argc/argv so CLI11 strips the program name (args[0]).
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

#endif // MOG_HAS_CLI11

} // namespace mog::cli
