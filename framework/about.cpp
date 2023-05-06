/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#if BUILD_APPLICATION

#include "about.h"
#include "version.h"

#include <framework/app.h>
#include <framework/bgfx.h>
#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/module.h>
#include <framework/string.h>

#include <foundation/foundation.h>

#include <curl/curl.h>

#define HASH_ABOUT static_hash_string("about", 5, 0x8265f1ec7bd613dfULL)

FOUNDATION_STATIC const char* FOUNDATION_RESTRICT about_configuration_cstr()
{
    #if BUILD_DEBUG
    return "Debug";
    #elif BUILD_RELEASE
    return "Release";
    #elif BUILD_PROFILE
    return "Profile";
    #else
    return ICON_MD_NUMBERS;
    #endif
}

FOUNDATION_STATIC void about_render_dialog()
{
    ImGui::TextURL(PRODUCT_COMPANY, nullptr, STRING_CONST(PRODUCT_URL));
    ImGui::TrTextWrapped(PRODUCT_DESCRIPTION);

    string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));
    ImGui::TrTextWrapped("Version %s %.*s (%s)", about_configuration_cstr(), STRING_FORMAT(version_string), __DATE__);

    ImGui::Separator();

    ImGui::TrTextWrapped(PRODUCT_COPYRIGHT);

    ImGui::SetWindowFontScale(0.8f);
    ImGui::TrTextWrapped("This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Separator();

    ImGui::TrTextWrapped("Build %s", GIT_BRANCH);
    ImGui::TrTextWrapped("Commit %s", GIT_SHORT_HASH);
    ImGui::TrTextWrapped("Renderer %s", bgfx::getRendererName(bgfx::getRendererType()));

    ImGui::Separator();

    ImGui::TrTextWrapped("This software uses the following third-party libraries:");

    ImGui::SetWindowFontScale(0.9f);

    // Render app 3rd party libs
    app_render_3rdparty_libs();

    // Print foundation version
    version_t fv = foundation_version();
    string_const_t fv_string = string_from_version_static(fv);
    string_const_t fv_version_string = string_format_static(STRING_CONST("Foundation %.*s"), STRING_FORMAT(fv_string));
    ImGui::TextURL(STRING_RANGE(fv_version_string), STRING_CONST("https://github.com/mjansson/foundation_lib"));

    // Print BGFX version
    string_const_t bgfx_version_string = string_format_static(STRING_CONST("BGFX 1.%d.%d"), BGFX_API_VERSION, BGFX_REV_NUMBER);
    ImGui::TextURL(STRING_RANGE(bgfx_version_string), STRING_CONST("https://github.com/bkaradzic/bgfx"));

    // Print IMGUI version
    string_const_t imgui_version_string = string_format_static(STRING_CONST("IMGUI %s"), ImGui::GetVersion());
    ImGui::TextURL(STRING_RANGE(imgui_version_string), STRING_CONST("https://www.dearimgui.org/"));

    // Print GLFW version
    string_const_t glfw_version_string = string_format_static(STRING_CONST("GLFW %s"), glfwGetVersionString());
    ImGui::TextURL(STRING_RANGE(glfw_version_string), STRING_CONST("https://www.glfw.org/"));

    // Print libcurl version
    string_const_t curl_version_string = string_format_static(STRING_CONST("CURL %s"), curl_version());
    ImGui::TextURL(STRING_RANGE(curl_version_string), STRING_CONST("https://curl.se/"));

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC void about_menu_open_dialog(void* user_data)
{
    about_open_window();
}

// # PUBLIC API

void about_open_window()
{
    const char* title = string_format_static_const(tr("About - %s##8"), PRODUCT_NAME);
    app_open_dialog(title, uint32_t(IM_SCALEF(350)), uint32_t(IM_SCALEF(400)), false, about_render_dialog);
}

//
// # MODULE INITIALIZATION
//

void about_initialize()
{
    string_const_t versions_url = CTEXT(PRODUCT_VERSIONS_URL);
    if (versions_url.length)
    {
        app_register_menu(HASH_ABOUT, STRING_CONST("Help/Check for new version..."), 
            nullptr, 0, AppMenuFlags::Append, [](void* context)
        {
            dispatcher_post_event(EVENT_CHECK_NEW_VERSIONS, nullptr, 0);
        }, nullptr);
    }

    app_register_menu(HASH_ABOUT, 
        STRING_CONST("Help/Web Site"), 
        nullptr, 0, AppMenuFlags::Append, L1(dispatcher_post_event(EVENT_ABOUT_OPEN_WEBSITE)));

    app_register_menu(HASH_ABOUT, 
        STRING_CONST("Help/About"), 
        STRING_CONST("F1"), AppMenuFlags::Append, about_menu_open_dialog);
}

DEFINE_MODULE(ABOUT, about_initialize, nullptr, MODULE_PRIORITY_UI);

#endif // BUILD_APPLICATION
