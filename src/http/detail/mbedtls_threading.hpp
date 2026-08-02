/**
 * @file mbedtls_threading.hpp
 * @brief Ensure mbedTLS threading support is initialized before concurrent TLS.
 */
#pragma once

namespace mog::detail
{

/**
 * @brief Initialize mbedTLS threading (idempotent, thread-safe).
 *
 * On Windows this registers Win32 CRITICAL_SECTION mutex callbacks via
 * mbedtls_threading_set_alt() on first call. On POSIX it is a no-op because
 * mbedTLS uses its built-in pthread mutexes (MBEDTLS_THREADING_PTHREAD). Call
 * this before any TLS handshake so mbedTLS's global state (including the PSA
 * subsystem used by TLS 1.3) is protected for concurrent use.
 */
void EnsureMbedtlsThreading();

} // namespace mog::detail
