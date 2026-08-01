// Tests for the embedded HTTP server (mog::Server).
//
// The server is driven by mog's own client (nice symmetry) on an ephemeral
// port, with the embedded backend for determinism. A few cases use the internal
// TcpSocket directly to exercise raw wire behavior (chunked request bodies).

#include "http/detail/socket.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <mog/mog.hpp>
#include <mog/server.hpp>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{

// A unique temporary directory, removed on destruction.
class TempDir
{
  public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        path_ = fs::temp_directory_path() / ("mog_srv_" +
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
    void write(const std::string &name, const std::string &content)
    {
        std::ofstream out((path_ / name).string(), std::ios::binary);
        out << content;
    }

  private:
    fs::path path_;
};

mog::Options Embedded()
{
    mog::Options opt;
    opt.backend = mog::Backend::Embedded;
    return opt;
}

std::string BaseUrl(const mog::Server &server)
{
    return "http://127.0.0.1:" + std::to_string(server.port());
}

} // namespace

TEST(ServerTest, RoutesGetAndReturnsBody)
{
    mog::ServerOptions opt;
    opt.port = 0;
    mog::Server server(opt);
    server.route(mog::Method::Get, "/hello", [](const mog::ServerRequest &) {
        return mog::ServerResponse::Text(200, "hi there");
    });
    ASSERT_TRUE(server.start());
    ASSERT_GT(server.port(), 0);

    auto r = mog::get(BaseUrl(server) + "/hello", Embedded());
    ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "hi there");
    EXPECT_EQ(r->header("Content-Type"), "text/plain; charset=utf-8");
}

TEST(ServerTest, EchoesBodyHeadersAndQuery)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Post, "/echo", [](const mog::ServerRequest &req) {
        auto resp = mog::ServerResponse::Text(200, req.body);
        resp.set_header("X-Seen-Header", req.header("X-Custom"));
        auto it = req.params.find("q");
        resp.set_header("X-Seen-Query", it != req.params.end() ? it->second : "");
        return resp;
    });
    ASSERT_TRUE(server.start());

    mog::Options opt = Embedded();
    opt.headers["X-Custom"] = "value1";
    opt.body = "payload-bytes";
    auto r = mog::post(BaseUrl(server) + "/echo?q=search+term", opt);
    ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "payload-bytes");
    EXPECT_EQ(r->header("X-Seen-Header"), "value1");
    EXPECT_EQ(r->header("X-Seen-Query"), "search term");
}

TEST(ServerTest, DefaultHandlerAnd404)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    ASSERT_TRUE(server.start());
    auto r = mog::get(BaseUrl(server) + "/nope", Embedded());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status_code, 404);
}

TEST(ServerTest, HeadOmitsBodyButSetsLength)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/page", [](const mog::ServerRequest &) {
        return mog::ServerResponse::Text(200, "0123456789");
    });
    ASSERT_TRUE(server.start());

    auto r = mog::head(BaseUrl(server) + "/page", Embedded());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->header("Content-Length"), "10");
    EXPECT_TRUE(r->text().empty());
}

TEST(ServerTest, StaticFileServingAndMime)
{
    TempDir dir;
    dir.write("data.json", "{\"ok\":true}");

    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.serve_files("/", dir.path().string());
    ASSERT_TRUE(server.start());

    auto r = mog::get(BaseUrl(server) + "/data.json", Embedded());
    ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "{\"ok\":true}");
    EXPECT_EQ(r->header("Content-Type"), "application/json");
    EXPECT_EQ(r->header("Accept-Ranges"), "bytes");
}

TEST(ServerTest, StaticIndexAndDirectoryListing)
{
    TempDir dir;
    dir.write("index.html", "<h1>home</h1>");
    fs::create_directories(dir.path() / "sub");
    std::ofstream((dir.path() / "sub" / "a.txt").string()) << "A";

    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.serve_files("/", dir.path().string());
    ASSERT_TRUE(server.start());

    // Root serves index.html.
    auto root = mog::get(BaseUrl(server) + "/", Embedded());
    ASSERT_TRUE(root);
    EXPECT_EQ(root->status_code, 200);
    EXPECT_EQ(root->text(), "<h1>home</h1>");

    // A directory without an index lists its contents.
    auto listing = mog::get(BaseUrl(server) + "/sub/", Embedded());
    ASSERT_TRUE(listing);
    EXPECT_EQ(listing->status_code, 200);
    EXPECT_NE(listing->text().find("a.txt"), std::string::npos);
}

TEST(ServerTest, StaticRejectsPathTraversal)
{
    TempDir dir;
    dir.write("public.txt", "public");

    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.serve_files("/", dir.path().string());
    ASSERT_TRUE(server.start());

    auto r = mog::get(BaseUrl(server) + "/../../etc/hosts", Embedded());
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status_code, 404);
}

TEST(ServerTest, RangeRequestReturnsPartialContent)
{
    TempDir dir;
    dir.write("blob.bin", "0123456789");

    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.serve_files("/", dir.path().string());
    ASSERT_TRUE(server.start());

    mog::Options opt = Embedded();
    opt.headers["Range"] = "bytes=2-5";
    auto r = mog::get(BaseUrl(server) + "/blob.bin", opt);
    ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
    EXPECT_EQ(r->status_code, 206);
    EXPECT_EQ(r->text(), "2345");
    EXPECT_EQ(r->header("Content-Range"), "bytes 2-5/10");
}

TEST(ServerTest, ConditionalGetReturns304)
{
    TempDir dir;
    dir.write("cached.txt", "cache me");

    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.serve_files("/", dir.path().string());
    ASSERT_TRUE(server.start());

    auto first = mog::get(BaseUrl(server) + "/cached.txt", Embedded());
    ASSERT_TRUE(first);
    const std::string last_modified = first->header("Last-Modified");
    ASSERT_FALSE(last_modified.empty());

    mog::Options opt = Embedded();
    opt.headers["If-Modified-Since"] = last_modified;
    auto second = mog::get(BaseUrl(server) + "/cached.txt", opt);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->status_code, 304);
}

TEST(ServerTest, StreamingChunkedResponse)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/stream", [](const mog::ServerRequest &) {
        mog::ServerResponse r;
        r.status_code = 200;
        r.set_header("Content-Type", "text/plain");
        r.body_producer = [](const mog::ResponseSink &sink) -> mog::Result<void> {
            for (int i = 0; i < 5; ++i)
            {
                auto w = sink("chunk" + std::to_string(i) + ";");
                if (!w)
                {
                    return w;
                }
            }
            return mog::Result<void>::Ok();
        };
        return r;
    });
    ASSERT_TRUE(server.start());

    auto r = mog::get(BaseUrl(server) + "/stream", Embedded());
    ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "chunk0;chunk1;chunk2;chunk3;chunk4;");
}

TEST(ServerTest, KeepAliveAcrossRequests)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/ping",
                 [](const mog::ServerRequest &) { return mog::ServerResponse::Text(200, "pong"); });
    ASSERT_TRUE(server.start());

    mog::Session session;
    session.set_base_url(BaseUrl(server));
    for (int i = 0; i < 5; ++i)
    {
        auto r = session.get("/ping");
        ASSERT_TRUE(r) << (r ? "" : r.error().to_string());
        EXPECT_EQ(r->status_code, 200);
        EXPECT_EQ(r->text(), "pong");
    }
}

TEST(ServerTest, HandlesConcurrentClients)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/n", [](const mog::ServerRequest &req) {
        auto it = req.params.find("i");
        return mog::ServerResponse::Text(200, it != req.params.end() ? it->second : "?");
    });
    ASSERT_TRUE(server.start());

    constexpr int kThreads = 16;
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&, i] {
            auto r = mog::get(BaseUrl(server) + "/n?i=" + std::to_string(i), Embedded());
            if (r && r->status_code == 200 && r->text() == std::to_string(i))
            {
                ok.fetch_add(1);
            }
        });
    }
    for (auto &t : threads)
    {
        t.join();
    }
    EXPECT_EQ(ok.load(), kThreads);
}

TEST(ServerTest, DecodesChunkedRequestBody)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Post, "/echo", [](const mog::ServerRequest &req) {
        return mog::ServerResponse::Text(200, req.body);
    });
    ASSERT_TRUE(server.start());

    auto sock = mog::detail::TcpSocket::Connect("127.0.0.1", server.port(), 2s);
    ASSERT_TRUE(sock) << (sock ? "" : sock.error().to_string());

    const std::string request = "POST /echo HTTP/1.1\r\n"
                                "Host: localhost\r\n"
                                "Transfer-Encoding: chunked\r\n"
                                "Connection: close\r\n"
                                "\r\n"
                                "5\r\nhello\r\n"
                                "6\r\n world\r\n"
                                "0\r\n\r\n";
    ASSERT_TRUE(sock->SendAll(request.data(), request.size(), 2s));

    std::string response;
    for (;;)
    {
        char buf[4096];
        auto n = sock->RecvSome(buf, sizeof(buf), 2s);
        if (!n || *n == 0)
        {
            break;
        }
        response.append(buf, *n);
    }
    EXPECT_NE(response.find(" 200 "), std::string::npos);
    EXPECT_NE(response.find("hello world"), std::string::npos);
}

TEST(ServerTest, Http10ClosesByDefault)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/x", [](const mog::ServerRequest &) {
        return mog::ServerResponse::Text(200, "body10");
    });
    ASSERT_TRUE(server.start());

    auto sock = mog::detail::TcpSocket::Connect("127.0.0.1", server.port(), 2s);
    ASSERT_TRUE(sock) << (sock ? "" : sock.error().to_string());
    // HTTP/1.0 with no Connection header: the server must close after responding.
    const std::string request = "GET /x HTTP/1.0\r\nHost: localhost\r\n\r\n";
    ASSERT_TRUE(sock->SendAll(request.data(), request.size(), 2s));

    std::string response;
    bool closed = false;
    for (;;)
    {
        char buf[2048];
        auto n = sock->RecvSome(buf, sizeof(buf), 2s);
        if (!n)
        {
            break; // timeout: connection did not close (would fail the assert below)
        }
        if (*n == 0)
        {
            closed = true;
            break;
        }
        response.append(buf, *n);
    }
    EXPECT_TRUE(closed);
    EXPECT_NE(response.find("body10"), std::string::npos);
    EXPECT_NE(response.find("Connection: close"), std::string::npos);
}

TEST(ServerTest, RejectsOversizedBody)
{
    mog::ServerOptions opt{"127.0.0.1", 0};
    opt.max_body_bytes = 16;
    mog::Server server(opt);
    server.route(mog::Method::Post, "/small", [](const mog::ServerRequest &req) {
        return mog::ServerResponse::Text(200, req.body);
    });
    ASSERT_TRUE(server.start());

    mog::Options o = Embedded();
    o.body = std::string(100, 'x');
    auto r = mog::post(BaseUrl(server) + "/small", o);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status_code, 413);
}

TEST(ServerTest, StopAndRestart)
{
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/x",
                 [](const mog::ServerRequest &) { return mog::ServerResponse::Text(200, "x"); });
    ASSERT_TRUE(server.start());
    EXPECT_TRUE(server.running());
    server.stop();
    EXPECT_FALSE(server.running());
}
