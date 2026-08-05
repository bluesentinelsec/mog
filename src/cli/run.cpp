/**
 * @file run.cpp
 * @brief CLI use-case orchestration: log config → prepare → request → output.
 */

#include "mog/backend.hpp"
#include "mog/cli.hpp"
#include "mog/http.hpp"
#include "mog/log.hpp"

#include <fstream>
#include <utility>

namespace mog::cli
{

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

bool ConfigureLogging(const Args &args)
{
    bool ok = true;
    const LogLevel level = ResolveLogLevel(args, &ok);
    UseDefaultLogger(level);
    MOG_LOG_DEBUG("cli: logging configured level={}", ToString(level));
    return ok;
}

int Run(const Prepared &prepared, Streams streams)
{
    // Log the requested backend; the request layer logs the concrete one actually
    // used (Auto may resolve to native or fall back to embedded per request).
    const std::string requested = prepared.options.backend.has_value()
                                      ? std::string{ToString(*prepared.options.backend)}
                                      : "auto";
    MOG_LOG_INFO("cli: {} {} backend={}", ToString(prepared.method), prepared.url, requested);

    // Stream the body straight to the output file for real downloads, so large
    // responses never sit fully in memory. Headers-in-body (-i) still uses the
    // buffered path so the header block precedes the body in the same stream.
    const bool stream_to_file = !prepared.output.empty() && prepared.output != "/dev/null" &&
                                prepared.method != Method::Head && !prepared.include_headers;

    Options options = prepared.options;
    if (stream_to_file)
    {
        auto writer = FileWriter(prepared.output);
        if (!writer)
        {
            MOG_LOG_ERROR("{}", writer.error().to_string());
            return 1;
        }
        options.response_writer = std::move(*writer);
    }

    auto result = request(prepared.method, prepared.url, options);
    if (!result)
    {
        if (!prepared.silent || prepared.show_error)
        {
            MOG_LOG_ERROR("{}", result.error().to_string());
            if (prepared.silent && prepared.show_error && streams.err != nullptr)
            {
                *streams.err << "mog: " << result.error().to_string() << '\n';
            }
        }
        return ExitCodeForError(prepared);
    }

    const Response &response = *result;
    MOG_LOG_INFO("cli: HTTP {} {} ({} bytes, {} ms, backend={})", response.status_code,
                 response.reason, response.downloaded_bytes, response.elapsed.count(),
                 response.backend);
    if (prepared.verbose)
    {
        for (const auto &h : response.headers)
        {
            static_cast<void>(h);
            MOG_LOG_DEBUG("cli: < {}: {}", h.name, h.value);
        }
    }

    auto dump = WriteHeaderDump(prepared, response);
    if (!dump)
    {
        MOG_LOG_ERROR("{}", dump.error().to_string());
        return 1;
    }

    // When streaming, the body has already been written to the output file by the
    // response writer; only non-streaming responses need explicit output here.
    if (!stream_to_file)
    {
        std::ostream *out = streams.out != nullptr ? streams.out : &std::cout;
        std::ofstream file;
        if (!prepared.output.empty())
        {
            file.open(prepared.output, std::ios::binary);
            if (!file)
            {
                MOG_LOG_ERROR("failed to open output file: {}", prepared.output);
                return 1;
            }
            out = &file;
        }

        auto written = WriteResponseOutput(prepared, response, *out);
        if (!written)
        {
            MOG_LOG_ERROR("{}", written.error().to_string());
            return 1;
        }
    }

    if (!prepared.write_out.empty() && streams.err != nullptr)
    {
        *streams.err << FormatWriteOut(prepared.write_out, response);
    }

    return ExitCodeForResponse(prepared, response);
}

int Run(const Args &args, Streams streams)
{
    if (!ConfigureLogging(args) && streams.err != nullptr)
    {
        *streams.err << "mog: unknown log level '" << args.log_level
                     << "' (trace|debug|info|warn|error|critical|off)\n";
    }

    auto prepared = PrepareRequest(args);
    if (!prepared)
    {
        MOG_LOG_ERROR("{}", prepared.error().to_string());
        if (streams.err != nullptr)
        {
            // Ensure prepare errors are visible even when log level is off.
            if (args.silent)
            {
                *streams.err << "mog: " << prepared.error().to_string() << '\n';
            }
        }
        return ExitCodeForPrepareError(prepared.error());
    }
    return Run(*prepared, streams);
}

} // namespace mog::cli
