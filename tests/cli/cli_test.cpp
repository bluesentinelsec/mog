/**
 * @file cli_test.cpp
 * @brief Thorough unit tests for every CLI flag / mapping path.
 */

#include "mog/cli.hpp"
#include "mog/mog.hpp"
#include "mog/util.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <stdlib.h>
#include <unistd.h>
#endif

namespace
{

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11
mog::cli::Args MustParse(std::initializer_list<const char *> argv_parts)
{
    std::vector<std::string> args;
    args.emplace_back("mog");
    for (const char *p : argv_parts)
    {
        args.emplace_back(p);
    }
    auto parsed = mog::cli::ParseArgv(args);
    EXPECT_TRUE(parsed) << (parsed ? "" : parsed.error().to_string());
    if (!parsed)
    {
        return {};
    }
    return *parsed;
}
#endif

mog::cli::Prepared MustPrepare(mog::cli::Args args)
{
    auto prepared = mog::cli::PrepareRequest(args);
    EXPECT_TRUE(prepared) << (prepared ? "" : prepared.error().to_string());
    if (!prepared)
    {
        return {};
    }
    return *prepared;
}

std::string TempFile(const std::string &contents)
{
    char path[] = "/tmp/mog_cli_test_XXXXXX";
#if defined(_WIN32)
    // Simple unique path on Windows CI.
    static int n = 0;
    std::string win = "mog_cli_test_" + std::to_string(n++) + ".tmp";
    std::ofstream out(win, std::ios::binary);
    out << contents;
    return win;
#else
    const int fd = mkstemp(path);
    EXPECT_GE(fd, 0);
    if (fd >= 0)
    {
        const auto written = write(fd, contents.data(), contents.size());
        EXPECT_EQ(static_cast<std::size_t>(written), contents.size());
        close(fd);
    }
    return std::string{path};
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Log level resolution (--log-level, -v, -s)
// ---------------------------------------------------------------------------

TEST(CliLogLevel, DefaultIsInfo)
{
    mog::cli::Args a;
    EXPECT_EQ(mog::cli::ResolveLogLevel(a), mog::LogLevel::Info);
}

TEST(CliLogLevel, VerboseIsDebug)
{
    mog::cli::Args a;
    a.verbose = true;
    EXPECT_EQ(mog::cli::ResolveLogLevel(a), mog::LogLevel::Debug);
}

TEST(CliLogLevel, SilentIsOff)
{
    mog::cli::Args a;
    a.silent = true;
    EXPECT_EQ(mog::cli::ResolveLogLevel(a), mog::LogLevel::Off);
}

TEST(CliLogLevel, ExplicitOverridesVerboseAndSilent)
{
    mog::cli::Args a;
    a.verbose = true;
    a.silent = true;
    a.log_level = "warn";
    bool ok = false;
    EXPECT_EQ(mog::cli::ResolveLogLevel(a, &ok), mog::LogLevel::Warn);
    EXPECT_TRUE(ok);
}

TEST(CliLogLevel, InvalidExplicitFallsBackToInfo)
{
    mog::cli::Args a;
    a.log_level = "nope";
    bool ok = true;
    EXPECT_EQ(mog::cli::ResolveLogLevel(a, &ok), mog::LogLevel::Info);
    EXPECT_FALSE(ok);
}

TEST(CliLogLevel, AllNamedLevels)
{
    const std::pair<const char *, mog::LogLevel> cases[] = {
        {"trace", mog::LogLevel::Trace},  {"debug", mog::LogLevel::Debug},
        {"info", mog::LogLevel::Info},    {"warn", mog::LogLevel::Warn},
        {"error", mog::LogLevel::Error},  {"critical", mog::LogLevel::Critical},
        {"off", mog::LogLevel::Off},      {"verbose", mog::LogLevel::Debug},
        {"warning", mog::LogLevel::Warn}, {"silent", mog::LogLevel::Off},
    };
    for (const auto &c : cases)
    {
        mog::cli::Args a;
        a.log_level = c.first;
        bool ok = false;
        EXPECT_EQ(mog::cli::ResolveLogLevel(a, &ok), c.second) << c.first;
        EXPECT_TRUE(ok) << c.first;
    }
}

// ---------------------------------------------------------------------------
// PrepareRequest: each flag → Options / Method
// ---------------------------------------------------------------------------

TEST(CliPrepare, Defaults)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Get);
    EXPECT_EQ(p.url, "https://example.com/");
    EXPECT_EQ(p.options.timeout, std::chrono::seconds(30));
    EXPECT_TRUE(p.options.verify_tls);
    EXPECT_TRUE(p.options.allow_redirects);
    EXPECT_EQ(p.options.max_redirects, 5);
    EXPECT_FALSE(p.options.backend.has_value());
    EXPECT_FALSE(p.fail_on_error);
    EXPECT_FALSE(p.include_headers);
}

TEST(CliPrepare, TimeoutAndConnectTimeout)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.timeout_sec = 12.5;
    a.connect_timeout_sec = 3.0;
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.timeout, std::chrono::milliseconds(12500));
    ASSERT_TRUE(p.options.connect_timeout.has_value());
    EXPECT_EQ(*p.options.connect_timeout, std::chrono::milliseconds(3000));
}

TEST(CliPrepare, InsecureDisablesTlsVerify)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.insecure = true;
    auto p = MustPrepare(a);
    EXPECT_FALSE(p.options.verify_tls);
}

TEST(CliPrepare, NoLocationDisablesRedirects)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.no_location = true;
    a.max_redirs = 2;
    auto p = MustPrepare(a);
    EXPECT_FALSE(p.options.allow_redirects);
    EXPECT_EQ(p.options.max_redirects, 2);
}

TEST(CliPrepare, Backend)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.backend = "embedded";
    auto p = MustPrepare(a);
    ASSERT_TRUE(p.options.backend.has_value());
    EXPECT_EQ(*p.options.backend, mog::Backend::Embedded);
}

TEST(CliPrepare, BackendInvalid)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.backend = "nope";
    auto p = mog::cli::PrepareRequest(a);
    ASSERT_FALSE(p);
    EXPECT_EQ(p.error().code(), mog::ErrorCode::InvalidArgument);
}

TEST(CliPrepare, Headers)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.headers = {"Accept: application/json", "X-Test: 1"};
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.headers["Accept"], "application/json");
    EXPECT_EQ(p.options.headers["X-Test"], "1");
}

TEST(CliPrepare, HeaderInvalid)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.headers = {"NoColon"};
    auto p = mog::cli::PrepareRequest(a);
    ASSERT_FALSE(p);
    EXPECT_EQ(p.error().code(), mog::ErrorCode::InvalidArgument);
}

TEST(CliPrepare, UserAgentAndReferer)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.user_agent = "mog-test/1";
    a.referer = "https://ref.example/";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.user_agent, "mog-test/1");
    EXPECT_EQ(p.options.headers["Referer"], "https://ref.example/");
}

TEST(CliPrepare, ProxyAndCaBundle)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.proxy = "http://127.0.0.1:8080";
    a.ca_bundle = "/etc/ssl/cert.pem";
    auto p = MustPrepare(a);
    ASSERT_TRUE(p.options.proxy.has_value());
    EXPECT_EQ(*p.options.proxy, "http://127.0.0.1:8080");
    ASSERT_TRUE(p.options.ca_bundle.has_value());
    EXPECT_EQ(*p.options.ca_bundle, "/etc/ssl/cert.pem");
}

TEST(CliPrepare, BasicAuthWithPassword)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.user = "alice:s3cret";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.auth.kind, mog::Auth::Kind::Basic);
    EXPECT_EQ(p.options.auth.username, "alice");
    EXPECT_EQ(p.options.auth.password, "s3cret");
}

TEST(CliPrepare, BasicAuthUserOnly)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.user = "bob";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.auth.kind, mog::Auth::Kind::Basic);
    EXPECT_EQ(p.options.auth.username, "bob");
    EXPECT_EQ(p.options.auth.password, "");
}

TEST(CliPrepare, Bearer)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.bearer = "tok-123";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.auth.kind, mog::Auth::Kind::Bearer);
    EXPECT_EQ(p.options.auth.token, "tok-123");
}

TEST(CliPrepare, Cookies)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.cookie = "sid=abc; theme=dark";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.cookies["sid"], "abc");
    EXPECT_EQ(p.options.cookies["theme"], "dark");
}

TEST(CliPrepare, JsonBodyImpliesPost)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.method = "GET";
    a.json = R"({"a":1})";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Post);
    ASSERT_TRUE(p.options.json.has_value());
    EXPECT_EQ(*p.options.json, R"({"a":1})");
}

TEST(CliPrepare, FormImpliesPost)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.form_fields = {"user=alice", "x=1"};
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Post);
    EXPECT_EQ(p.options.form["user"], "alice");
    EXPECT_EQ(p.options.form["x"], "1");
}

TEST(CliPrepare, FormInvalid)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.form_fields = {"nocolon"};
    auto p = mog::cli::PrepareRequest(a);
    ASSERT_FALSE(p);
}

TEST(CliPrepare, DataBodyImpliesPost)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.data = "raw-body";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Post);
    EXPECT_EQ(p.options.body, "raw-body");
}

TEST(CliPrepare, GetWithDataUsesQueryParams)
{
    mog::cli::Args a;
    a.url = "https://example.com/search";
    a.method = "GET";
    a.data = "q=hello&n=1";
    a.get_with_data = true;
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Get);
    EXPECT_TRUE(p.options.body.empty());
    EXPECT_EQ(p.options.params["q"], "hello");
    EXPECT_EQ(p.options.params["n"], "1");
}

TEST(CliPrepare, DataFromFile)
{
    const std::string path = TempFile("file-body");
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.data = "@" + path;
    auto p = MustPrepare(a);
    EXPECT_EQ(p.options.body, "file-body");
    std::remove(path.c_str());
}

TEST(CliPrepare, JsonFromFile)
{
    const std::string path = TempFile(R"({"from":"file"})");
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.json = "@" + path;
    auto p = MustPrepare(a);
    ASSERT_TRUE(p.options.json.has_value());
    EXPECT_EQ(*p.options.json, R"({"from":"file"})");
    std::remove(path.c_str());
}

TEST(CliPrepare, DataFileMissing)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.data = "@/no/such/mog_cli_file_xyz";
    auto p = mog::cli::PrepareRequest(a);
    ASSERT_FALSE(p);
    EXPECT_EQ(p.error().code(), mog::ErrorCode::FileError);
}

TEST(CliPrepare, HeadFlag)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.head = true;
    a.method = "GET";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Head);
}

TEST(CliPrepare, ExplicitMethodPut)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.method = "PUT";
    a.data = "x";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Put);
}

TEST(CliPrepare, ExplicitMethodDelete)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.method = "DELETE";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Delete);
}

TEST(CliPrepare, ExplicitMethodPatch)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.method = "PATCH";
    a.json = "{}";
    auto p = MustPrepare(a);
    EXPECT_EQ(p.method, mog::Method::Patch);
}

TEST(CliPrepare, UnknownMethod)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.method = "TRACE";
    auto p = mog::cli::PrepareRequest(a);
    ASSERT_FALSE(p);
    EXPECT_EQ(p.error().code(), mog::ErrorCode::InvalidArgument);
}

TEST(CliPrepare, OutputAndFlagsPassthrough)
{
    mog::cli::Args a;
    a.url = "https://example.com/";
    a.output = "/tmp/out.bin";
    a.dump_header = "/tmp/hdr.txt";
    a.write_out = "%{http_code}";
    a.include_headers = true;
    a.fail_on_error = true;
    a.verbose = true;
    a.silent = true;
    a.show_error = true;
    auto p = MustPrepare(a);
    EXPECT_EQ(p.output, "/tmp/out.bin");
    EXPECT_EQ(p.dump_header, "/tmp/hdr.txt");
    EXPECT_EQ(p.write_out, "%{http_code}");
    EXPECT_TRUE(p.include_headers);
    EXPECT_TRUE(p.fail_on_error);
    EXPECT_TRUE(p.verbose);
    EXPECT_TRUE(p.silent);
    EXPECT_TRUE(p.show_error);
}

// ---------------------------------------------------------------------------
// write-out + exit codes
// ---------------------------------------------------------------------------

TEST(CliWriteOut, ExpandsTokens)
{
    mog::Response r;
    r.status_code = 201;
    r.url = "https://example.com/final";
    r.body = "hello";
    r.elapsed = std::chrono::milliseconds(1500);
    r.history_len = 2;
    const auto out =
        mog::cli::FormatWriteOut("code=%{http_code} url=%{url_effective} t=%{time_total} "
                                 "n=%{size_download} r=%{num_redirects}",
                                 r);
    EXPECT_NE(out.find("code=201"), std::string::npos);
    EXPECT_NE(out.find("url=https://example.com/final"), std::string::npos);
    EXPECT_NE(out.find("n=5"), std::string::npos);
    EXPECT_NE(out.find("r=2"), std::string::npos);
    EXPECT_NE(out.find("t=1.5"), std::string::npos);
}

TEST(CliExitCode, FailOnError)
{
    mog::cli::Prepared p;
    p.fail_on_error = true;
    mog::Response ok;
    ok.status_code = 200;
    EXPECT_EQ(mog::cli::ExitCodeForResponse(p, ok), 0);
    mog::Response bad;
    bad.status_code = 404;
    EXPECT_EQ(mog::cli::ExitCodeForResponse(p, bad), 22);
    p.fail_on_error = false;
    EXPECT_EQ(mog::cli::ExitCodeForResponse(p, bad), 0);
}

TEST(CliExitCode, TransportError)
{
    mog::cli::Prepared p;
    EXPECT_EQ(mog::cli::ExitCodeForError(p), 1);
}

// ---------------------------------------------------------------------------
// CLI11 ParseArgv — every flag wire-up
// ---------------------------------------------------------------------------

#if defined(MOG_HAS_CLI11) && MOG_HAS_CLI11

TEST(CliParse, BareUrl)
{
    auto a = MustParse({"https://example.com/path"});
    EXPECT_EQ(a.url, "https://example.com/path");
    EXPECT_EQ(a.method, "GET");
}

TEST(CliParse, SubcommandGetPostPutPatchDeleteHead)
{
    EXPECT_EQ(MustParse({"get", "https://e/"}).method, "GET");
    EXPECT_EQ(MustParse({"post", "https://e/"}).method, "POST");
    EXPECT_EQ(MustParse({"put", "https://e/"}).method, "PUT");
    EXPECT_EQ(MustParse({"patch", "https://e/"}).method, "PATCH");
    EXPECT_EQ(MustParse({"delete", "https://e/"}).method, "DELETE");
    EXPECT_EQ(MustParse({"head", "https://e/"}).method, "HEAD");
}

TEST(CliParse, RequestMethodFlag)
{
    auto a = MustParse({"-X", "OPTIONS", "https://e/"});
    EXPECT_EQ(a.method, "OPTIONS");
}

TEST(CliParse, HeadShortcut)
{
    auto a = MustParse({"-I", "https://e/"});
    EXPECT_TRUE(a.head);
}

TEST(CliParse, HeadersRepeatable)
{
    auto a = MustParse({"-H", "A: 1", "-H", "B: two", "https://e/"});
    ASSERT_EQ(a.headers.size(), 2U);
    EXPECT_EQ(a.headers[0], "A: 1");
    EXPECT_EQ(a.headers[1], "B: two");
    EXPECT_EQ(a.url, "https://e/");
}

TEST(CliParse, HeadersUrlFirstOrder)
{
    auto a = MustParse({"https://e/", "-H", "A: 1", "-H", "B: 2"});
    EXPECT_EQ(a.url, "https://e/");
    ASSERT_EQ(a.headers.size(), 2U);
}

TEST(CliParse, SubcommandMultiHeader)
{
    auto a = MustParse({"get", "-H", "A: 1", "-H", "B: 2", "https://e/"});
    EXPECT_EQ(a.method, "GET");
    EXPECT_EQ(a.url, "https://e/");
    ASSERT_EQ(a.headers.size(), 2U);
}

TEST(CliParse, DataJsonForm)
{
    auto d = MustParse({"-d", "body", "https://e/"});
    EXPECT_EQ(d.data, "body");
    auto j = MustParse({"--json", "{\"x\":1}", "https://e/"});
    EXPECT_EQ(j.json, "{\"x\":1}");
    auto f = MustParse({"-F", "a=1", "-F", "b=2", "https://e/"});
    ASSERT_EQ(f.form_fields.size(), 2U);
}

TEST(CliParse, OutputDumpHeaderWriteOut)
{
    auto a = MustParse({"-o", "out.bin", "-D", "h.txt", "-w", "%{http_code}", "https://e/"});
    EXPECT_EQ(a.output, "out.bin");
    EXPECT_EQ(a.dump_header, "h.txt");
    EXPECT_EQ(a.write_out, "%{http_code}");
}

TEST(CliParse, AuthFlags)
{
    auto a = MustParse({"-u", "u:p", "--bearer", "tok", "https://e/"});
    EXPECT_EQ(a.user, "u:p");
    EXPECT_EQ(a.bearer, "tok");
}

TEST(CliParse, UserAgentRefererCookie)
{
    auto a = MustParse({"-A", "ua/1", "-e", "https://ref/", "-b", "a=1; b=2", "https://e/"});
    EXPECT_EQ(a.user_agent, "ua/1");
    EXPECT_EQ(a.referer, "https://ref/");
    EXPECT_EQ(a.cookie, "a=1; b=2");
}

TEST(CliParse, ProxyCacertBackend)
{
    auto a = MustParse(
        {"-x", "http://proxy:1", "--cacert", "ca.pem", "--backend", "embedded", "https://e/"});
    EXPECT_EQ(a.proxy, "http://proxy:1");
    EXPECT_EQ(a.ca_bundle, "ca.pem");
    EXPECT_EQ(a.backend, "embedded");
}

TEST(CliParse, TimeoutsAndMaxRedirs)
{
    auto a =
        MustParse({"--timeout", "9", "--connect-timeout", "2", "--max-redirs", "7", "https://e/"});
    EXPECT_DOUBLE_EQ(a.timeout_sec, 9.0);
    EXPECT_DOUBLE_EQ(a.connect_timeout_sec, 2.0);
    EXPECT_EQ(a.max_redirs, 7);
}

TEST(CliParse, BooleanFlags)
{
    auto a = MustParse({"-k", "-v", "-i", "-f", "--no-location", "-G", "-s", "-S", "https://e/"});
    EXPECT_TRUE(a.insecure);
    EXPECT_TRUE(a.verbose);
    EXPECT_TRUE(a.include_headers);
    EXPECT_TRUE(a.fail_on_error);
    EXPECT_TRUE(a.no_location);
    EXPECT_TRUE(a.get_with_data);
    EXPECT_TRUE(a.silent);
    EXPECT_TRUE(a.show_error);
}

TEST(CliParse, LogLevelFlag)
{
    auto a = MustParse({"--log-level", "trace", "https://e/"});
    EXPECT_EQ(a.log_level, "trace");
}

TEST(CliParse, MissingUrlFails)
{
    auto parsed = mog::cli::ParseArgv(std::vector<std::string>{"mog", "-v"});
    ASSERT_FALSE(parsed);
}

TEST(CliParse, EndToEndPrepareFromArgv)
{
    auto args =
        MustParse({"-X", "POST", "-H", "Content-Type: text/plain", "-d", "hi", "-u", "user:pass",
                   "--timeout", "5", "-k", "--backend", "embedded", "https://example.com/api"});
    auto p = MustPrepare(args);
    EXPECT_EQ(p.method, mog::Method::Post);
    EXPECT_EQ(p.options.body, "hi");
    EXPECT_EQ(p.options.headers["Content-Type"], "text/plain");
    EXPECT_EQ(p.options.auth.kind, mog::Auth::Kind::Basic);
    EXPECT_EQ(p.options.auth.username, "user");
    EXPECT_FALSE(p.options.verify_tls);
    EXPECT_EQ(p.options.timeout, std::chrono::seconds(5));
    ASSERT_TRUE(p.options.backend.has_value());
    EXPECT_EQ(*p.options.backend, mog::Backend::Embedded);
}

TEST(CliParse, SubcommandPostWithJson)
{
    auto args = MustParse({"post", "--json", "{\"ok\":true}", "https://example.com/j"});
    EXPECT_EQ(args.method, "POST");
    auto p = MustPrepare(args);
    EXPECT_EQ(p.method, mog::Method::Post);
    ASSERT_TRUE(p.options.json.has_value());
}

#endif // MOG_HAS_CLI11
