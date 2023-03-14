/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
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

#define VERSION_MAJOR                   1
#define VERSION_MINOR                   0
#define VERSION_PATCH                   0
#define VERSION_BUILD               GIT_REVCOUNT

#define PRODUCT_VERSION STRINGIZE(VERSION_MAJOR.VERSION_MINOR.VERSION_PATCH)
#define PRODUCT_NAME "Wallet Setup Installer"
#define PRODUCT_COMPANY "Wiimag Inc."
#define PRODUCT_DESCRIPTION "Install the Wallet Application and keep it up-to-date."
#define PRODUCT_CODE_NAME "wallet_setup"
#define PRODUCT_COPYRIGHT "Copyright (C) 2023 - equals-forty-two.com - All rights reserved"
#define PRODUCT_WINDOWS_FILENAME "wallet_setup.exe"
