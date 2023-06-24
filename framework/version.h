/*
 * Copyright 2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
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

#ifndef VERSION_MAJOR
#define VERSION_MAJOR                   0
#endif

#ifndef VERSION_MINOR
#define VERSION_MINOR                   1
#endif

#ifndef VERSION_PATCH
#define VERSION_PATCH                   0 
#endif

#ifndef VERSION_BUILD
#define VERSION_BUILD                   GIT_REVCOUNT
#endif

#define PRODUCT_VERSION STRINGIZE(VERSION_MAJOR.VERSION_MINOR.VERSION_PATCH)

#ifndef PRODUCT_NAME
#error "PRODUCT_NAME is not defined"
#endif

#ifndef PRODUCT_COMPANY
#error "PRODUCT_COMPANY is not defined"
#endif

#ifndef PRODUCT_DESCRIPTION
#error "PRODUCT_DESCRIPTION is not defined"
#endif

#ifndef PRODUCT_CODE_NAME
#error "PRODUCT_CODE_NAME is not defined"
#endif

#ifndef PRODUCT_COPYRIGHT
#error "PRODUCT_COPYRIGHT is not defined"
#endif
