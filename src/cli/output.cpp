/**
 * @file output.cpp
 * @brief CLI presentation: write-out format, header dump, body output, exit codes.
 */

#include "mog/cli.hpp"
#include "mog/util.hpp"

#include <fstream>
#include <sstream>

namespace mog::cli
{
namespace
{

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

std::string FormatHeaderBlock(const Response &response)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << response.status_code << ' ' << response.reason << "\r\n";
    for (const auto &h : response.headers)
    {
        oss << h.name << ": " << h.value << "\r\n";
    }
    oss << "\r\n";
    return oss.str();
}

Result<void> WriteHeaderDump(const Prepared &prepared, const Response &response)
{
    if (prepared.dump_header.empty())
    {
        return Result<void>::Ok();
    }
    std::ofstream hdr(prepared.dump_header, std::ios::binary);
    if (!hdr)
    {
        return Result<void>::Err(Error{ErrorCode::FileError,
                                       "failed to open header dump file: " + prepared.dump_header});
    }
    hdr << FormatHeaderBlock(response);
    return Result<void>::Ok();
}

Result<void> WriteResponseOutput(const Prepared &prepared, const Response &response,
                                 std::ostream &out)
{
    if (prepared.include_headers)
    {
        out << FormatHeaderBlock(response);
    }
    if (prepared.method != Method::Head && prepared.output != "/dev/null")
    {
        out.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
    }
    return Result<void>::Ok();
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

int ExitCodeForPrepareError(const Error &error)
{
    return error.code() == ErrorCode::FileError ? 1 : 2;
}

} // namespace mog::cli
