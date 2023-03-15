/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "tabs.h"

#include <framework/imgui.h>
#include <framework/array.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>

struct tabbar_t 
{
    int tab_index{0};
    int* active_tab{ nullptr };
    int push_color_tabs_counter{0};

    ImVec2 end_tabs_cursor;
    function<void()> tools_callback{ nullptr };
};

static int _tab_current = -1;
static tabbar_t* _tabbars = nullptr;

FOUNDATION_STATIC void tab_capture_cursor(tabbar_t* tb)
{
    tb->end_tabs_cursor = ImVec2(ImGui::GetItemRectMax().x + 8.0f, ImGui::GetItemRectMin().y);
}

void tab_draw(
    const char* label, bool* opened,
    ImGuiTabItemFlags tab_flags,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback)
{
    static bool tab_init_selected = false;

    tabbar_t* tb = array_last(_tabbars);
    FOUNDATION_ASSERT(tb);

    int& tab_index = tb->tab_index;
    int& current_tab = *tb->active_tab;

    if (current_tab < 0)
        current_tab = 0;

    if (opened && *opened == false && current_tab == tab_index)
        current_tab++;

    tab_flags |= !tab_init_selected && current_tab == tab_index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem(label, opened, tab_flags))
    {
        tab_capture_cursor(tb);

        if (tab_tools_callback)
            tab_tools_callback();
        else if ((tab_flags & (ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder)) == (ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder))
        {
            // Make a exception to render the tab bar end of row controls for the last tabs if any.
            if (tb->tools_callback)
            {
                ImGui::SameLine();
                tb->tools_callback();
                tb->tools_callback = nullptr;
            }
        }

        if (tab_index == current_tab)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

            if (render_tab_callback != nullptr)
            {   
                if (ImGui::BeginChild(label))
                    render_tab_callback();
                ImGui::EndChild();
            }
            else
                ImGui::TextUnformatted(label);

            ImGui::PopStyleVar(1);
        }

        if (!tab_init_selected && current_tab == tab_index)
            tab_init_selected = true;
        else if (tab_init_selected && current_tab != tab_index)
            current_tab = tab_index;
         
        ImGui::EndTabItem();
    }
    else
    {
        tab_capture_cursor(tb);
    }

    tab_index++;
}

void tab_set_color(ImU32 color)
{
    tabbar_t* tb = array_last(_tabbars);
    FOUNDATION_ASSERT(tb);

    ImGui::PushStyleColor(ImGuiCol_Tab, color);
    ImGui::PushStyleColor(ImGuiCol_TabActive, imgui_color_highlight(color, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, imgui_color_highlight(color, 0.3f));

    tb->push_color_tabs_counter += 3;
}

void tab_set_color(const ImVec4& c)
{
    return tab_set_color(ImGui::ColorConvertFloat4ToU32(c));
}

void tab_pop_color() 
{
    tabbar_t* tb = array_last(_tabbars);
    FOUNDATION_ASSERT(tb);

    tb->push_color_tabs_counter -= 3;
    ImGui::PopStyleColor(3);
}

bool tabs_begin(
    const char* FOUNDATION_RESTRICT tab_bar_name, 
    int& active_tab, 
    ImGuiTabBarFlags flags /*= ImGuiTabBarFlags_None*/,
    const function<void()>& tools_callback /*= nullptr*/)
{
    if (ImGui::BeginTabBar(tab_bar_name, flags | ImGuiTabBarFlags_NoTabListScrollingButtons))
    {
        tabbar_t tb{};
        tb.tab_index = 0;
        tb.active_tab = &active_tab;
        tb.push_color_tabs_counter = 0;
        tb.tools_callback = tools_callback;

        array_push_memcpy(_tabbars, &tb);
        return true;
    }

    return false;
}

void tabs_end()
{
    tabbar_t* tb = array_last(_tabbars);
    FOUNDATION_ASSERT(tb);

    ImGui::PopStyleColor(tb->push_color_tabs_counter);

    if (*tb->active_tab > tb->tab_index)
        *tb->active_tab = tb->tab_index - 1;

    ImGui::EndTabBar();

    if (tb->end_tabs_cursor.x < ImGui::GetWindowContentRegionMax().x && tb->tools_callback)
    {
        ImGui::SetCursorScreenPos(tb->end_tabs_cursor);
        tb->tools_callback();
    }

    array_pop(_tabbars);
}

void tabs_draw_all()
{
    static ImGuiTabBarFlags tabs_init_flags = ImGuiTabBarFlags_Reorderable;

    if (_tab_current == -1)
        _tab_current = session_get_integer("current_tab", _tab_current);
    
    if (tabs_begin("Tabs", _tab_current, tabs_init_flags, nullptr))
    {
        service_foreach_tabs();


        tabbar_t* tb = array_last(_tabbars);
        if (tb)
        {
            _tab_current = max(0, min(_tab_current, tb->tab_index-1));
        }
        tabs_end();
    }

    if ((tabs_init_flags & ImGuiTabBarFlags_AutoSelectNewTabs) == 0)
        tabs_init_flags |= ImGuiTabBarFlags_AutoSelectNewTabs;
}

void tabs_shutdown()
{
    session_set_integer("current_tab", _tab_current);
    array_deallocate(_tabbars);
}
