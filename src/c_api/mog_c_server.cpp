/**
 * @file mog_c_server.cpp
 * @brief C API for the embedded HTTP/S server (declared in <mog/mog_c.h>).
 *
 * The server is configured through a wrapper that buffers options, routes, and
 * static mounts, then constructs and starts a mog::Server on mog_server_start().
 * Route handlers are C callbacks wrapped in std::function; each invocation gets
 * a borrowed request view and a response builder that live only for the call.
 */
#include "mog/mog_c.h"
#include "mog/options.hpp" // mog::Method, ParseMethod
#include "mog/server.hpp"

#include <cctype>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Opaque handles
// ---------------------------------------------------------------------------

struct mog_server_request
{
    const mog::ServerRequest *req = nullptr;
};

struct mog_server_response
{
    mog::ServerResponse *resp = nullptr;
};

struct mog_server
{
    struct Route
    {
        mog::Method method;
        std::string path;
        mog_handler_fn fn;
        void *user;
    };
    struct Mount
    {
        std::string prefix;
        std::string directory;
        bool listing;
    };

    mog::ServerOptions options;
    std::vector<Route> routes;
    std::vector<Mount> mounts;
    mog_handler_fn default_fn = nullptr;
    void *default_user = nullptr;
    mog::TlsServerConfig tls;
    bool tls_set = false;
    std::unique_ptr<mog::Server> server; // constructed at start()
    std::string last_error;
};

namespace
{

const char *kEmpty = "";

// Wrap a C handler callback as a mog::Handler. The request/response views are
// stack objects valid only for the duration of the callback.
mog::Handler MakeHandler(mog_handler_fn fn, void *user)
{
    return [fn, user](const mog::ServerRequest &req) -> mog::ServerResponse {
        mog::ServerResponse resp;
        resp.status_code = 200;
        mog_server_request creq{&req};
        mog_server_response cresp{&resp};
        if (fn != nullptr)
        {
            fn(&creq, &cresp, user);
        }
        return resp;
    };
}

bool IEquals(const std::string &a, const char *b) noexcept
{
    if (b == nullptr)
    {
        return false;
    }
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle & configuration
// ---------------------------------------------------------------------------

extern "C" mog_server *mog_server_new(void)
{
    return new (std::nothrow) mog_server;
}

extern "C" void mog_server_free(mog_server *server)
{
    delete server; // ~Server stops the running server
}

extern "C" void mog_server_set_bind_address(mog_server *server, const char *address)
{
    if (server == nullptr || address == nullptr)
    {
        return;
    }
    try
    {
        server->options.bind_address = address;
    }
    catch (...)
    {
    }
}

extern "C" void mog_server_set_port(mog_server *server, unsigned short port)
{
    if (server != nullptr)
    {
        server->options.port = port;
    }
}

extern "C" void mog_server_set_threads(mog_server *server, unsigned threads)
{
    if (server != nullptr)
    {
        server->options.threads = threads;
    }
}

extern "C" int mog_server_use_self_signed_tls(mog_server *server)
{
    if (server == nullptr)
    {
        return 1;
    }
    try
    {
        auto tls = mog::TlsServerConfig::SelfSigned("localhost");
        if (!tls)
        {
            server->last_error = std::string{tls.error().message()};
            return 1;
        }
        server->tls = *tls;
        server->tls_set = true;
        return 0;
    }
    catch (...)
    {
        server->last_error = "self-signed TLS generation failed";
        return 1;
    }
}

extern "C" int mog_server_use_tls_files(mog_server *server, const char *cert_path,
                                        const char *key_path)
{
    if (server == nullptr || cert_path == nullptr || key_path == nullptr)
    {
        return 1;
    }
    try
    {
        auto tls = mog::TlsServerConfig::FromFiles(cert_path, key_path);
        if (!tls)
        {
            server->last_error = std::string{tls.error().message()};
            return 1;
        }
        server->tls = *tls;
        server->tls_set = true;
        return 0;
    }
    catch (...)
    {
        server->last_error = "loading TLS files failed";
        return 1;
    }
}

extern "C" void mog_server_serve_files(mog_server *server, const char *mount_prefix,
                                       const char *directory, int directory_listing)
{
    if (server == nullptr || directory == nullptr)
    {
        return;
    }
    try
    {
        server->mounts.push_back(mog_server::Mount{mount_prefix != nullptr ? mount_prefix : "/",
                                                   directory, directory_listing != 0});
    }
    catch (...)
    {
    }
}

extern "C" int mog_server_route(mog_server *server, const char *method, const char *path,
                                mog_handler_fn handler, void *userdata)
{
    if (server == nullptr || method == nullptr || path == nullptr || handler == nullptr)
    {
        return 1;
    }
    auto parsed = mog::ParseMethod(method);
    if (!parsed)
    {
        server->last_error = std::string{"invalid HTTP method: "} + method;
        return 1;
    }
    try
    {
        server->routes.push_back(mog_server::Route{*parsed, path, handler, userdata});
        return 0;
    }
    catch (...)
    {
        return 1;
    }
}

extern "C" void mog_server_set_default_handler(mog_server *server, mog_handler_fn handler,
                                               void *userdata)
{
    if (server != nullptr)
    {
        server->default_fn = handler;
        server->default_user = userdata;
    }
}

extern "C" int mog_server_start(mog_server *server)
{
    if (server == nullptr)
    {
        return 1;
    }
    if (server->server)
    {
        server->last_error = "server already started";
        return 1;
    }
    try
    {
        if (server->tls_set)
        {
            server->options.tls = server->tls;
        }
        auto instance = std::make_unique<mog::Server>(server->options);
        for (const auto &route : server->routes)
        {
            instance->route(route.method, route.path, MakeHandler(route.fn, route.user));
        }
        for (const auto &mount : server->mounts)
        {
            mog::StaticOptions opt;
            opt.directory_listing = mount.listing;
            instance->serve_files(mount.prefix, mount.directory, opt);
        }
        if (server->default_fn != nullptr)
        {
            instance->set_default_handler(MakeHandler(server->default_fn, server->default_user));
        }
        auto started = instance->start();
        if (!started)
        {
            server->last_error = std::string{started.error().message()};
            return 1;
        }
        server->server = std::move(instance);
        return 0;
    }
    catch (const std::exception &ex)
    {
        server->last_error = ex.what();
        return 1;
    }
    catch (...)
    {
        server->last_error = "unknown error starting server";
        return 1;
    }
}

extern "C" unsigned short mog_server_port(const mog_server *server)
{
    return (server != nullptr && server->server) ? server->server->port() : 0;
}

extern "C" int mog_server_is_running(const mog_server *server)
{
    return (server != nullptr && server->server && server->server->running()) ? 1 : 0;
}

extern "C" void mog_server_stop(mog_server *server)
{
    if (server != nullptr && server->server)
    {
        server->server->stop();
    }
}

extern "C" void mog_server_wait(mog_server *server)
{
    if (server != nullptr && server->server)
    {
        server->server->wait();
    }
}

extern "C" const char *mog_server_last_error(const mog_server *server)
{
    return server != nullptr ? server->last_error.c_str() : kEmpty;
}

// ---------------------------------------------------------------------------
// Request accessors
// ---------------------------------------------------------------------------

extern "C" const char *mog_server_request_method(const mog_server_request *req)
{
    return (req != nullptr && req->req != nullptr) ? req->req->method_text.c_str() : kEmpty;
}

extern "C" const char *mog_server_request_path(const mog_server_request *req)
{
    return (req != nullptr && req->req != nullptr) ? req->req->path.c_str() : kEmpty;
}

extern "C" const char *mog_server_request_target(const mog_server_request *req)
{
    return (req != nullptr && req->req != nullptr) ? req->req->target.c_str() : kEmpty;
}

extern "C" const char *mog_server_request_client_address(const mog_server_request *req)
{
    return (req != nullptr && req->req != nullptr) ? req->req->client_address.c_str() : kEmpty;
}

extern "C" const char *mog_server_request_query(const mog_server_request *req, const char *name)
{
    if (req == nullptr || req->req == nullptr || name == nullptr)
    {
        return kEmpty;
    }
    auto it = req->req->params.find(name);
    return it != req->req->params.end() ? it->second.c_str() : kEmpty;
}

extern "C" const char *mog_server_request_header(const mog_server_request *req, const char *name)
{
    if (req == nullptr || req->req == nullptr || name == nullptr)
    {
        return kEmpty;
    }
    for (const auto &h : req->req->headers)
    {
        if (IEquals(h.name, name))
        {
            return h.value.c_str();
        }
    }
    return kEmpty;
}

extern "C" const char *mog_server_request_body(const mog_server_request *req, size_t *len_out)
{
    if (req == nullptr || req->req == nullptr)
    {
        if (len_out != nullptr)
        {
            *len_out = 0;
        }
        return kEmpty;
    }
    if (len_out != nullptr)
    {
        *len_out = req->req->body.size();
    }
    return req->req->body.c_str();
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

extern "C" void mog_server_response_set_status(mog_server_response *resp, int status)
{
    if (resp != nullptr && resp->resp != nullptr)
    {
        resp->resp->status_code = status;
    }
}

extern "C" void mog_server_response_set_header(mog_server_response *resp, const char *name,
                                               const char *value)
{
    if (resp == nullptr || resp->resp == nullptr || name == nullptr)
    {
        return;
    }
    try
    {
        resp->resp->set_header(name, value != nullptr ? value : "");
    }
    catch (...)
    {
    }
}

extern "C" void mog_server_response_set_body(mog_server_response *resp, const void *data,
                                             size_t len)
{
    if (resp == nullptr || resp->resp == nullptr)
    {
        return;
    }
    try
    {
        resp->resp->body.assign(data != nullptr ? static_cast<const char *>(data) : "",
                                data != nullptr ? len : 0);
    }
    catch (...)
    {
    }
}
