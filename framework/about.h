/* 
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * About dialog that show build and version informations.
 */

#pragma once

#include <foundation/platform.h>

#if BUILD_APPLICATION

/*! Initialize about module.
 *
 *  We use #FOUNDATION_LINKER_INCLUDE in order to force the linker to include the #about module.
 */
FOUNDATION_EXTERN void FOUNDATION_LINKER_INCLUDE(about_initialize)();

/*! Opens the about dialog. */
void about_open_window();

#endif // BUILD_APPLICATION
