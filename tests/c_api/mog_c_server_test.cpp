// Tests for the C server API (<mog/mog_c.h>). The server is created and driven
// entirely through the C API, and requests are made with the C client API, so
// both halves of the binding are exercised together over a loopback port.

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <mog/mog_c.h>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{

// Perform a GET against the server using the C client API (embedded backend for
// determinism). Returns status; fills body_out.
int ClientGet(const std::string &url, std::string &body_out)
{
    mog_request *req = mog_request_new("GET", url.c_str());
    if (req == nullptr)
    {
        return -1;
    }
    mog_request_set_backend(req, "embedded");
    mog_response *resp = mog_perform(req);
    int status = -1;
    if (resp != nullptr)
    {
        status = mog_response_status(resp);
        size_t len = 0;
        const char *body = mog_response_body(resp, &len);
        body_out.assign(body, len);
    }
    mog_response_free(resp);
    mog_request_free(req);
    return status;
}

std::string Url(mog_server *server, const std::string &path)
{
    return "http://127.0.0.1:" + std::to_string(mog_server_port(server)) + path;
}

void HelloHandler(const mog_server_request *, mog_server_response *resp, void *)
{
    mog_server_response_set_status(resp, 200);
    mog_server_response_set_header(resp, "Content-Type", "text/plain");
    static const char kBody[] = "hi from C";
    mog_server_response_set_body(resp, kBody, std::strlen(kBody));
}

// Echoes selected request fields back through response headers and the body.
void EchoHandler(const mog_server_request *req, mog_server_response *resp, void *)
{
    mog_server_response_set_header(resp, "X-Method", mog_server_request_method(req));
    mog_server_response_set_header(resp, "X-Query", mog_server_request_query(req, "q"));
    mog_server_response_set_header(resp, "X-Custom", mog_server_request_header(req, "X-Custom"));
    size_t len = 0;
    const char *body = mog_server_request_body(req, &len);
    mog_server_response_set_body(resp, body, len);
    mog_server_response_set_status(resp, 200);
}

void CountingHandler(const mog_server_request *, mog_server_response *resp, void *user)
{
    auto *counter = static_cast<std::atomic<int> *>(user);
    counter->fetch_add(1);
    mog_server_response_set_status(resp, 204);
}

class TempDir
{
  public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        path_ = fs::temp_directory_path() / ("mog_c_srv_" +
                                             std::to_string(
#if defined(_WIN32)
                                                 static_cast<long>(_getpid())
#else
                                                 static_cast<long>(::getpid())
#endif
                                                     ) +
                                             "_" + std::to_string(counter.fetch_add(1)));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path &path() const
    {
        return path_;
    }

  private:
    fs::path path_;
};

} // namespace

TEST(MogCServer, RoutesAndServesBody)
{
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    ASSERT_EQ(mog_server_route(server, "GET", "/hello", HelloHandler, nullptr), 0);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);
    EXPECT_EQ(mog_server_is_running(server), 1);
    ASSERT_GT(mog_server_port(server), 0);

    std::string body;
    EXPECT_EQ(ClientGet(Url(server, "/hello"), body), 200);
    EXPECT_EQ(body, "hi from C");

    mog_server_free(server);
}

TEST(MogCServer, HandlerSeesRequestFields)
{
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    ASSERT_EQ(mog_server_route(server, "POST", "/echo", EchoHandler, nullptr), 0);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);

    // POST with a header, query, and body via the C client API.
    mog_request *req = mog_request_new("POST", Url(server, "/echo?q=search").c_str());
    ASSERT_NE(req, nullptr);
    mog_request_set_backend(req, "embedded");
    mog_request_set_header(req, "X-Custom", "value1");
    const std::string payload = "the-body";
    mog_request_set_body(req, payload.data(), payload.size());
    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_status(resp), 200);
    EXPECT_STREQ(mog_response_header(resp, "X-Method"), "POST");
    EXPECT_STREQ(mog_response_header(resp, "X-Query"), "search");
    EXPECT_STREQ(mog_response_header(resp, "X-Custom"), "value1");
    size_t len = 0;
    const char *body = mog_response_body(resp, &len);
    EXPECT_EQ(std::string(body, len), payload);
    mog_response_free(resp);
    mog_request_free(req);

    mog_server_free(server);
}

TEST(MogCServer, ServesStaticFiles)
{
    TempDir dir;
    std::ofstream((dir.path() / "note.txt").string(), std::ios::binary) << "static bytes";

    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    mog_server_serve_files(server, "/", dir.path().string().c_str(), 1);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);

    std::string body;
    EXPECT_EQ(ClientGet(Url(server, "/note.txt"), body), 200);
    EXPECT_EQ(body, "static bytes");

    mog_server_free(server);
}

TEST(MogCServer, DefaultHandlerReceivesUnmatched)
{
    std::atomic<int> hits{0};
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    mog_server_set_default_handler(server, CountingHandler, &hits);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);

    std::string body;
    EXPECT_EQ(ClientGet(Url(server, "/anything"), body), 204);
    EXPECT_EQ(hits.load(), 1);

    mog_server_free(server);
}

TEST(MogCServer, HttpsWithSelfSignedCert)
{
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    ASSERT_EQ(mog_server_use_self_signed_tls(server), 0) << mog_server_last_error(server);
    ASSERT_EQ(mog_server_route(server, "GET", "/s", HelloHandler, nullptr), 0);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);

    // Fetch over HTTPS with verification disabled (self-signed) via the C client.
    const std::string url = "https://127.0.0.1:" + std::to_string(mog_server_port(server)) + "/s";
    mog_request *req = mog_request_new("GET", url.c_str());
    ASSERT_NE(req, nullptr);
    mog_request_set_backend(req, "embedded");
    mog_request_set_verify_tls(req, 0);
    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_ok(resp), 1) << mog_response_error_message(resp);
    EXPECT_EQ(mog_response_status(resp), 200);
    size_t len = 0;
    const char *body = mog_response_body(resp, &len);
    EXPECT_EQ(std::string(body, len), "hi from C");
    mog_response_free(resp);
    mog_request_free(req);

    mog_server_free(server);
}

TEST(MogCServer, RejectsInvalidMethodAndNullArgs)
{
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    EXPECT_NE(mog_server_route(server, "NOTAMETHOD", "/x", HelloHandler, nullptr), 0);
    EXPECT_NE(mog_server_route(server, "GET", "/x", nullptr, nullptr), 0);
    mog_server_free(server);

    // NULL-tolerant.
    mog_server_free(nullptr);
    EXPECT_EQ(mog_server_is_running(nullptr), 0);
    EXPECT_EQ(mog_server_port(nullptr), 0);
    EXPECT_STREQ(mog_server_last_error(nullptr), "");
}

TEST(MogCServer, StopEndsRunning)
{
    mog_server *server = mog_server_new();
    ASSERT_NE(server, nullptr);
    mog_server_set_port(server, 0);
    ASSERT_EQ(mog_server_start(server), 0) << mog_server_last_error(server);
    EXPECT_EQ(mog_server_is_running(server), 1);
    mog_server_stop(server);
    EXPECT_EQ(mog_server_is_running(server), 0);
    mog_server_free(server);
}
