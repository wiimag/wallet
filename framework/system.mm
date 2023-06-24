/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "system.h"

#if FOUNDATION_PLATFORM_MACOS

#include <framework/string.h>
#include <framework/common.h>

#include <foundation/apple.h>
#include <foundation/foundation.h>

extern void* _window_handle;

bool system_open_file_dialog(const char* dialog_title,
                             const char* extension,
                             const char* current_file_path,
                             const function<bool(string_const_t)>& selected_file_callback)
{
    NSWindow* app_window = (NSWindow*)_window_handle;
    if (!app_window)
        return false;
    
    string_t file_path = {};
    string_t file_path_buffer = string_static_buffer(1024, true);
    if (current_file_path != nullptr)
    {
        file_path = string_format(STRING_ARGS(file_path_buffer), STRING_CONST("%s"), current_file_path);
        file_path = path_clean(STRING_ARGS(file_path), file_path_buffer.length);
        file_path = string_replace(STRING_ARGS(file_path), file_path_buffer.length,
            STRING_CONST("/"), STRING_CONST("\\"), true);
    }

    string_const_t filename = path_file_name(STRING_ARGS(file_path));
    
    @autoreleasepool 
    {    
        NSOpenPanel* openPanel = [NSOpenPanel openPanel];
        
        openPanel.title = [NSString stringWithUTF8String:dialog_title];
        
        openPanel.showsResizeIndicator = YES;
        openPanel.showsHiddenFiles = NO;
        openPanel.canChooseDirectories = NO;
        openPanel.canCreateDirectories = YES;
        openPanel.allowsMultipleSelection = NO;

        if (extension)
        {
            // Split extension string into array
            string_t* exts = string_split(string_to_const(extension), CTEXT("|"));
            
            // Add extension to savePanel.allowedFileTypes
            for (unsigned int i = 0; i < array_size(exts); ++i)
            {
                // Skip description
                if (i % 2 == 0)
                    continue;
                string_t ext = exts[i];
                // Remove *. from extension
                ext.str += 2;
                ext.length -= 2;

                NSString* ns_ext = [NSString stringWithUTF8String:ext.str];
                [openPanel setAllowedFileTypes:[openPanel.allowedFileTypes arrayByAddingObject:ns_ext]];

                // Add default extension to savePanel.nameFieldStringValue
                if (filename.length != 0 && string_find(STRING_ARGS(filename), '.', 0))
                {
                    NSString* ns_filename = [NSString stringWithUTF8String:filename.str];
                    openPanel.nameFieldStringValue = ns_filename;

                    // Append . to filename
                    ns_filename = [ns_filename stringByAppendingString:@"."];
                    
                    // Add .extension to filename
                    NSString* ns_ext = [NSString stringWithUTF8String:ext.str];
                    openPanel.nameFieldStringValue = [ns_filename stringByAppendingString:ns_ext];
                }
            }
            
            string_array_deallocate(exts);

            openPanel.allowsOtherFileTypes = NO;
        }
        else
        {
            openPanel.allowsOtherFileTypes = YES;
        }

        // Create a local copy of the callback for the async call to the save panel
        function<bool(string_const_t)> selected_file_callback_copy = selected_file_callback;
        
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
                string_t selected_file_path = string_copy(STRING_BUFFER(selected_file_path_buffer), path.UTF8String, path.length);
                selected_file_callback_copy(string_to_const(selected_file_path));
            }
        }];
    }

    return true;
}

bool system_save_file_dialog(
    const char* dialog_title,
    const char* extension,
    const char* current_file_path,
    const function<bool(string_const_t)>& selected_file_callback)
{
    NSWindow* app_window = (NSWindow*)_window_handle;
    if (!app_window)
        return false;
    
    string_t file_path = {};
    string_t file_path_buffer = string_static_buffer(1024, true);
    if (current_file_path != nullptr)
    {
        file_path = string_format(STRING_ARGS(file_path_buffer), STRING_CONST("%s"), current_file_path);
        file_path = path_clean(STRING_ARGS(file_path), file_path_buffer.length);
        file_path = string_replace(STRING_ARGS(file_path), file_path_buffer.length,
            STRING_CONST("/"), STRING_CONST("\\"), true);
    }

    string_const_t filename = path_file_name(STRING_ARGS(file_path));
    
    @autoreleasepool 
    {
        // Open save dialog
        NSSavePanel* savePanel = [NSSavePanel savePanel];

        savePanel.title = [NSString stringWithUTF8String:dialog_title];

        savePanel.showsHiddenFiles = NO;
        savePanel.showsResizeIndicator = YES;
        savePanel.canCreateDirectories = YES;

        if (filename.length > 0)
        {
            NSString* ns_filename = [NSString stringWithUTF8String:filename.str];
            savePanel.nameFieldStringValue = ns_filename;
        }

        if (extension)
        {
            // Split extension string into array
            string_t* exts = string_split(string_to_const(extension), CTEXT("|"));
            
            // Add extension to savePanel.allowedFileTypes
            for (unsigned int i = 0; i < array_size(exts); ++i)
            {
                // Skip description
                if (i % 2 == 0)
                    continue;
                string_t ext = exts[i];
                // Remove *. from extension
                ext.str += 2;
                ext.length -= 2;

                NSString* ns_ext = [NSString stringWithUTF8String:ext.str];
                [savePanel setAllowedFileTypes:[savePanel.allowedFileTypes arrayByAddingObject:ns_ext]];

                // Add default extension to savePanel.nameFieldStringValue
                if (filename.length != 0 && string_find(STRING_ARGS(filename), '.', 0))
                {
                    NSString* ns_filename = [NSString stringWithUTF8String:filename.str];
                    savePanel.nameFieldStringValue = ns_filename;

                    // Append . to filename
                    ns_filename = [ns_filename stringByAppendingString:@"."];
                    
                    // Add .extension to filename
                    NSString* ns_ext = [NSString stringWithUTF8String:ext.str];
                    savePanel.nameFieldStringValue = [ns_filename stringByAppendingString:ns_ext];
                }
            }
            
            string_array_deallocate(exts);

            savePanel.allowsOtherFileTypes = NO;
        }
        else
        {
            savePanel.allowsOtherFileTypes = YES;
        }

        // Create a local copy of the callback for the async call to the save panel
        function<bool(string_const_t)> selected_file_callback_copy = selected_file_callback;

        [savePanel beginSheetModalForWindow: app_window completionHandler:^(NSInteger result)
        {
            // If the result is NSOKButton the user selected a file
            if (result == NSModalResponseOK)
            {
                // Get the save path file
                NSURL *selection = savePanel.URL;

                //finally store the selected file path as a string
                NSString* path = [[selection path] stringByResolvingSymlinksInPath];

                static char selected_file_path_buffer[BUILD_MAX_PATHLEN];
                string_t selected_file_path = string_copy(STRING_BUFFER(selected_file_path_buffer), path.UTF8String, path.length);
                selected_file_callback_copy(string_to_const(selected_file_path));
            }
        }];
    }

    return true;
}

#if 0
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
#endif

#endif
