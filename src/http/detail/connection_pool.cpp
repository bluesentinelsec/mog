/**
 * @file connection_pool.cpp
 * @brief Idle stream pool implementation.
 */

#include "http/detail/connection_pool.hpp"

#include <cctype>
#include <functional>
#include <sstream>

namespace mog::detail
{
namespace
{

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

bool HeaderTokenHas(std::string_view value, std::string_view token) noexcept
{
    // Simple case-insensitive substring match on comma-separated tokens.
    std::string lower;
    lower.reserve(value.size());
    for (char ch : value)
    {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    std::string needle;
    for (char ch : token)
    {
        needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower.find(needle) != std::string::npos;
}

} // namespace

std::string ConnectionKey::ToString() const
{
    std::ostringstream oss;
    oss << scheme << "://" << host << ':' << port;
    if (!proxy.empty())
    {
        oss << "|proxy=" << proxy;
    }
    return oss.str();
}

bool ConnectionKey::operator==(const ConnectionKey &other) const noexcept
{
    return scheme == other.scheme && host == other.host && port == other.port &&
           proxy == other.proxy;
}

std::size_t ConnectionKeyHash::operator()(const ConnectionKey &k) const noexcept
{
    // Combine hashes of fields (not cryptographic).
    std::size_t h = std::hash<std::string>{}(k.scheme);
    h ^= std::hash<std::string>{}(k.host) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::uint16_t>{}(k.port) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(k.proxy) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

std::unique_ptr<Stream> ConnectionPool::Take(const ConnectionKey &key)
{
    std::lock_guard lock(mutex_);
    auto it = idle_.find(key);
    if (it == idle_.end())
    {
        return nullptr;
    }
    auto stream = std::move(it->second);
    idle_.erase(it);
    return stream;
}

void ConnectionPool::Put(const ConnectionKey &key, std::unique_ptr<Stream> stream)
{
    if (!stream)
    {
        return;
    }
    std::lock_guard lock(mutex_);
    idle_[key] = std::move(stream);
}

void ConnectionPool::Drop(const ConnectionKey &key)
{
    std::lock_guard lock(mutex_);
    idle_.erase(key);
}

std::size_t ConnectionPool::idle_count() const
{
    std::lock_guard lock(mutex_);
    return idle_.size();
}

ConnectionKey MakeConnectionKey(const Url &url, const std::optional<std::string> &proxy)
{
    ConnectionKey key;
    key.scheme = url.scheme;
    key.host = url.host;
    key.port = url.port;
    if (proxy.has_value())
    {
        key.proxy = *proxy;
    }
    return key;
}

bool ResponseAllowsKeepAlive(std::string_view version_token,
                             const std::vector<Header> &headers) noexcept
{
    bool has_close = false;
    bool has_keep_alive = false;
    for (const auto &h : headers)
    {
        if (!EqualsIgnoreCase(h.name, "Connection"))
        {
            continue;
        }
        if (HeaderTokenHas(h.value, "close"))
        {
            has_close = true;
        }
        if (HeaderTokenHas(h.value, "keep-alive"))
        {
            has_keep_alive = true;
        }
    }
    if (has_close)
    {
        return false;
    }
    // HTTP/1.1 defaults to persistent unless Connection: close.
    if (version_token.size() >= 8 && version_token.substr(0, 8) == "HTTP/1.1")
    {
        return true;
    }
    // HTTP/1.0 (and unknown): only if Connection: keep-alive.
    return has_keep_alive;
}

} // namespace mog::detail
