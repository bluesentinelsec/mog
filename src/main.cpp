/**
 * @file main.cpp
 * @brief mog CLI — curl-like HTTP client over the mog library.
 */

#include "mog/mog.hpp"

#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct CliOptions
{
    std::string url;
    std::string method = "GET";
    std::vector<std::string> headers;
    std::string data;
    std::string json;
    std::vector<std::string> form_fields; // name=value
    std::string output;
    std::string dump_header;
    std::string backend;
    std::string user;
    std::string bearer;
    std::string user_agent;
    std::string referer;
    std::string cookie;
    std::string proxy;
    std::string ca_bundle;
    double timeout_sec = 30.0;
    double connect_timeout_sec = -1.0;
    int max_redirs = 5;
    bool insecure = false;
    bool verbose = false;
    bool include_headers = false;
    bool fail_on_error = false;
    bool head = false;
    bool no_location = false;
    bool get_with_data = false; // -G: append -d to query
    bool silent = false;
    bool show_error = false;
    std::string write_out;
};

mog::Result<std::string> LoadDataArg(const std::string &data)
{
    if (!data.empty() && data.front() == '@')
    {
        return mog::ReadFile(std::string_view{data}.substr(1));
    }
    return mog::Result<std::string>::Ok(data);
}

int Run(const CliOptions &cli)
{
    mog::Options options;
    options.timeout = std::chrono::milliseconds{static_cast<int>(cli.timeout_sec * 1000.0)};
    if (cli.connect_timeout_sec >= 0.0)
    {
        options.connect_timeout =
            std::chrono::milliseconds{static_cast<int>(cli.connect_timeout_sec * 1000.0)};
    }
    options.verify_tls = !cli.insecure;
    options.allow_redirects = !cli.no_location;
    options.max_redirects = cli.max_redirs;

    if (!cli.backend.empty())
    {
        auto parsed = mog::ParseBackend(cli.backend);
        if (!parsed)
        {
            std::cerr << "mog: unknown backend '" << cli.backend
                      << "' (expected auto|embedded|curl|winhttp|native)\n";
            return 2;
        }
        options.backend = *parsed;
    }

    if (!cli.user_agent.empty())
    {
        options.user_agent = cli.user_agent;
    }
    if (!cli.referer.empty())
    {
        options.headers["Referer"] = cli.referer;
    }
    if (!cli.proxy.empty())
    {
        options.proxy = cli.proxy;
    }
    if (!cli.ca_bundle.empty())
    {
        options.ca_bundle = cli.ca_bundle;
    }

    if (!cli.user.empty())
    {
        const auto colon = cli.user.find(':');
        if (colon == std::string::npos)
        {
            mog::WithBasicAuth(options, cli.user, "");
        }
        else
        {
            mog::WithBasicAuth(options, cli.user.substr(0, colon), cli.user.substr(colon + 1));
        }
    }
    if (!cli.bearer.empty())
    {
        mog::WithBearerToken(options, cli.bearer);
    }

    if (!cli.cookie.empty())
    {
        // name=value; name2=value2
        std::string remaining = cli.cookie;
        while (!remaining.empty())
        {
            auto semi = remaining.find(';');
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
            options.cookies[part.substr(0, eq)] = part.substr(eq + 1);
        }
    }

    for (const auto &h : cli.headers)
    {
        const auto colon = h.find(':');
        if (colon == std::string::npos)
        {
            std::cerr << "mog: invalid header (expected Name: value): " << h << '\n';
            return 2;
        }
        std::string name = h.substr(0, colon);
        std::string value = h.substr(colon + 1);
        while (!value.empty() && value.front() == ' ')
        {
            value.erase(value.begin());
        }
        options.headers[std::move(name)] = std::move(value);
    }

    if (!cli.json.empty())
    {
        auto loaded = LoadDataArg(cli.json);
        if (!loaded)
        {
            std::cerr << "mog: " << loaded.error().to_string() << '\n';
            return 1;
        }
        options.json = std::move(*loaded);
    }
    else if (!cli.form_fields.empty())
    {
        for (const auto &f : cli.form_fields)
        {
            const auto eq = f.find('=');
            if (eq == std::string::npos)
            {
                std::cerr << "mog: invalid form field (expected name=value): " << f << '\n';
                return 2;
            }
            options.form[f.substr(0, eq)] = f.substr(eq + 1);
        }
    }
    else if (!cli.data.empty())
    {
        auto loaded = LoadDataArg(cli.data);
        if (!loaded)
        {
            std::cerr << "mog: " << loaded.error().to_string() << '\n';
            return 1;
        }
        if (cli.get_with_data)
        {
            // Treat body as query string name=value&...
            std::string qs = *loaded;
            std::size_t pos = 0;
            while (pos < qs.size())
            {
                const auto amp = qs.find('&', pos);
                const std::string part =
                    amp == std::string::npos ? qs.substr(pos) : qs.substr(pos, amp - pos);
                const auto eq = part.find('=');
                if (eq == std::string::npos)
                {
                    options.params[part] = "";
                }
                else
                {
                    options.params[part.substr(0, eq)] = part.substr(eq + 1);
                }
                if (amp == std::string::npos)
                {
                    break;
                }
                pos = amp + 1;
            }
        }
        else
        {
            options.body = std::move(*loaded);
        }
    }

    std::string method_text = cli.method;
    if (cli.head)
    {
        method_text = "HEAD";
    }
    // If JSON/form/data provided and method still GET, curl often keeps GET unless -X;
    // for --json default to POST when method is GET and body present.
    if ((options.json.has_value() || !options.form.empty() ||
         (!options.body.empty() && !cli.get_with_data)) &&
        method_text == "GET" && !cli.head)
    {
        method_text = "POST";
    }

    auto method = mog::ParseMethod(method_text);
    if (!method)
    {
        std::cerr << "mog: unknown method '" << method_text << "'\n";
        return 2;
    }

    if (cli.verbose && !cli.silent)
    {
        const auto backend = mog::ResolveBackend(options.backend);
        std::cerr << "* backend: " << mog::ToString(backend) << '\n';
        std::cerr << "> " << mog::ToString(*method) << ' ' << cli.url << '\n';
    }

    auto result = mog::request(*method, cli.url, options);
    if (!result)
    {
        if (!cli.silent || cli.show_error)
        {
            std::cerr << "mog: " << result.error().to_string() << '\n';
        }
        return 1;
    }

    const mog::Response &response = *result;
    if (cli.verbose && !cli.silent)
    {
        std::cerr << "< HTTP " << response.status_code << ' ' << response.reason << '\n';
        for (const auto &h : response.headers)
        {
            std::cerr << "< " << h.name << ": " << h.value << '\n';
        }
        std::cerr << "* backend used: " << response.backend << '\n';
        std::cerr << "* elapsed: " << response.elapsed.count() << " ms\n";
    }

    if (!cli.dump_header.empty())
    {
        std::ofstream hdr(cli.dump_header, std::ios::binary);
        if (!hdr)
        {
            std::cerr << "mog: failed to open header dump file: " << cli.dump_header << '\n';
            return 1;
        }
        hdr << "HTTP/1.1 " << response.status_code << ' ' << response.reason << "\r\n";
        for (const auto &h : response.headers)
        {
            hdr << h.name << ": " << h.value << "\r\n";
        }
        hdr << "\r\n";
    }

    std::ostream *out = &std::cout;
    std::ofstream file;
    if (!cli.output.empty())
    {
        file.open(cli.output, std::ios::binary);
        if (!file)
        {
            std::cerr << "mog: failed to open output file: " << cli.output << '\n';
            return 1;
        }
        out = &file;
    }

    if (cli.include_headers)
    {
        *out << "HTTP/1.1 " << response.status_code << ' ' << response.reason << "\r\n";
        for (const auto &h : response.headers)
        {
            *out << h.name << ": " << h.value << "\r\n";
        }
        *out << "\r\n";
    }

    if (*method != mog::Method::Head && cli.output != "/dev/null")
    {
        out->write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    }

    if (!cli.write_out.empty())
    {
        // Minimal subset: http_code, url_effective, time_total, size_download
        std::string fmt = cli.write_out;
        auto replace = [&](std::string_view token, const std::string &value) {
            for (;;)
            {
                const auto pos = fmt.find(token);
                if (pos == std::string::npos)
                {
                    break;
                }
                fmt.replace(pos, token.size(), value);
            }
        };
        replace("%{http_code}", std::to_string(response.status_code));
        replace("%{url_effective}", response.url);
        replace("%{time_total}",
                std::to_string(static_cast<double>(response.elapsed.count()) / 1000.0));
        replace("%{size_download}", std::to_string(response.body.size()));
        replace("%{num_redirects}", std::to_string(response.history_len));
        std::cerr << fmt;
        if (fmt.empty() || fmt.back() != '\n')
        {
            // curl often needs explicit \n in format; don't force.
        }
    }

    if (cli.fail_on_error && response.status_code >= 400)
    {
        return 22;
    }
    return 0;
}

void AddCommon(CLI::App *app, CliOptions &cli)
{
    app->add_option("-H,--header", cli.headers, "HTTP header (Name: value)")->take_all();
    app->add_option("-d,--data", cli.data, "Request body (prefix @ to read a file)");
    app->add_option("--json", cli.json, "JSON body; sets Content-Type (prefix @ for file)");
    app->add_option("-F,--form", cli.form_fields, "Form field name=value (urlencoded)")->take_all();
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
    app->add_flag("-v,--verbose", cli.verbose, "Verbose progress on stderr");
    app->add_flag("-i,--include", cli.include_headers, "Include response headers in output");
    app->add_flag("-f,--fail", cli.fail_on_error, "Exit non-zero on HTTP 4xx/5xx");
    // Redirects are followed by default (curl -L style).
    app->add_flag("--no-location", cli.no_location, "Do not follow redirects");
    app->add_flag("-G,--get", cli.get_with_data, "Send -d data as query string on GET");
    app->add_flag("-s,--silent", cli.silent, "Silent mode (no progress)");
    app->add_flag("-S,--show-error", cli.show_error, "Show errors even with --silent");
}

} // namespace

int main(int argc, char **argv)
{
    CLI::App app{"mog — lightweight HTTP/S client (embedded backend by default)"};
    app.set_version_flag("-V,--version", std::string{mog::Version()});
    app.require_subcommand(0, 1);

    CliOptions cli;

    auto add_sub = [&](CLI::App *sub, const char *default_method) {
        sub->add_option("url", cli.url, "Request URL")->required();
        sub->callback([&, method = std::string{default_method}]() { cli.method = method; });
        AddCommon(sub, cli);
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
    AddCommon(&app, cli);

    CLI11_PARSE(app, argc, argv);

    if (cli.url.empty())
    {
        std::cerr << "mog: URL required (try: mog get https://example.com)\n";
        return 2;
    }

    return Run(cli);
}
