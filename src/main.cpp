/**
 * @file main.cpp
 * @brief mog CLI entrypoint — thin wrapper over mog::cli + library.
 */

#include "mog/mog.hpp"

#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

int Run(const mog::cli::Args &args)
{
    bool level_ok = true;
    const mog::LogLevel level = mog::cli::ResolveLogLevel(args, &level_ok);
    if (!level_ok)
    {
        std::cerr << "mog: unknown log level '" << args.log_level
                  << "' (trace|debug|info|warn|error|critical|off)\n";
    }
    mog::UseDefaultLogger(level);
    MOG_LOG_DEBUG("cli: logging configured level={}", mog::ToString(level));

    auto prepared = mog::cli::PrepareRequest(args);
    if (!prepared)
    {
        MOG_LOG_ERROR("{}", prepared.error().to_string());
        return prepared.error().code() == mog::ErrorCode::FileError ? 1 : 2;
    }

    const auto backend = mog::ResolveBackend(prepared->options.backend);
    MOG_LOG_INFO("cli: {} {} backend={}", mog::ToString(prepared->method), prepared->url,
                 mog::ToString(backend));

    auto result = mog::request(prepared->method, prepared->url, prepared->options);
    if (!result)
    {
        if (!prepared->silent || prepared->show_error)
        {
            MOG_LOG_ERROR("{}", result.error().to_string());
            if (prepared->silent && prepared->show_error)
            {
                std::cerr << "mog: " << result.error().to_string() << '\n';
            }
        }
        return mog::cli::ExitCodeForError(*prepared);
    }

    const mog::Response &response = *result;
    MOG_LOG_INFO("cli: HTTP {} {} ({} bytes, {} ms, backend={})", response.status_code,
                 response.reason, response.body.size(), response.elapsed.count(), response.backend);
    if (prepared->verbose)
    {
        for (const auto &h : response.headers)
        {
            MOG_LOG_DEBUG("cli: < {}: {}", h.name, h.value);
        }
    }

    if (!prepared->dump_header.empty())
    {
        std::ofstream hdr(prepared->dump_header, std::ios::binary);
        if (!hdr)
        {
            MOG_LOG_ERROR("failed to open header dump file: {}", prepared->dump_header);
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
    if (!prepared->output.empty())
    {
        file.open(prepared->output, std::ios::binary);
        if (!file)
        {
            MOG_LOG_ERROR("failed to open output file: {}", prepared->output);
            return 1;
        }
        out = &file;
    }

    if (prepared->include_headers)
    {
        *out << "HTTP/1.1 " << response.status_code << ' ' << response.reason << "\r\n";
        for (const auto &h : response.headers)
        {
            *out << h.name << ": " << h.value << "\r\n";
        }
        *out << "\r\n";
    }

    if (prepared->method != mog::Method::Head && prepared->output != "/dev/null")
    {
        out->write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    }

    if (!prepared->write_out.empty())
    {
        std::cerr << mog::cli::FormatWriteOut(prepared->write_out, response);
    }

    return mog::cli::ExitCodeForResponse(*prepared, response);
}

} // namespace

int main(int argc, char **argv)
{
    // Stamp real version on the CLI11 app via a local parse wrapper.
    // ParseArgv uses a placeholder version flag; re-parse here with Version().
    CLI::App app{"mog — lightweight HTTP/S client (embedded backend by default)"};
    app.set_version_flag("-V,--version", std::string{mog::Version()});

    // Use library ParseArgv for flag binding; inject version-aware help via early check.
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "-V" || a == "--version")
        {
            std::cout << mog::Version() << '\n';
            return 0;
        }
    }

    auto parsed = mog::cli::ParseArgv(argc, argv);
    if (!parsed)
    {
        // CLI11 parse errors / missing URL
        const std::string msg{parsed.error().message()};
        if (msg.find("URL required") != std::string::npos)
        {
            std::cerr << "mog: " << msg << '\n';
            return 2;
        }
        // Help/version and other CLI11 messages
        std::cerr << msg << '\n';
        return 2;
    }

    return Run(*parsed);
}
