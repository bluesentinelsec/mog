/**
 * @file dynload.cpp
 * @brief SharedLibrary implementation (POSIX dlopen / Windows LoadLibrary).
 */

#include "mog/dynload.hpp"

#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace mog
{
namespace
{

std::string LastDynloadError()
{
#if defined(_WIN32)
    const DWORD code = GetLastError();
    if (code == 0)
    {
        return "unknown dynamic library error";
    }
    char *msg = nullptr;
    const DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    std::string out;
    if (n != 0 && msg != nullptr)
    {
        out.assign(msg, n);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        {
            out.pop_back();
        }
        LocalFree(msg);
    }
    else
    {
        out = "Win32 error " + std::to_string(code);
    }
    return out;
#else
    const char *err = dlerror();
    return err != nullptr ? std::string{err} : std::string{"unknown dynamic library error"};
#endif
}

} // namespace

SharedLibrary::~SharedLibrary()
{
    Close();
}

SharedLibrary::SharedLibrary(SharedLibrary &&other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), path_(std::move(other.path_))
{
}

SharedLibrary &SharedLibrary::operator=(SharedLibrary &&other) noexcept
{
    if (this != &other)
    {
        Close();
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

Result<void> SharedLibrary::Open(std::string_view path, SharedLibraryMode mode)
{
    Close();
    if (path.empty())
    {
        return Result<void>::Err(Error{ErrorCode::InvalidArgument, "shared library path is empty"});
    }

    const std::string path_str{path};

#if defined(_WIN32)
    (void)mode; // Windows LoadLibrary has no direct RTLD_LAZY/NOW equivalent.
    // LOAD_LIBRARY_SEARCH flags are optional; default search path is fine for system DLLs.
    HMODULE mod = LoadLibraryA(path_str.c_str());
    if (mod == nullptr)
    {
        return Result<void>::Err(Error{ErrorCode::DynamicLibraryError,
                                       "failed to load '" + path_str + "': " + LastDynloadError()});
    }
    handle_ = static_cast<void *>(mod);
#else
    int flags = (mode == SharedLibraryMode::Now) ? RTLD_NOW : RTLD_LAZY;
    flags |= RTLD_LOCAL;
    // Clear stale error.
    (void)dlerror();
    void *mod = dlopen(path_str.c_str(), flags);
    if (mod == nullptr)
    {
        return Result<void>::Err(Error{ErrorCode::DynamicLibraryError,
                                       "failed to load '" + path_str + "': " + LastDynloadError()});
    }
    handle_ = mod;
#endif

    path_ = path_str;
    return Result<void>::Ok();
}

void SharedLibrary::Close() noexcept
{
    if (handle_ == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
    path_.clear();
}

Result<void *> SharedLibrary::Lookup(const char *symbol_name) const
{
    if (handle_ == nullptr)
    {
        return Result<void *>::Err(
            Error{ErrorCode::DynamicLibraryError, "shared library is not open"});
    }
    if (symbol_name == nullptr || symbol_name[0] == '\0')
    {
        return Result<void *>::Err(Error{ErrorCode::InvalidArgument, "symbol name is empty"});
    }

#if defined(_WIN32)
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(handle_), symbol_name);
    if (proc == nullptr)
    {
        return Result<void *>::Err(
            Error{ErrorCode::DynamicLibraryError, "symbol '" + std::string{symbol_name} +
                                                      "' not found in '" + path_ +
                                                      "': " + LastDynloadError()});
    }
    return Result<void *>::Ok(reinterpret_cast<void *>(proc));
#else
    (void)dlerror();
    void *sym = dlsym(handle_, symbol_name);
    const char *err = dlerror();
    if (err != nullptr)
    {
        return Result<void *>::Err(
            Error{ErrorCode::DynamicLibraryError, "symbol '" + std::string{symbol_name} +
                                                      "' not found in '" + path_ +
                                                      "': " + std::string{err}});
    }
    return Result<void *>::Ok(sym);
#endif
}

Result<SharedLibrary> SharedLibrary::Load(std::string_view path, SharedLibraryMode mode)
{
    SharedLibrary lib;
    auto opened = lib.Open(path, mode);
    if (!opened)
    {
        return Result<SharedLibrary>::Err(opened.error());
    }
    return Result<SharedLibrary>::Ok(std::move(lib));
}

std::string_view SharedLibraryExtension() noexcept
{
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

} // namespace mog
