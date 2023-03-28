/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Installer Executable App
 */

#define FRAMEWORK_APP_IMPLEMENTATION
#define FRAMEWORK_APP_CUSTOM_RENDER_IMPLEMENTATION
#define FRAMEWORK_APP_EXTENDED_INITIALIZE installer_initialize
#define FRAMEWORK_APP_EXTENDED_SHUTDOWN installer_shutdown

#include <framework/app.h>
#include <framework/array.h>
#include <framework/string.h>
#include <framework/system.h>

struct installer_version_t
{
    string_t version{};
    string_t description{};
    string_t release_date{};
    string_t download_url{};
    string_t execute_script{};

    string_t* changelogs{ nullptr };
};

constexpr const char* MANIFEST_URL = "https://gist.githubusercontent.com/jschmidt42/cb4948480930c48a3116b1c9408919b9/raw/803418392d0e0fe3dca0291deea1739f89456e3e/wallet.installer.manifest.json";

struct INSTALLER_MODULE {
    bool initialized{ false };
    ImFont* title_font{ nullptr };
    dispatcher_thread_handle_t downloader_thread{ 0 };
    string_t app_data_local_path{};
    string_t app_data_local_install_path{};

    mutex_t* manifest_mutex{ nullptr };
    string_t manifest_app_name{};
    string_t manifest_app_description{};
    installer_version_t* manifest_versions{ nullptr };

} *_installer;

FOUNDATION_STATIC void installer_manifest_data(const json_object_t& json)
{
    log_infof(0, STRING_CONST("Installer manifest: %s"), json.buffer);

    string_const_t app_name = json["name"].as_string();
    if (string_is_null(app_name))
    {
        log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'name'"));
        return;
    }

    string_const_t app_description = json["description"].as_string();
    if (string_is_null(app_description))
    {
        log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'description'"));
        return;
    }

    char app_data_local_install_path_buffer[BUILD_MAX_PATHLEN];
    string_t app_data_local_install_path = string_copy(STRING_BUFFER(app_data_local_install_path_buffer), STRING_ARGS(_installer->app_data_local_path));

    char normalized_name_buffer[BUILD_MAX_PATHLEN];
    string_t normalized_name = path_normalize_name(STRING_BUFFER(normalized_name_buffer), STRING_ARGS(app_name), '_');
    app_data_local_install_path = path_append(STRING_ARGS(app_data_local_install_path), BUILD_MAX_PATHLEN, STRING_ARGS(normalized_name));
    app_data_local_install_path = path_clean(STRING_ARGS(app_data_local_install_path), BUILD_MAX_PATHLEN);

    installer_version_t* versions = nullptr;
    for (auto cv : json["versions"])
    {
        string_const_t version_string = cv["version"].as_string();
        if (string_is_null(version_string))
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'version'"));
            continue;
        }

        string_const_t version_description = cv["description"].as_string();
        if (string_is_null(version_description))
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'description'"));
            continue;
        }

        string_const_t version_release_date = cv["date"].as_string();
        if (string_is_null(version_release_date))
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'date'"));
            continue;
        }

        json_object_t package_data = cv["package"];
        if (package_data.is_null())
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'package'"));
            continue;
        }

        // Select the proper platform
        platform_t platform = system_platform();
        if (platform == PLATFORM_WINDOWS)
        {
            package_data = package_data["windows"];
        }
        else if (platform == PLATFORM_LINUX)
        {
            package_data = package_data["linux"];
        }
        else if (platform == PLATFORM_MACOS)
        {
            package_data = package_data["osx"];
        }
        else
        {
            log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: unsupported platform '%s'"), system_platform_name(platform));
            continue;
        }

        if (package_data.is_null())
        {
            log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'package.%s'"), system_platform_name(platform));
            continue;
        }

        string_const_t version_download_url = package_data["url"].as_string();
        if (string_is_null(version_download_url))
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'download_url'"));
            continue;
        }

        string_const_t version_execute_script = package_data["start"].as_string();
        if (string_is_null(version_execute_script))
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: missing 'execute_script'"));
            continue;
        }

        string_t* changelogs = nullptr;
        for (auto cl : cv["changelog"])
        {
            string_const_t changelog = cl.as_string();
            array_push(changelogs, string_clone(STRING_ARGS(changelog)));
        }

        if (array_size(changelogs) < 1)
        {
            log_error(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid installer manifest: Not enough changelog elements"));
            continue;
        }

        installer_version_t version{};
        version.version = string_clone(STRING_ARGS(version_string));
        version.description = string_clone(STRING_ARGS(version_description));
        version.release_date = string_clone(STRING_ARGS(version_release_date));
        version.download_url = string_clone(STRING_ARGS(version_download_url));
        version.execute_script = string_clone(STRING_ARGS(version_execute_script));
        version.changelogs = changelogs;

        array_push(versions, version);
    }

    if (mutex_lock(_installer->manifest_mutex))
    {
        _installer->manifest_versions = versions;
        _installer->manifest_app_description = string_clone(STRING_ARGS(app_description));
        _installer->app_data_local_install_path = string_clone(STRING_ARGS(app_data_local_install_path));
        _installer->manifest_app_name = string_clone(STRING_ARGS(app_name));

        mutex_unlock(_installer->manifest_mutex);
    }

}

FOUNDATION_STATIC void* installer_downloader_thread(void* context)
{
    if (!query_execute_json(MANIFEST_URL, FORMAT_JSON_WITH_ERROR, installer_manifest_data))
    {
        log_errorf(0, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to download installer manifest from '%s'"), MANIFEST_URL);
        return to_ptr(error());
    }

    return nullptr;
}

FOUNDATION_STATIC void installer_render_manifest_data()
{
    if (string_is_null(_installer->manifest_app_name))
    {
        ImGui::TrTextWrapped("Downloading installer manifest...");
        return;
    }

    if (array_size(_installer->manifest_versions) == 0)
    {
        ImGui::TrTextWrapped("No versions available");
        return;
    }

    installer_version_t* latest_version = array_last(_installer->manifest_versions);

    ImGui::TrTextWrapped("Latest version: %.*s (%.*s)", STRING_FORMAT(latest_version->version), STRING_FORMAT(latest_version->release_date));
    ImGui::TrTextWrapped("Current version: %.*s", STRING_FORMAT(_installer->app_data_local_install_path));

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TrTextUnformatted("Installation package:");
    ImGui::SameLine();
    if (ImGui::TextURL(STRING_RANGE(latest_version->download_url), STRING_ARGS(latest_version->download_url), 0))
    {
    }

    ImGui::TrTextUnformatted("Installation directory:");
    ImGui::SameLine();
    if (ImGui::TextURL(STRING_RANGE(_installer->app_data_local_install_path), nullptr, 0))
    {
        system_browse_to_file(STRING_ARGS(_installer->app_data_local_install_path), true);
    }
}

FOUNDATION_STATIC void installer_render()
{
    ImGui::MoveCursor(20, 4);
    ImGui::BeginGroup();
    // Draw installer title using a bigger font
    ImGui::PushFont(_installer->title_font);
    ImGui::Text("%s", PRODUCT_NAME);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::PopFont();

    // Setup default font
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.back());

    ImGui::TextWrapped("%s", PRODUCT_DESCRIPTION);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    if (mutex_lock(_installer->manifest_mutex))
    {
        installer_render_manifest_data();
        if (!mutex_unlock(_installer->manifest_mutex))
        {
            log_warnf(0, WARNING_DEADLOCK, STRING_CONST("Installer dead lock"));
            ImGui::TrTextWrapped("Installer dead lock!");
            return;
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    // Draw privacy notice
    static bool agree = BUILD_DEBUG ? true : false;
    ImGui::TrTextWrapped("By installing this software, you agree to the following privacy policy:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.0f);
    ImGui::Checkbox(tr("I Agree"), &agree);

    ImGui::TextURL("https://equals-forty-two.com/privacy", nullptr, STRING_CONST("https://equals-forty-two.com/privacy"));
    ImGui::EndGroup();

    // Draw a footer rect at the bottom of the screen
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 window_pos = ImGui::GetWindowPos();
    ImVec2 window_size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(ImVec2(window_pos.x, window_pos.y + window_size.y - 140.0f),
           ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
                  IM_COL32(110, 110, 110, 255));

    ImGui::SetCursorPos({28, window_size.y - 95.0f});
    ImGui::BeginGroup();

    // Draw a button to start the installation
    if (ImGui::Button(tr("Exit"), ImVec2(200, 50)))
        glfw_request_close_window(glfw_main_window());

    ImGui::BeginDisabled(!agree || array_size(_installer->manifest_versions) == 0);
    // Draw a button to exit the installer
    ImGui::SameLine(window_size.x - 260.0f);
    if (ImGui::Button(tr("Install"), ImVec2(200, 50)))
    {
    }
    ImGui::EndDisabled();

    ImGui::EndGroup();
    ImGui::PopFont();
}

extern void installer_initialize(GLFWwindow* window)
{
    _installer = MEM_NEW(0, INSTALLER_MODULE);

    // Create a large title font
    _installer->title_font = imgui_load_main_font(4.0f);

    _installer->manifest_mutex = mutex_allocate(STRING_CONST("Installer Manifest Lock"));

    // Create thread to start downloading the installer manifest
    _installer->downloader_thread = dispatch_thread("Downloader", installer_downloader_thread);

    // Get the local app data path
    string_const_t app_data_local = system_app_data_local_path();
    _installer->app_data_local_path = string_clone(STRING_ARGS(app_data_local));
}

extern void installer_shutdown()
{
    if (dispatcher_thread_is_running(_installer->downloader_thread))
        dispatcher_thread_stop(_installer->downloader_thread);

    mutex_deallocate(_installer->manifest_mutex);

    string_deallocate(_installer->app_data_local_path.str);
    string_deallocate(_installer->app_data_local_install_path.str);
    string_deallocate(_installer->manifest_app_name.str);
    string_deallocate(_installer->manifest_app_description.str);

    for (size_t i = 0, end = array_size(_installer->manifest_versions); i < end; ++i)
    {
        installer_version_t* version = _installer->manifest_versions + i;
        string_deallocate(version->version.str);
        string_deallocate(version->description.str);
        string_deallocate(version->release_date.str);
        string_deallocate(version->download_url.str);
        string_deallocate(version->execute_script.str);
        for (size_t j = 0, end = array_size(version->changelogs); j < end; ++j)
            string_deallocate(version->changelogs[j].str);
        array_deallocate(version->changelogs);
    }
    array_deallocate(_installer->manifest_versions);

    MEM_DELETE(_installer);
}

extern void app_render(GLFWwindow* window, int frame_width, int frame_height)
{
    FOUNDATION_ASSERT(_installer);

    dispatcher_update();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)frame_width, (float)frame_height));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

    if (ImGui::Begin(app_title(), nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings))
    {    
        installer_render();
    } ImGui::End();

    ImGui::PopStyleVar(2);
}
