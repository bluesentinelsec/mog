/**
 * @file cli.hpp
 * @brief Testable CLI argument model and mapping into library Options.
 *
 * The mog executable uses CLI11 to fill @ref Args, then @ref PrepareRequest
 * / @ref ResolveLogLevel. Unit tests exercise the same code paths without
 * spawning a process for most cases.
 */
#pragma once

#include "mog/error.hpp"
#include "mog/log.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mog::cli
{

/**
 * @brief Parsed CLI arguments (mirrors CLI11 bindings).
 */
struct Args
{
    std::string url;
    std::string method = "GET";
    std::vector<std::string> headers;
    std::string data;
    std::string json;
    std::vector<std::string> form_fields;
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
    bool get_with_data = false;
    bool silent = false;
    bool show_error = false;
    std::string write_out;
    std::string log_level;
};

/**
 * @brief Fully resolved CLI request ready for mog::request / output handling.
 */
struct Prepared
{
    Options options;
    Method method = Method::Get;
    std::string url;
    std::string output;
    std::string dump_header;
    std::string write_out;
    bool include_headers = false;
    bool fail_on_error = false;
    bool verbose = false;
    bool silent = false;
    bool show_error = false;
};

/**
 * @brief Resolve log level from flags.
 *
 * Precedence: @c log_level > @c silent (off) > @c verbose (debug) > info.
 * Invalid @c log_level falls back to info and returns false via @p ok.
 */
[[nodiscard]] LogLevel ResolveLogLevel(const Args &args, bool *ok = nullptr);

/**
 * @brief Load @c -d / --json values; supports @file syntax.
 */
[[nodiscard]] Result<std::string> LoadDataArg(std::string_view data);

/**
 * @brief Map CLI args into library Options + Method.
 * @return Prepared request, or Error (invalid header/backend/method/form/file).
 */
[[nodiscard]] Result<Prepared> PrepareRequest(const Args &args);

/**
 * @brief Expand curl-style -w format tokens against a response.
 *
 * Supports: %{http_code} %{url_effective} %{time_total} %{size_download}
 * %{num_redirects}
 */
[[nodiscard]] std::string FormatWriteOut(std::string_view format, const Response &response);

/**
 * @brief Exit code after a completed exchange (0, or 22 when -f and 4xx/5xx).
 */
[[nodiscard]] int ExitCodeForResponse(const Prepared &prepared, const Response &response);

/**
 * @brief Exit code for a failed transport Result.
 */
[[nodiscard]] int ExitCodeForError(const Prepared &prepared);

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11
/**
 * @brief Parse argv with CLI11 into Args (does not run the request).
 *
 * @return Args on success; Error on CLI11 parse failure or missing URL.
 */
[[nodiscard]] Result<Args> ParseArgv(int argc, char **argv);
[[nodiscard]] Result<Args> ParseArgv(const std::vector<std::string> &args);
#endif

} // namespace mog::cli
