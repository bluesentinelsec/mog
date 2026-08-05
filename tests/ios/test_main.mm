/**
 * @file test_main.mm
 * @brief End-to-end iOS tests consuming the release XCFramework.
 */

#import <UIKit/UIKit.h>
#import <os/log.h>

#include <mog/mog.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace
{

os_log_t TestLog()
{
    static os_log_t log = os_log_create("com.bluesentinelsec.mog.test", "tests");
    return log;
}

class TestRun
{
  public:
    void Check(bool condition, std::string_view message)
    {
        const std::string text{message};
        if (condition)
        {
            os_log_info(TestLog(), "PASS: %{public}s", text.c_str());
            return;
        }
        ++failures_;
        os_log_error(TestLog(), "FAIL: %{public}s", text.c_str());
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
    mog::Server server(mog::ServerOptions{"127.0.0.1", 0});
    server.route(mog::Method::Get, "/ios",
                 [](const mog::ServerRequest &) { return mog::ServerResponse::Text(200, "mog-on-ios"); });

    const auto started = server.start();
    run.Check(started.has_value(), "start loopback HTTP server");
    if (!started)
    {
        return;
    }

    const std::string url = Origin("http", server) + "/ios";
    const auto native_response = mog::get(url);
    run.Check(native_response.has_value(), "Auto performs a loopback HTTP request");
    if (native_response)
    {
        run.Check(native_response->status_code == 200, "HTTP response status is 200");
        run.Check(native_response->body == "mog-on-ios", "HTTP response body is preserved");
        run.Check(native_response->backend == "native", "Auto uses NSURLSession on iOS");
    }

    mog::Options embedded_options;
    embedded_options.backend = mog::Backend::Embedded;
    const auto embedded_response = mog::get(url, embedded_options);
    run.Check(embedded_response.has_value(), "embedded fallback performs an HTTP request");
    if (embedded_response)
    {
        run.Check(embedded_response->backend == "embedded", "explicit embedded request reports its backend");
    }

    mog::Options auto_proxy_options;
    auto_proxy_options.proxy = "https://unsupported-proxy.invalid";
    const auto auto_proxy_response = mog::get(url, auto_proxy_options);
    run.Check(!auto_proxy_response && auto_proxy_response.error().code() == mog::ErrorCode::ProxyError,
              "Auto routes an explicit proxy through the embedded backend");

    mog::Options native_proxy_options = auto_proxy_options;
    native_proxy_options.backend = mog::Backend::Native;
    const auto native_proxy_response = mog::get(url, native_proxy_options);
    run.Check(!native_proxy_response && native_proxy_response.error().code() == mog::ErrorCode::UnsupportedBackend,
              "explicit native rejects unsupported proxy configuration");
}

void TestHttpsClientAndServer(TestRun &run)
{
    const auto tls = mog::TlsServerConfig::SelfSigned("localhost");
    run.Check(tls.has_value(), "generate a self-signed iOS test certificate");
    if (!tls)
    {
        return;
    }

    mog::ServerOptions server_options{"127.0.0.1", 0};
    server_options.tls = tls.value();
    mog::Server server(std::move(server_options));
    server.route(mog::Method::Get, "/secure",
                 [](const mog::ServerRequest &) { return mog::ServerResponse::Text(200, "encrypted"); });

    const auto started = server.start();
    run.Check(started.has_value(), "start loopback HTTPS server");
    if (!started)
    {
        return;
    }

    mog::Options options;
    options.verify_tls = false;
    const auto response = mog::get(Origin("https", server) + "/secure", options);
    run.Check(response.has_value(), "perform an encrypted NSURLSession loopback request");
    if (response)
    {
        run.Check(response->status_code == 200, "HTTPS response status is 200");
        run.Check(response->body == "encrypted", "HTTPS response body is preserved");
        run.Check(response->backend == "native", "HTTPS uses NSURLSession on iOS");
    }
}

int RunTests()
{
    TestRun run;
    run.Check(mog::Version() == MOG_EXPECTED_VERSION, "XCFramework exports the VERSION-derived API");
    run.Check(mog::ResolveBackend() == mog::Backend::Native, "iOS Auto resolves to the native backend");
    TestHttpClientAndServer(run);
    TestHttpsClientAndServer(run);
    return run.failures();
}

} // namespace

@interface MogTestAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation MogTestAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey, id> *)launchOptions
{
    (void)application;
    (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[UIViewController alloc] init];
    self.window.rootViewController.view.backgroundColor = UIColor.systemBackgroundColor;
    [self.window makeKeyAndVisible];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      const int failures = RunTests();
      os_log_info(TestLog(), "MOG_IOS_TEST_RESULT: %{public}d", failures);
    });
    return YES;
}

@end

int main(int argc, char *argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(MogTestAppDelegate.class));
    }
}
