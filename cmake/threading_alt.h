/*
 * mbedTLS alternate mutex type (used on Windows via MBEDTLS_THREADING_ALT).
 * mbedTLS includes this header to define mbedtls_threading_mutex_t. The mutex
 * callbacks are supplied by mog at runtime (see src/http/detail/mbedtls_threading).
 * On POSIX, MBEDTLS_THREADING_PTHREAD is used instead and this type is unused.
 */
#ifndef MOG_MBEDTLS_THREADING_ALT_H
#define MOG_MBEDTLS_THREADING_ALT_H

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct mbedtls_threading_mutex_t
{
    CRITICAL_SECTION cs;
    int is_valid;
} mbedtls_threading_mutex_t;

#endif /* _WIN32 */

#endif /* MOG_MBEDTLS_THREADING_ALT_H */
