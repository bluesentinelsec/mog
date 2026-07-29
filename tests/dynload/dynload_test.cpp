/**
 * @file dynload_test.cpp
 * @brief Unit tests for portable SharedLibrary (dlopen / LoadLibrary).
 */

#include "mog/dynload.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
using GetCurrentProcessId_t = unsigned long(__stdcall *)();
#else
using Strlen_t = std::size_t (*)(const char *);
#endif

TEST(Dynload, ExtensionIsPlatformSpecific)
{
    const auto ext = mog::SharedLibraryExtension();
#if defined(_WIN32)
    EXPECT_EQ(ext, ".dll");
#elif defined(__APPLE__)
    EXPECT_EQ(ext, ".dylib");
#else
    EXPECT_EQ(ext, ".so");
#endif
}

TEST(Dynload, OpenMissingLibraryFails)
{
    mog::SharedLibrary lib;
    auto r = lib.Open("mog_definitely_does_not_exist_xyzzy_12345");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::DynamicLibraryError);
    EXPECT_FALSE(lib.is_open());
}

TEST(Dynload, LoadSystemLibraryAndResolveSymbol)
{
#if defined(_WIN32)
    auto lib = mog::SharedLibrary::Load("kernel32.dll");
    ASSERT_TRUE(lib) << lib.error().to_string();
    auto fn = lib->Symbol<GetCurrentProcessId_t>("GetCurrentProcessId");
    ASSERT_TRUE(fn) << fn.error().to_string();
    EXPECT_NE((*fn)(), 0u);
#else
    // libc is always present; symbol names may be versioned on Linux — try common ones.
    const char *candidates[] = {
#if defined(__APPLE__)
        "/usr/lib/libSystem.B.dylib",
        "libSystem.B.dylib",
#else
        "libc.so.6",
        "libc.so",
#endif
    };
    std::optional<mog::SharedLibrary> loaded;
    std::string last_err;
    for (const char *path : candidates)
    {
        auto lib = mog::SharedLibrary::Load(path);
        if (lib)
        {
            loaded = std::move(*lib);
            break;
        }
        last_err = lib.error().to_string();
    }
    ASSERT_TRUE(loaded.has_value()) << last_err;
    auto strlen_fn = loaded->Symbol<Strlen_t>("strlen");
    ASSERT_TRUE(strlen_fn) << strlen_fn.error().to_string();
    EXPECT_EQ((*strlen_fn)("mog"), 3u);
#endif
}

TEST(Dynload, LookupMissingSymbolFails)
{
#if defined(_WIN32)
    auto lib = mog::SharedLibrary::Load("kernel32.dll");
#else
#if defined(__APPLE__)
    auto lib = mog::SharedLibrary::Load("/usr/lib/libSystem.B.dylib");
#else
    auto lib = mog::SharedLibrary::Load("libc.so.6");
#endif
#endif
    ASSERT_TRUE(lib) << lib.error().to_string();
    auto missing = lib->Lookup("mog_no_such_symbol_ever_42");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), mog::ErrorCode::DynamicLibraryError);
}

TEST(Dynload, MoveTransfersOwnership)
{
#if defined(_WIN32)
    auto lib = mog::SharedLibrary::Load("kernel32.dll");
#else
#if defined(__APPLE__)
    auto lib = mog::SharedLibrary::Load("/usr/lib/libSystem.B.dylib");
#else
    auto lib = mog::SharedLibrary::Load("libc.so.6");
#endif
#endif
    ASSERT_TRUE(lib);
    mog::SharedLibrary moved = std::move(*lib);
    EXPECT_TRUE(moved.is_open());
    EXPECT_FALSE(lib->is_open());
    moved.Close();
    EXPECT_FALSE(moved.is_open());
}
