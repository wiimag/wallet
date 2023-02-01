/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "progress.h"

#include <foundation/assert.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#endif

#if FOUNDATION_PLATFORM_WINDOWS
extern HWND _window_handle;
ITaskbarList3* g_task_bar_list = nullptr;
#endif

void progress_initialize()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        CoCreateInstance(
            CLSID_TaskbarList, NULL, CLSCTX_ALL,
            IID_ITaskbarList3, (void**)&g_task_bar_list);
        g_task_bar_list->SetProgressState(_window_handle, TBPF_NOPROGRESS);
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}

void progress_stop()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        FOUNDATION_ASSERT(g_task_bar_list);

        FlashWindow(_window_handle, FALSE);
        g_task_bar_list->SetProgressState(_window_handle, TBPF_NOPROGRESS);
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}

void progress_finalize()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        if (!g_task_bar_list)
            return;

        progress_stop();

        g_task_bar_list->Release();
        g_task_bar_list = nullptr;
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}

void progress_set(size_t current, size_t total)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        if (!g_task_bar_list)
            return;
        g_task_bar_list->SetProgressValue(_window_handle, current, total);
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}
