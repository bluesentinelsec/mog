/**
 * @file mbedtls_threading.cpp
 * @brief mbedTLS threading initialization (Win32 alt mutexes; no-op on POSIX).
 */
#include "http/detail/mbedtls_threading.hpp"

#include <mutex>

#if defined(_WIN32)
#include <mbedtls/threading.h>

namespace mog::detail
{
namespace
{

void MutexInit(mbedtls_threading_mutex_t *mutex)
{
    if (mutex != nullptr)
    {
        InitializeCriticalSection(&mutex->cs);
        mutex->is_valid = 1;
    }
}

void MutexFree(mbedtls_threading_mutex_t *mutex)
{
    if (mutex != nullptr && mutex->is_valid != 0)
    {
        DeleteCriticalSection(&mutex->cs);
        mutex->is_valid = 0;
    }
}

int MutexLock(mbedtls_threading_mutex_t *mutex)
{
    if (mutex == nullptr || mutex->is_valid == 0)
    {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    EnterCriticalSection(&mutex->cs);
    return 0;
}

int MutexUnlock(mbedtls_threading_mutex_t *mutex)
{
    if (mutex == nullptr || mutex->is_valid == 0)
    {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    LeaveCriticalSection(&mutex->cs);
    return 0;
}

} // namespace

void EnsureMbedtlsThreading()
{
    static std::once_flag once;
    std::call_once(once,
                   [] { mbedtls_threading_set_alt(MutexInit, MutexFree, MutexLock, MutexUnlock); });
}

} // namespace mog::detail

#else // POSIX: mbedTLS uses built-in pthread mutexes; nothing to register.

namespace mog::detail
{
void EnsureMbedtlsThreading()
{
}
} // namespace mog::detail

#endif
