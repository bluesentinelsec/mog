/**
 * @file prepare.cpp
 * @brief Map CLI Args → Prepared Options (single responsibility: domain mapping).
 */

#include "mog/backend.hpp"
#include "mog/cli.hpp"
#include "mog/util.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

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

// Split a string on the first occurrence of a delimiter char.
std::vector<std::string> SplitOn(const std::string &s, char delim)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
    {
        if (i == s.size() || s[i] == delim)
        {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

// Parse one curl-style -F argument into a multipart FormPart.
//   name=value                     text field
//   name=@path[;type=..][;filename=..]   file upload (bytes read from disk)
//   name=<path                     text field whose value is read from a file
Result<FormPart> ParseFormArg(const std::string &arg)
{
    const auto eq = arg.find('=');
    if (eq == std::string::npos || eq == 0)
    {
        return Result<FormPart>::Err(
            Error{ErrorCode::InvalidArgument, "invalid form field (expected name=value): " + arg});
    }
    FormPart part;
    part.name = arg.substr(0, eq);
    const std::string rest = arg.substr(eq + 1);

    if (!rest.empty() && (rest.front() == '@' || rest.front() == '<'))
    {
        const bool is_file = rest.front() == '@';
        const auto segments = SplitOn(rest.substr(1), ';');
        const std::string path = segments.empty() ? std::string{} : segments.front();
        auto data = ReadFile(path);
        if (!data)
        {
            return Result<FormPart>::Err(data.error());
        }
        part.value = std::move(*data);
        if (is_file)
        {
            part.filename = std::filesystem::path(path).filename().string();
            for (std::size_t i = 1; i < segments.size(); ++i)
            {
                const std::string &param = segments[i];
                const auto peq = param.find('=');
                if (peq == std::string::npos)
                {
                    continue;
                }
                const std::string key = param.substr(0, peq);
                const std::string val = param.substr(peq + 1);
                if (key == "type")
                {
                    part.content_type = val;
                }
                else if (key == "filename")
                {
                    part.filename = val;
                }
            }
        }
        // '<' yields a plain field (value from file), so leave filename unset.
    }
    else
    {
        part.value = rest;
    }
    return Result<FormPart>::Ok(std::move(part));
}

} // namespace

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
    options.decompress = !args.no_decompress;

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
    if (!args.client_cert.empty())
    {
        options.client_cert = args.client_cert;
    }
    if (!args.client_key.empty())
    {
        options.client_key = args.client_key;
    }
    if (!args.client_key_password.empty())
    {
        options.client_key_password = args.client_key_password;
    }

    if (!args.user.empty())
    {
        const auto colon = args.user.find(':');
        const std::string user =
            colon == std::string::npos ? args.user : args.user.substr(0, colon);
        const std::string pass = colon == std::string::npos ? "" : args.user.substr(colon + 1);
        if (args.digest)
        {
            WithDigestAuth(options, user, pass);
        }
        else
        {
            WithBasicAuth(options, user, pass);
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
        // -F builds a multipart/form-data body (curl-compatible). This differs
        // from mog's earlier -F, which produced urlencoded fields.
        for (const auto &f : args.form_fields)
        {
            auto part = ParseFormArg(f);
            if (!part)
            {
                return Result<Prepared>::Err(part.error());
            }
            options.multipart.push_back(std::move(*part));
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
    if ((options.json.has_value() || !options.form.empty() || !options.multipart.empty() ||
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

} // namespace mog::cli
