/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "common.h"

#include <foundation/foundation.h>
#include <foundation/apple.h>
#include <foundation/string.h>

extern void* _window_handle;

#if FOUNDATION_PLATFORM_MACOS
    bool open_file_dialog(const char* dialog_title,
                                    const char* extension,
                                    const char* current_file_path,
                                    function<bool(string_const_t)> selected_file_callback)
    {
        NSWindow* app_window = (NSWindow*)_window_handle;
        if (!app_window)
            return false;
        
        string_t file_path_buffer = string_static_buffer(1024, true);
        if (current_file_path != nullptr)
        {
            string_t file_path = string_format(STRING_ARGS(file_path_buffer), STRING_CONST("%s"), current_file_path);
            file_path = path_clean(STRING_ARGS(file_path), file_path_buffer.length);
            file_path = string_replace(STRING_ARGS(file_path), file_path_buffer.length,
                STRING_CONST("/"), STRING_CONST("\\"), true);
        }
        
        @autoreleasepool {
            
            NSOpenPanel* openPanel = [NSOpenPanel openPanel];
           
            openPanel.title = [NSString stringWithUTF8String:dialog_title];
            
            openPanel.showsResizeIndicator = YES;
            openPanel.showsHiddenFiles = NO;
            openPanel.canChooseDirectories = NO;
            openPanel.canCreateDirectories = YES;
            openPanel.allowsMultipleSelection = NO;
    
            openPanel.allowedFileTypes = @[@"dcm"];
            
            [openPanel beginSheetModalForWindow: app_window completionHandler:^(NSInteger result)
             {
                // If the result is NSOKButton the user selected a file
                if (result == NSModalResponseOK)
                {
                    //get the selected file URLs
                    NSURL *selection = openPanel.URLs[0];
                    
                    //finally store the selected file path as a string
                    NSString* path = [[selection path] stringByResolvingSymlinksInPath];
                    
                    static char selected_file_path_buffer[BUILD_MAX_PATHLEN];
                    string_t selected_file_path = string_copy(STRING_BUFFER(selected_file_path_buffer),
                                                              path.UTF8String, path.length);
                    selected_file_callback(string_to_const(selected_file_path));
                }
            }];
        }

        return true;
    }
#endif
