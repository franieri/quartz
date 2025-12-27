#pragma once

// Cross-platform symbol visibility / DLL import-export.
//
// - QZ_CORE_API: symbols exported by the qz-core shared library.
// - QZ_EXTENSION_API: symbols exported by extension shared libraries (init_extension).

#if defined(_WIN32)
    #if defined(QZ_CORE_BUILD)
        #define QZ_CORE_API __declspec(dllexport)
    #else
        #define QZ_CORE_API __declspec(dllimport)
    #endif

    #if defined(QZ_EXTENSION_BUILD)
        #define QZ_EXTENSION_API __declspec(dllexport)
    #else
        #define QZ_EXTENSION_API
    #endif
#else
    #if defined(__GNUC__) || defined(__clang__)
        #define QZ_CORE_API __attribute__((visibility("default")))
        #define QZ_EXTENSION_API __attribute__((visibility("default")))
    #else
        #define QZ_CORE_API
        #define QZ_EXTENSION_API
    #endif
#endif
