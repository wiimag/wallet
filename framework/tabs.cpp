/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "tabs.h"

#include <framework/imgui.h>
#include <framework/array.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/module.h>

struct tabbar_t 
{
    int tab_index{0};
    int* active_tab{ nullptr };
    int push_color_tabs_counter{0};

    /*! Track the next tab to automatically select, -1 if none. */
    int select_tab_index{ -1 };
    bool tab_init_selected{ false };

    ImVec2 end_tabs_cursor;
    function<void()> tools_callback{ nullptr };

    /*! Track the tabs that are selected. */
    int* tab_selection_queue{ nullptr };
};

static int _tab_current = -1;
static tabbar_t* _tabbars = nullptr;
static int _current_tabbar_index = -1;

FOUNDATION_STATIC void tab_capture_cursor(tabbar_t* tb)
{
    tb->end_tabs_cursor = ImVec2(ImGui::GetItemRectMax().x + 8.0f, ImGui::GetItemRectMin().y);
}

FOUNDATION_FORCEINLINE tabbar_t* tab_current_bar()
{
    FOUNDATION_ASSERT(_tabbars);
    FOUNDATION_ASSERT(_current_tabbar_index >= 0 && _current_tabbar_index < (int)array_size(_tabbars));
    return _tabbars + _current_tabbar_index;
}

void tab_draw(
    const char* label, bool* opened,
    ImGuiTabItemFlags tab_flags,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback)
{
    tabbar_t* tb = tab_current_bar();

    int& tab_index = tb->tab_index;
    int& current_tab = *tb->active_tab;

    if (current_tab < 0)
        current_tab = 0;

    if (opened && *opened == false && current_tab == tab_index)
        current_tab++;

    if (tb->select_tab_index >= 0)
    {
        tb->tab_init_selected = false;
        current_tab = tb->select_tab_index;
        tb->select_tab_index = -1;
    }

    tab_flags |= !tb->tab_init_selected && current_tab == tab_index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
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
                {
                    if (ImGui::IsWindowAppearing())
                    {
                        ImGuiWindow* current_window = ImGui::GetCurrentWindow();
                        ImGui::FocusWindow(current_window);
                    }

                    render_tab_callback();
                }
                ImGui::EndChild();
            }
            else
                ImGui::TextUnformatted(label);

            ImGui::PopStyleVar(1);
        }

        if (!tb->tab_init_selected && current_tab == tab_index)
        {
            tb->tab_init_selected = true;
            array_insert(tb->tab_selection_queue, 0, tab_index);
        }
        else if (tb->tab_init_selected && current_tab != tab_index)
        {
            // Remove current_tab in selection queue.
            if (array_size(tb->tab_selection_queue) > 0 && tb->tab_selection_queue[0] != tab_index)
                array_remove(tb->tab_selection_queue, tab_index);

            array_insert(tb->tab_selection_queue, 0, tab_index);
            current_tab = tab_index;
        }
         
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
    tabbar_t* tb = tab_current_bar();

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
    tabbar_t* tb = tab_current_bar();
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
        ++_current_tabbar_index;
        tabbar_t* tb = _tabbars + _current_tabbar_index;
        
        // Do we need to stack a new tab bar?
        if (_current_tabbar_index + 1 > (int)array_size(_tabbars))
        {
            tabbar_t ntb{};
            array_push_memcpy(_tabbars, &ntb);
            tb = _tabbars + _current_tabbar_index;
        }

        tb->tab_index = 0;
        tb->active_tab = &active_tab;
        tb->push_color_tabs_counter = 0;
        tb->tools_callback = tools_callback;

        return true;
    }

    return false;
}

void tabs_end()
{
    tabbar_t* tb = tab_current_bar();

    ImGui::PopStyleColor(tb->push_color_tabs_counter);

    if (*tb->active_tab > tb->tab_index)
        *tb->active_tab = tb->tab_index - 1;

    // Check if the CTRL+TAB key has been pressed to change the active tab.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Tab, 0, ImGuiInputFlags_RouteGlobal))
    {
        if (array_size(tb->tab_selection_queue) > 1)
        {
            int current_tab = tb->tab_selection_queue[0];
            array_erase_ordered_safe(tb->tab_selection_queue, 0);
            array_push(tb->tab_selection_queue, current_tab);
            tb->select_tab_index = tb->tab_selection_queue[0];
        }
        else
            tb->select_tab_index = (*tb->active_tab + 1) % tb->tab_index;
        tb->select_tab_index = max(0, min(tb->select_tab_index, tb->tab_index));
    }
    else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab, 0, ImGuiInputFlags_RouteGlobal))
    {
        if (array_size(tb->tab_selection_queue) > 1)
        {
            int next_tab = *array_last(tb->tab_selection_queue);
            array_pop_safe(tb->tab_selection_queue);
            array_insert(tb->tab_selection_queue, 0, next_tab);
            tb->select_tab_index = tb->tab_selection_queue[0];
        }
        else
            tb->select_tab_index = (*tb->active_tab + tb->tab_index - 1) % tb->tab_index;
        tb->select_tab_index = max(0, min(tb->select_tab_index, tb->tab_index));
    }

    // Remove all index from selection queue that are larger than the tab index
    for (int i = (int)array_size(tb->tab_selection_queue) - 1; i >= 0; --i)
    {
        if (tb->tab_selection_queue[i] >= tb->tab_index)
            array_erase(tb->tab_selection_queue, i);
    }


    ImGui::EndTabBar();

    if (tb->end_tabs_cursor.x < ImGui::GetWindowContentRegionMax().x && tb->tools_callback)
    {
        ImGui::SetCursorScreenPos(tb->end_tabs_cursor);
        tb->tools_callback();
    }

    _current_tabbar_index--;
}

void tabs_draw_all()
{
    static ImGuiTabBarFlags tabs_init_flags = ImGuiTabBarFlags_Reorderable;

    if (_tab_current == -1)
        _tab_current = session_get_integer("current_tab", _tab_current);
    
    if (tabs_begin("Tabs", _tab_current, tabs_init_flags, nullptr))
    {
        module_foreach_tabs();

        tabbar_t* tb = array_last(_tabbars);
        if (tb)
            _tab_current = max(0, min(_tab_current, tb->tab_index-1));
        tabs_end();
    }

    if ((tabs_init_flags & ImGuiTabBarFlags_AutoSelectNewTabs) == 0)
        tabs_init_flags |= ImGuiTabBarFlags_AutoSelectNewTabs;
}

void tabs_shutdown()
{
    session_set_integer("current_tab", _tab_current);

    for (unsigned i = 0; i < array_size(_tabbars); ++i)
    {
        tabbar_t* tb = _tabbars + i;
        array_deallocate(tb->tab_selection_queue);
    }
    array_deallocate(_tabbars);
}
