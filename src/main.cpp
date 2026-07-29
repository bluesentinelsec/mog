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
    std::string output;
    std::string backend;
    double timeout_sec = 30.0;
    bool insecure = false;
    bool verbose = false;
    bool include_headers = false;
    bool fail_on_error = false;
    bool head = false;
};

int Run(const CliOptions &cli)
{
    mog::Options options;
    options.timeout = std::chrono::milliseconds{static_cast<int>(cli.timeout_sec * 1000.0)};
    options.verify_tls = !cli.insecure;
    options.body = cli.data;

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

    std::string method_text = cli.method;
    if (cli.head)
    {
        method_text = "HEAD";
    }
    auto method = mog::ParseMethod(method_text);
    if (!method)
    {
        std::cerr << "mog: unknown method '" << method_text << "'\n";
        return 2;
    }

    if (cli.verbose)
    {
        const auto backend = mog::ResolveBackend(options.backend);
        std::cerr << "* backend: " << mog::ToString(backend) << '\n';
        std::cerr << "> " << mog::ToString(*method) << ' ' << cli.url << '\n';
    }

    auto result = mog::request(*method, cli.url, options);
    if (!result)
    {
        std::cerr << "mog: " << result.error().to_string() << '\n';
        return 1;
    }

    const mog::Response &response = *result;
    if (cli.verbose)
    {
        std::cerr << "< HTTP " << response.status_code << ' ' << response.reason << '\n';
        for (const auto &h : response.headers)
        {
            std::cerr << "< " << h.first << ": " << h.second << '\n';
        }
        std::cerr << "* backend used: " << response.backend << '\n';
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
            *out << h.first << ": " << h.second << "\r\n";
        }
        *out << "\r\n";
    }

    if (*method != mog::Method::Head)
    {
        out->write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    }

    if (cli.fail_on_error && response.status_code >= 400)
    {
        return 22; // curl-like exit code for HTTP errors
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    CLI::App app{"mog — lightweight HTTP/S client (embedded backend by default)"};
    app.set_version_flag("-V,--version", std::string{mog::Version()});
    app.require_subcommand(0, 1);

    CliOptions cli;

    // Subcommand style: mog get URL / mog post URL
    auto add_common = [&](CLI::App *sub, const char *default_method) {
        sub->add_option("url", cli.url, "Request URL")->required();
        // Set method when this subcommand is selected (not at registration time).
        sub->callback([&, method = std::string{default_method}]() { cli.method = method; });
        sub->add_option("-H,--header", cli.headers, "HTTP header (Name: value)")->take_all();
        sub->add_option("-d,--data", cli.data, "Request body");
        sub->add_option("-o,--output", cli.output, "Write body to file");
        sub->add_option("--backend", cli.backend,
                        "Backend override: auto|embedded|curl|winhttp|native "
                        "(overrides MOG_BACKEND env)");
        sub->add_option("--timeout", cli.timeout_sec, "Timeout in seconds")->default_val(30.0);
        sub->add_flag("-k,--insecure", cli.insecure, "Disable TLS certificate verification");
        sub->add_flag("-v,--verbose", cli.verbose, "Verbose progress on stderr");
        sub->add_flag("-i,--include", cli.include_headers, "Include response headers in output");
        sub->add_flag("-f,--fail", cli.fail_on_error, "Exit non-zero on HTTP 4xx/5xx");
    };

    auto *get = app.add_subcommand("get", "HTTP GET");
    add_common(get, "GET");
    auto *post = app.add_subcommand("post", "HTTP POST");
    add_common(post, "POST");
    auto *put = app.add_subcommand("put", "HTTP PUT");
    add_common(put, "PUT");
    auto *patch = app.add_subcommand("patch", "HTTP PATCH");
    add_common(patch, "PATCH");
    auto *del = app.add_subcommand("delete", "HTTP DELETE");
    add_common(del, "DELETE");
    auto *head = app.add_subcommand("head", "HTTP HEAD");
    add_common(head, "HEAD");

    // Bare form: mog [options] URL
    app.add_option("url", cli.url, "Request URL");
    app.add_option("-X,--request", cli.method, "HTTP method")->default_val("GET");
    app.add_option("-H,--header", cli.headers, "HTTP header (Name: value)")->take_all();
    app.add_option("-d,--data", cli.data, "Request body");
    app.add_option("-o,--output", cli.output, "Write body to file");
    app.add_option("--backend", cli.backend,
                   "Backend override: auto|embedded|curl|winhttp|native "
                   "(overrides MOG_BACKEND env)");
    app.add_option("--timeout", cli.timeout_sec, "Timeout in seconds")->default_val(30.0);
    app.add_flag("-k,--insecure", cli.insecure, "Disable TLS certificate verification");
    app.add_flag("-v,--verbose", cli.verbose, "Verbose progress on stderr");
    app.add_flag("-i,--include", cli.include_headers, "Include response headers in output");
    app.add_flag("-f,--fail", cli.fail_on_error, "Exit non-zero on HTTP 4xx/5xx");
    app.add_flag("-I,--head", cli.head, "Issue a HEAD request");

    CLI11_PARSE(app, argc, argv);

    if (cli.url.empty())
    {
        std::cerr << "mog: URL required (try: mog get https://example.com)\n";
        return 2;
    }

    // Subcommands set method via default; if a subcommand ran, CLI11 already parsed it.
    // When using bare URL form with -d and default GET, leave as-is (requests-like).
    return Run(cli);
}
