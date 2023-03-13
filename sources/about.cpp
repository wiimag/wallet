/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "about.h"

#include <version.h>

#include <framework/app.h>
#include <framework/bgfx.h>
#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/service.h>
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

FOUNDATION_STATIC void about_menu_open_dialog(void* user_data)
{
    const char* title = string_format_static_const("About - %s##6", PRODUCT_NAME);
    app_open_dialog(title, [](void*)
    {
        ImGui::TextURL(PRODUCT_COMPANY, nullptr, STRING_CONST("https://equals-forty-two.com"));
        ImGui::TextWrapped(PRODUCT_DESCRIPTION);

        string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));
        ImGui::TextWrapped("Version %s %.*s (%s)", about_configuration_cstr(), STRING_FORMAT(version_string), __DATE__);

        ImGui::Separator();

        ImGui::TextWrapped(PRODUCT_COPYRIGHT);

        ImGui::SetWindowFontScale(0.8f);
        ImGui::TextWrapped("This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.");
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Separator();

        ImGui::TextWrapped("Build %s", GIT_BRANCH);
        ImGui::TextWrapped("Commit %s", GIT_SHORT_HASH);
        ImGui::TextWrapped("Renderer %s", bgfx::getRendererName(bgfx::getRendererType()));

        ImGui::Separator();

        ImGui::TextWrapped("This software uses the following third-party libraries:");

        ImGui::SetWindowFontScale(0.9f);
        // Print foundation version
        version_t fv = foundation_version();
        string_const_t fv_string = string_from_version_static(fv);
        ImGui::TextWrapped("Foundation %.*s", STRING_FORMAT(fv_string));

        // Print BGFX version
        ImGui::TextWrapped("BGFX 1.%d.%d (%s)", BGFX_API_VERSION, BGFX_REV_NUMBER, BGFX_REV_SHA1);

        // Print IMGUI version
        ImGui::TextWrapped("IMGUI %s", ImGui::GetVersion());

        // Print GLFW version
        ImGui::TextWrapped("GLFW %s", glfwGetVersionString());

        // Print libcurl version
        ImGui::TextWrapped("CURL %s", curl_version());

        ImGui::SetWindowFontScale(1.0f);

        return true;
    }, 700, 900, false, nullptr, nullptr);
}

FOUNDATION_STATIC void about_initialize()
{
    app_register_menu(HASH_ABOUT, 
        STRING_CONST("Help/About"), 
        STRING_CONST("F1"), AppMenuFlags::Append, about_menu_open_dialog);
}

DEFINE_SERVICE(ABOUT, about_initialize, nullptr, SERVICE_PRIORITY_UI);