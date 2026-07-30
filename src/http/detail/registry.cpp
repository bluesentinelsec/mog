/**
 * @file registry.cpp
 * @brief Transport backend registry (open for extension).
 */

#include "http/detail/curl_backend.hpp"
#include "http/detail/embedded_backend.hpp"
#include "http/detail/native_backend.hpp"
#include "http/detail/transport.hpp"

#include <array>
#include <mutex>

namespace mog::detail
{
namespace
{

constexpr std::size_t kSlotCount = 5; // Auto unused; Embedded..Native

struct Registry
{
    std::mutex mu;
    std::array<std::unique_ptr<Transport>, kSlotCount> slots{};
    bool defaults_loaded = false;
};

Registry &GetRegistry()
{
    static Registry reg;
    return reg;
}

std::size_t SlotIndex(Backend id) noexcept
{
    switch (id)
    {
    case Backend::Embedded:
        return 0;
    case Backend::Curl:
        return 1;
    case Backend::WinHttp:
        return 2;
    case Backend::Native:
        return 3;
    case Backend::Auto:
        return 4;
    }
    return 4;
}

class EmbeddedTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "embedded";
    }

    [[nodiscard]] Result<Response> Execute(Method method, std::string_view url,
                                           const Options &options) override
    {
        return EmbeddedRequest(method, url, options);
    }
};

class UnimplementedTransport final : public Transport
{
  public:
    explicit UnimplementedTransport(std::string_view name) : name_(name)
    {
    }

    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return name_;
    }

    // A placeholder is never a usable backend, so Auto skips it.
    [[nodiscard]] bool Available() const noexcept override
    {
        return false;
    }

    [[nodiscard]] Result<Response> Execute(Method, std::string_view, const Options &) override
    {
        return Result<Response>::Err(Error{ErrorCode::UnsupportedBackend,
                                           std::string("backend '") + std::string{name_} +
                                               "' is not implemented yet; use embedded (default)"});
    }

  private:
    std::string_view name_;
};

} // namespace

void RegisterTransport(Backend id, std::unique_ptr<Transport> transport)
{
    auto &reg = GetRegistry();
    std::lock_guard lock(reg.mu);
    reg.slots[SlotIndex(id)] = std::move(transport);
}

Transport *FindTransport(Backend id) noexcept
{
    EnsureDefaultTransportsRegistered();
    auto &reg = GetRegistry();
    std::lock_guard lock(reg.mu);
    return reg.slots[SlotIndex(id)].get();
}

void EnsureDefaultTransportsRegistered()
{
    auto &reg = GetRegistry();
    std::lock_guard lock(reg.mu);
    if (reg.defaults_loaded)
    {
        return;
    }
    // Only fill empty slots so RegisterTransport() before first use is honored (OCP).
    auto &embedded = reg.slots[SlotIndex(Backend::Embedded)];
    if (!embedded)
    {
        embedded = std::make_unique<EmbeddedTransport>();
    }
    auto &curl = reg.slots[SlotIndex(Backend::Curl)];
    if (!curl)
    {
        curl = MakeCurlTransport(); // libcurl via dlopen where available
        if (!curl)
        {
            curl = std::make_unique<UnimplementedTransport>("curl");
        }
    }
    auto &win = reg.slots[SlotIndex(Backend::WinHttp)];
    if (!win)
    {
        win = std::make_unique<UnimplementedTransport>("winhttp");
    }
    auto &native = reg.slots[SlotIndex(Backend::Native)];
    if (!native)
    {
        native = MakeNativeTransport(); // platform-native (e.g. NSURLSession on macOS)
        if (!native)
        {
            native = std::make_unique<UnimplementedTransport>("native");
        }
    }
    reg.defaults_loaded = true;
}

Backend PreferredNativeBackend() noexcept
{
#if defined(__APPLE__)
    return Backend::Native;
#elif defined(_WIN32)
    return Backend::WinHttp;
#elif defined(__linux__)
    return Backend::Curl;
#else
    return Backend::Embedded;
#endif
}

bool IsBackendAvailable(Backend id)
{
    Transport *transport = FindTransport(id); // ensures defaults + locks internally
    return transport != nullptr && transport->Available();
}

Backend ResolveAutoBackend()
{
    const Backend native = PreferredNativeBackend();
    if (native != Backend::Embedded)
    {
        Transport *transport = FindTransport(native);
        // Auto prefers a native backend only once it opts in (feature parity),
        // not merely because it is available for explicit selection.
        if (transport != nullptr && transport->Available() && transport->AutoPreferred())
        {
            return native;
        }
    }
    return Backend::Embedded;
}

} // namespace mog::detail
