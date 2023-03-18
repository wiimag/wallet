/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

/*
 * Turn A into a string literal without expanding macro definitions
 * (however, if invoked from a macro, macro arguments are expanded).
 */
#define STRINGIZE_NX(A) #A

/*
 * Turn A into a string literal after macro-expanding it.
 */
#define STRINGIZE(A) STRINGIZE_NX(A)

#if !defined(GIT_BRANCH) && !defined(GIT_COMMIT) && !defined(GIT_REVCOUNT) && !defined(GIT_SHORT_HASH)
#pragma warning(disable: 4067)
#ifdef __has_include
    #if __has_include("../artifacts/version.git.h")
        #include "../artifacts/version.git.h"
    #else
        #define GIT_BRANCH "-"
        #define GIT_COMMIT "-"
        #define GIT_REVCOUNT 0
        #define GIT_SHORT_HASH "-"
    #endif
#else
    #include "../artifacts/version.git.h"
#endif
#pragma warning(default: 4067)
#endif

#if !defined(GIT_AVAILABLE)
#define GIT_AVAILABLE 0
#endif

#if !defined(GIT_BRANCH)
    #define GIT_BRANCH "-"
#endif

#if !defined(GIT_COMMIT)
    #define GIT_COMMIT "-"
#endif

#if !defined(GIT_SHORT_HASH)
    #define GIT_SHORT_HASH "-"
#endif

#if !defined(GIT_REVCOUNT)
    #define GIT_REVCOUNT 0
#endif

#define VERSION_MAJOR                   0
#define VERSION_MINOR                   9
#define VERSION_PATCH                   22
#define VERSION_BUILD                   GIT_REVCOUNT

#define PRODUCT_VERSION STRINGIZE(VERSION_MAJOR.VERSION_MINOR.VERSION_PATCH)
#define PRODUCT_NAME "Wallet"
#define PRODUCT_COMPANY "Wiimag Inc."
#define PRODUCT_DESCRIPTION "Wallet - Finance 300K"
#define PRODUCT_CODE_NAME "Wallet"
#define PRODUCT_COPYRIGHT "Copyright (C) 2022-2023 - equals-forty-two.com - All rights reserved"
#define PRODUCT_WINDOWS_FILENAME "wallet.exe"
