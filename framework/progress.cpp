/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "progress.h"

#include <foundation/assert.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#endif

#if FOUNDATION_PLATFORM_WINDOWS
extern void* _window_handle;
ITaskbarList3* g_task_bar_list = nullptr;
#endif

void progress_initialize()
{
    #if FOUNDATION_PLATFORM_WINDOWS

        if (_window_handle == nullptr || !::IsWindowVisible((HWND)_window_handle))
            return;
    
        CoCreateInstance(
            CLSID_TaskbarList, NULL, CLSCTX_ALL,
            IID_ITaskbarList3, (void**)&g_task_bar_list);
        g_task_bar_list->SetProgressState((HWND)_window_handle, TBPF_NOPROGRESS);
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}

void progress_stop()
{
    #if FOUNDATION_PLATFORM_WINDOWS

        if (_window_handle == nullptr)
            return;

        FOUNDATION_ASSERT(g_task_bar_list);

        FlashWindow((HWND)_window_handle, FALSE);
        g_task_bar_list->SetProgressState((HWND)_window_handle, TBPF_NOPROGRESS);
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
        if (!g_task_bar_list || _window_handle == nullptr)
            return;
        g_task_bar_list->SetProgressValue((HWND)_window_handle, current, total);
    #elif FOUNDATION_PLATFORM_MACOS
        // Not supported yet
    #else
        #error "Not implemented"
    #endif
}
