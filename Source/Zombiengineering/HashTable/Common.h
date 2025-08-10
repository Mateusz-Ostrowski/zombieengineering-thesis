// Copyright Epic Games, Inc. All Rights Reserved.
// Base Declarations (Windows-only, trimmed)

#pragma once

#include <stdlib.h>   // size_t, malloc/free prototypes, etc.
#include <stdint.h>   // fixed-size integer types
#include <new>        // placement new

// ------------------------------------------------------------------
// Platform (Windows only)

#ifndef ULANG_PLATFORM_WINDOWS
#define ULANG_PLATFORM_WINDOWS 1
#endif

#ifndef ULANG_PLATFORM
#define ULANG_PLATFORM ULANG_PLATFORM_WINDOWS
#endif

// ------------------------------------------------------------------
/// Prevent API mismatch in dynamic linking situations
#define ULANG_API_VERSION 2

// ------------------------------------------------------------------
// DLL API (MSVC)

#ifndef ULANG_DLLIMPORT
  #define ULANG_DLLIMPORT __declspec(dllimport)
#endif
#ifndef ULANG_DLLEXPORT
  #define ULANG_DLLEXPORT __declspec(dllexport)
#endif
#ifndef ULANG_DLLIMPORT_VTABLE
  #define ULANG_DLLIMPORT_VTABLE
#endif
#ifndef ULANG_DLLEXPORT_VTABLE
  #define ULANG_DLLEXPORT_VTABLE
#endif

#ifndef DLLIMPORT
  #define DLLIMPORT ULANG_DLLIMPORT
  #define DLLEXPORT ULANG_DLLEXPORT
#endif
#ifndef DLLIMPORT_VTABLE
  #define DLLIMPORT_VTABLE ULANG_DLLIMPORT_VTABLE
  #define DLLEXPORT_VTABLE ULANG_DLLEXPORT_VTABLE
#endif

// ------------------------------------------------------------------
// MSVC warnings and 3rd-party include guards

#ifdef _MSC_VER
  #pragma warning(disable: 4100) // unreferenced formal parameter
  #pragma warning(disable: 4127) // conditional expression is constant
  #pragma warning(disable: 4251) // needs dll-interface
  #pragma warning(disable: 4275) // non-DLL-interface base used for DLL-interface class
  #pragma warning(disable: 6255) // _alloca indicates failure by raising a stack overflow
  #pragma warning(disable: 6326) // potential comparison of a constant with another constant

  // Disable warnings commonly triggered by third-party headers
  #define ULANG_THIRD_PARTY_INCLUDES_START \
      __pragma(warning(push)) \
      __pragma(warning(disable: 4141 4189 4244 4245 4267 4324 4458 4624 4668 6313 4389 5054))
  #define ULANG_THIRD_PARTY_INCLUDES_END \
      __pragma(warning(pop))
#else
  #define ULANG_THIRD_PARTY_INCLUDES_START
  #define ULANG_THIRD_PARTY_INCLUDES_END
#endif

// ------------------------------------------------------------------
// Alignment (MSVC)

#ifdef _MSC_VER
  #define ULANG_MS_ALIGN(n) __declspec(align(n))
#else
  #define ULANG_MS_ALIGN(n)
#endif

// ------------------------------------------------------------------
// Inlining (MSVC-first, with safe fallbacks)

#if defined(_MSC_VER)
  #define ULANG_FORCEINLINE __forceinline
  #define ULANG_FORCENOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
  #define ULANG_FORCEINLINE inline __attribute__((always_inline))
  #define ULANG_FORCENOINLINE __attribute__((noinline))
#else
  #define ULANG_FORCEINLINE inline
  #define ULANG_FORCENOINLINE
#endif

// ------------------------------------------------------------------
// Restrict

#define ULANG_RESTRICT __restrict

// ------------------------------------------------------------------
// Likely/Unlikely (fallbacks on MSVC)

#define ULANG_LIKELY(x)   (x)
#define ULANG_UNLIKELY(x) (x)

// ------------------------------------------------------------------
// Array count

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
  #define ULANG_COUNTOF(x) _countof(x)
#else
  #define ULANG_COUNTOF(x) (sizeof(x)/sizeof(x[0]))
#endif

// ------------------------------------------------------------------
// Switch case fall-through (C++17 and up)

#if __cplusplus >= 201703L
  #define ULANG_FALLTHROUGH [[fallthrough]]
#else
  #define ULANG_FALLTHROUGH
#endif

// ------------------------------------------------------------------
// Mark a code path that is unreachable.

#ifndef ULANG_BREAK
  // Provide a reasonable default for Windows builds; projects can override.
  #if defined(_MSC_VER)
    #define ULANG_BREAK() __debugbreak()
  #else
    #define ULANG_BREAK() ((void)0)
  #endif
#endif

#define ULANG_UNREACHABLE() while(true){ ULANG_BREAK(); }

// ------------------------------------------------------------------
// Static analysis helpers (no-ops on MSVC by default)

#define ULANG_CA_ASSUME(Expr) ((void)(Expr))

// ------------------------------------------------------------------
// Asserts & Logging toggles

#ifndef ULANG_DO_CHECK
  #define ULANG_DO_CHECK 0
#endif

// ------------------------------------------------------------------
// Logging helpers

#define VERSE_SUPPRESS_UNUSED(_Variable) (void)(_Variable)

#define USING_ELogVerbosity \
    constexpr TestHashTable::ELogVerbosity Error   = TestHashTable::ELogVerbosity::Error;   VERSE_SUPPRESS_UNUSED(Error) \
    constexpr TestHashTable::ELogVerbosity Warning = TestHashTable::ELogVerbosity::Warning; VERSE_SUPPRESS_UNUSED(Warning) \
    constexpr TestHashTable::ELogVerbosity Display = TestHashTable::ELogVerbosity::Display; VERSE_SUPPRESS_UNUSED(Display) \
    constexpr TestHashTable::ELogVerbosity Log     = TestHashTable::ELogVerbosity::Log;     VERSE_SUPPRESS_UNUSED(Log) \
    constexpr TestHashTable::ELogVerbosity Verbose = TestHashTable::ELogVerbosity::Verbose; VERSE_SUPPRESS_UNUSED(Verbose)

#define ULANG_LOGF(verbosity, format, ...) \
    { USING_ELogVerbosity; (*::TestHashTable::GetSystemParams()._LogMessage)(verbosity, format, ##__VA_ARGS__); }

// ------------------------------------------------------------------
// Memory tuning

#ifndef ULANG_AGGRESSIVE_MEMORY_SAVING
  #define ULANG_AGGRESSIVE_MEMORY_SAVING 0
#endif

// ------------------------------------------------------------------
// TestHashTable namespace basics

namespace TestHashTable
{

// Type of nullptr
using NullPtrType = std::nullptr_t;

// Language defaults
using Integer = int64_t;
using Float   = double;
using Boolean = bool;

inline const uint32_t uint32_invalid = UINT32_MAX;

/// Visitor results
enum class EVisitResult : int8_t
{
    Continue     = 0,
    SkipChildren = 1,
    Stop         = 2
};

/// Iterate results
enum class EIterateResult : int8_t
{
    Stopped   = 0,
    Completed = 1
};

/// Generic results
enum class EResult : int8_t
{
    Unspecified = -1,
    OK = 0,
    Error
};

/// After-error action
enum class EErrorAction : int8_t
{
    Continue = 0,
    Break
};

/// Compare results
enum class EEquate : int8_t
{
    Less = -1,
    Equal = 0,
    Greater = 1
};

/// Initialization markers
enum ENoInit     { NoInit };
enum EDefaultInit{ DefaultInit };

/// Unspecified index
enum EIndex { IndexNone = -1 };

// ------------------------------------------------------------------
// System Initialization

enum class EAssertSeverity : int8_t
{
    Fatal = 0,
    Recoverable
};

enum class ELogVerbosity : int8_t
{
    Error,
    Warning,
    Display,
    Verbose,
    Log
};

struct SSystemParams
{
    using FMalloc  = void * (*)(size_t);
    using FRealloc = void * (*)(void *, size_t);
    using FFree    = void   (*)(void *);

    using FAssert = EErrorAction(*)(EAssertSeverity /*Severity*/, const char* /*Expr*/, const char* /*File*/, int32_t /*Line*/, const char* /*Format*/, ...);
    using FLog    = void(*)(ELogVerbosity /*Verbosity*/, const char* /*Format*/, ...);

    int      _APIVersion;   // set to ULANG_API_VERSION

    FMalloc  _HeapMalloc;
    FRealloc _HeapRealloc;
    FFree    _HeapFree;

    FAssert  _AssertFailed;
    FLog     _LogMessage;

    ELogVerbosity _Verbosity = ELogVerbosity::Display;

    SSystemParams(int APIVersion, FMalloc HeapMalloc, FRealloc HeapRealloc, FFree HeapFree, FAssert AssertFailed, FLog LogMessage = nullptr)
        : _APIVersion(APIVersion)
        , _HeapMalloc(HeapMalloc)
        , _HeapRealloc(HeapRealloc)
        , _HeapFree(HeapFree)
        , _AssertFailed(AssertFailed)
        , _LogMessage(LogMessage)
    {}
};

ZOMBIENGINEERING_API bool operator==(const SSystemParams& Lhs, const SSystemParams& Rhs);
ZOMBIENGINEERING_API SSystemParams& GetSystemParams();

// ------------------------------------------------------------------
// Module lifecycle

class CAllocatorInstance;
extern ZOMBIENGINEERING_API const CAllocatorInstance GSystemAllocatorInstance;

ZOMBIENGINEERING_API EResult Initialize(const SSystemParams& Params);
ZOMBIENGINEERING_API bool   IsInitialized();
ZOMBIENGINEERING_API EResult DeInitialize();
ZOMBIENGINEERING_API void   SetGlobalVerbosity(const TestHashTable::ELogVerbosity GlobalVerbosity);

} // namespace TestHashTable

// ------------------------------------------------------------------
// Asserts (depend on GetSystemParams)

#if ULANG_DO_CHECK
  #define ULANG_ASSERT(expr) { if (ULANG_UNLIKELY(!(bool)(expr))) { if ((*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Fatal, #expr, __FILE__, __LINE__, "") == ::TestHashTable::EErrorAction::Break) { ULANG_BREAK(); } (void)0; } }
  #define ULANG_VERIFY(expr) { if (ULANG_UNLIKELY(!(bool)(expr)) && (*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, "") == ::TestHashTable::EErrorAction::Break) ULANG_BREAK(); }
  #define ULANG_ENSURE(expr) ( (bool)(expr) || ( ((*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, "") == ::TestHashTable::EErrorAction::Break) && ([]()->bool{ ULANG_BREAK(); return false; }()) ) )
  #define ULANG_ERRORF(format, ...)        { if ((*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Fatal, "", __FILE__, __LINE__, format, ##__VA_ARGS__) == ::TestHashTable::EErrorAction::Break) ULANG_BREAK(); }
  #define ULANG_ASSERTF(expr, format, ...) { if (ULANG_UNLIKELY(!(bool)(expr))) { if ((*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Fatal, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::TestHashTable::EErrorAction::Break) { ULANG_BREAK(); } (void)0; } }
  #define ULANG_VERIFYF(expr, format, ...) { if (ULANG_UNLIKELY(!(bool)(expr)) && (*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::TestHashTable::EErrorAction::Break) ULANG_BREAK(); }
  #define ULANG_ENSUREF(expr, format, ...) ( (bool)(expr) || ( ((*::TestHashTable::GetSystemParams()._AssertFailed)(::TestHashTable::EAssertSeverity::Recoverable, #expr, __FILE__, __LINE__, format, ##__VA_ARGS__) == ::TestHashTable::EErrorAction::Break) && ([]()->bool{ ULANG_BREAK(); return false; }()) ) )
#else
  #define ULANG_ASSERT(expr)               (void(sizeof(!!(expr))))
  #define ULANG_VERIFY(expr)               (void(sizeof(!!(expr))))
  #define ULANG_ENSURE(expr)               (!!(expr))
  #define ULANG_ERRORF(format, ...)        (void(0))
  #define ULANG_ASSERTF(expr, format, ...) (void(sizeof(!!(expr))))
  #define ULANG_VERIFYF(expr, format, ...) (void(sizeof(!!(expr))))
  #define ULANG_ENSUREF(expr, format, ...) (!!(expr))
#endif
