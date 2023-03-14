/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "common.h"

#include <framework/string.h>

#include <foundation/foundation.h>
#include <foundation/apple.h>

extern void* _window_handle;

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

void system_add_menu_item(const char* name)
{
    NSWindow* app_window = (NSWindow*)_window_handle;
    if (!app_window)
        return;
    
    @autoreleasepool {

        // Add menu bar with File, Edit and Windows menus
        NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@"Main Menu"];
        [NSApp setMainMenu:mainMenu];

        // Find Edit menu if any
        NSMenu* editMenu = nil;
        for (NSMenuItem* item in mainMenu.itemArray)
        {
            if ([item.title isEqualToString:@"Edit"])
            {
                editMenu = item.submenu;
                break;
            }
        }

        // Add File menu
        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
        [mainMenu addItem:fileMenuItem];
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [fileMenuItem setSubmenu:fileMenu];

        // Add Open menu item
        NSMenuItem* openMenuItem = [[NSMenuItem alloc] initWithTitle:@"Open" action:@selector(openFile:) keyEquivalent:@"o"];
        [openMenuItem setTarget:app_window];
        [fileMenu addItem:openMenuItem];

        // Add Quit menu item
        NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
        [quitMenuItem setTarget:NSApp];
        [fileMenu addItem:quitMenuItem];

        // Add Edit menu
        NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
        [mainMenu addItem:editMenuItem];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenuItem setSubmenu:editMenu];
    }

}
