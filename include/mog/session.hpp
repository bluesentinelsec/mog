/**
 * @file session.hpp
 * @brief Thread-safe Session with shared default headers/options.
 */
#pragma once

#include "mog/http.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"

#include <mutex>
#include <string>
#include <string_view>

namespace mog
{

/**
 * @brief Reusable client with default headers and options.
 *
 * All public methods are safe to call concurrently from multiple threads.
 * Each request uses a snapshot of session state taken under the lock.
 */
class Session
{
  public:
    Session();
    explicit Session(Options defaults);

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Session(Session &&) = delete;
    Session &operator=(Session &&) = delete;

    /**
     * @brief Replace default options (headers, timeout, backend, …).
     */
    void set_defaults(Options defaults);

    /**
     * @return Copy of current default options.
     */
    [[nodiscard]] Options defaults() const;

    /**
     * @brief Set or replace a default header.
     */
    void set_header(std::string name, std::string value);

    /**
     * @brief Optional base URL prepended to relative request paths.
     */
    void set_base_url(std::string base_url);

    [[nodiscard]] std::string base_url() const;

    [[nodiscard]] Result<Response> request(Method method, std::string_view url,
                                           const Options &options = {});

    [[nodiscard]] Result<Response> get(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> post(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> put(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> patch(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> del(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> head(std::string_view url, const Options &options = {});
    [[nodiscard]] Result<Response> options(std::string_view url, const Options &options = {});

  private:
    [[nodiscard]] Options merge_options(const Options &per_request) const;
    [[nodiscard]] std::string resolve_url(std::string_view url) const;

    mutable std::mutex mutex_;
    Options defaults_;
    std::string base_url_;
};

} // namespace mog
