/**
 * @file android_test.cpp
 * @brief End-to-end Android tests consuming the mog Prefab package.
 */

#include <android/log.h>
#include <jni.h>
#include <mog/mog.hpp>
#include <string>
#include <string_view>

namespace
{

constexpr const char *kLogTag = "mog-android-test";

class TestRun
{
  public:
    void Check(bool condition, std::string_view message)
    {
        if (condition)
        {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "PASS: %.*s",
                                static_cast<int>(message.size()), message.data());
            return;
        }
        ++failures_;
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "FAIL: %.*s",
                            static_cast<int>(message.size()), message.data());
    }

    [[nodiscard]] int failures() const noexcept
    {
        return failures_;
    }

  private:
    int failures_ = 0;
};

std::string Origin(std::string_view scheme, const mog::Server &server)
{
    return std::string{scheme} + "://127.0.0.1:" + std::to_string(server.port());
}

void TestHttpClientAndServer(TestRun &run)
{
    mog::ServerOptions server_options;
    server_options.port = 0;
    mog::Server server(server_options);
    server.route(mog::Method::Get, "/android", [](const mog::ServerRequest &) {
        return mog::ServerResponse::Text(200, "mog-on-android");
    });

    const auto started = server.start();
    run.Check(started.has_value(), "start loopback HTTP server");
    if (!started)
    {
        return;
    }

    const auto response = mog::get(Origin("http", server) + "/android");
    run.Check(response.has_value(), "Auto performs a loopback HTTP request");
    if (response)
    {
        run.Check(response->status_code == 200, "HTTP response status is 200");
        run.Check(response->body == "mog-on-android", "HTTP response body is preserved");
        run.Check(response->backend == "embedded", "Auto uses the embedded Android backend");
    }

    mog::Options curl_options;
    curl_options.backend = mog::Backend::Curl;
    const auto curl_response = mog::get(Origin("http", server) + "/android", curl_options);
    run.Check(!curl_response.has_value(), "Android does not expose a system curl backend");
    if (!curl_response)
    {
        run.Check(curl_response.error().code() == mog::ErrorCode::UnsupportedBackend,
                  "explicit curl returns UnsupportedBackend");
    }
}

void TestHttpsClientAndServer(TestRun &run)
{
    const auto tls = mog::TlsServerConfig::SelfSigned("localhost");
    run.Check(tls.has_value(), "generate a self-signed Android test certificate");
    if (!tls)
    {
        return;
    }

    mog::ServerOptions server_options;
    server_options.port = 0;
    server_options.tls = tls.value();
    mog::Server server(server_options);
    server.route(mog::Method::Get, "/secure", [](const mog::ServerRequest &) {
        return mog::ServerResponse::Text(200, "encrypted");
    });

    const auto started = server.start();
    run.Check(started.has_value(), "start loopback HTTPS server");
    if (!started)
    {
        return;
    }

    mog::Options options;
    options.verify_tls = false;
    const auto response = mog::get(Origin("https", server) + "/secure", options);
    run.Check(response.has_value(), "perform an encrypted loopback request");
    if (response)
    {
        run.Check(response->status_code == 200, "HTTPS response status is 200");
        run.Check(response->body == "encrypted", "HTTPS response body is preserved");
        run.Check(response->backend == "embedded", "HTTPS uses the embedded Android backend");
    }
}

int RunTests()
{
    TestRun run;
    run.Check(!mog::Version().empty(), "Prefab package exports the generated version API");
    run.Check(mog::ResolveBackend() == mog::Backend::Embedded,
              "Android Auto resolves to the embedded backend");
    TestHttpClientAndServer(run);
    TestHttpsClientAndServer(run);
    return run.failures();
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_bluesentinelsec_mog_test_TestActivity_runNativeTests(JNIEnv *, jclass)
{
    return RunTests();
}
