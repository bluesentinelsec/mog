/*
 * mog's mbedTLS user config (appended to mbedtls_config.h via
 * MBEDTLS_USER_CONFIG_FILE). It enables threading so mbedTLS's global state
 * (notably the PSA crypto subsystem used by TLS 1.3) is mutex-protected, which
 * is required for concurrent TLS handshakes across mog's server worker threads
 * and concurrent client requests.
 */
#ifndef MOG_MBEDTLS_USER_CONFIG_H
#define MOG_MBEDTLS_USER_CONFIG_H

#define MBEDTLS_THREADING_C
#if defined(_WIN32)
/* Windows uses an alternate mutex implementation supplied by mog (Win32
 * CRITICAL_SECTION); see src/http/detail/mbedtls_threading_win. */
#define MBEDTLS_THREADING_ALT
#else
/* POSIX platforms use mbedTLS's built-in pthread mutexes. */
#define MBEDTLS_THREADING_PTHREAD
#endif

#endif /* MOG_MBEDTLS_USER_CONFIG_H */
