/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <framework/common.h>
#include <framework/service.h>
#include <framework/session.h>
#include <framework/table.h>
#include <framework/imgui.h>
#include <framework/string.h>

#include <foundation/path.h>

#if BUILD_DEVELOPMENT

#include <doctest/doctest.h>

#define HASH_TEST_RUNNER static_hash_string("test_runner", 11, 0x9b1ffcb52dac6a0fULL)

typedef struct TestRunnerCase {
    string_t name;
    string_t suite;
    string_t filename;
    string_t description;
    string_t results;
    bool skipped;
    int status;
} test_runner_case_t;

static table_t* _test_runner_table = nullptr;
static bool _test_runner_window_opened = false;
static test_runner_case_t* _test_runner_cases = nullptr;
static char _test_runner_search_filter[64] = "";

//
// # PRIVATE
//

FOUNDATION_STATIC void test_runner_clean_results(test_runner_case_t* e)
{
    string_deallocate(e->results.str);
    e->status = 0;
}

FOUNDATION_STATIC void test_runner_clean_cases()
{
    foreach(e, _test_runner_cases)
    {
        test_runner_clean_results(e);
        string_deallocate(e->name.str);
        string_deallocate(e->suite.str);
        string_deallocate(e->filename.str);
        string_deallocate(e->description.str);
    }
    array_deallocate(_test_runner_cases);
}

FOUNDATION_STATIC void test_runner_load_tests()
{
    // Setup test context to enumerate all suite and cases
    doctest::Context context;
    context.setOption("list-test-cases", "true");
    context.setOption("reporters", "test_runner");
    context.setOption("no-intro", "true");
    context.setOption("no-version", "true");
    context.setOption("no-debug-output", "true");

    context.run();
}

struct TestRunnerReporter : public doctest::IReporter
{
    const doctest::ContextOptions& opt;

    TestRunnerReporter(const doctest::ContextOptions& in)
        : opt(in)
    {
        test_runner_clean_cases();
    }

    void report_query(const doctest::QueryData& in) override
    {
        for (unsigned i = 0; i < in.num_data; ++i) 
        {
            const doctest::TestCaseData* tcd = in.data[i];

            test_runner_case_t test_runner_case;
            test_runner_case.status = tcd->m_may_fail ? -1 : 0;
            test_runner_case.name = string_clone(tcd->m_name, string_length(tcd->m_name));
            test_runner_case.suite = string_clone(tcd->m_test_suite, string_length(tcd->m_test_suite));
            test_runner_case.filename = string_clone(tcd->m_file.c_str(), tcd->m_file.size());
            test_runner_case.description = string_clone(tcd->m_description, string_length(tcd->m_description));
            test_runner_case.skipped = tcd->m_skip;
            test_runner_case.results = {};

            array_push_memcpy(_test_runner_cases, &test_runner_case);
        }
    }

    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats& in) override {}

    void test_case_start(const doctest::TestCaseData& in) override {}
    void test_case_reenter(const doctest::TestCaseData& in) override {}
    void test_case_end(const doctest::CurrentTestCaseStats& in) override {}
    void test_case_exception(const doctest::TestCaseException& /*in*/) override {}
    void test_case_skipped(const doctest::TestCaseData& /*in*/) override {}

    void subcase_start(const doctest::SubcaseSignature& in) override {}
    void subcase_end() override {}

    void log_assert(const doctest::AssertData& in) override {}
    void log_message(const doctest::MessageData& in) override {}
};

FOUNDATION_STATIC void test_runner_run_case(test_runner_case_t* tc)
{
    // Setup test context to enumerate all suite and cases
    doctest::Context context;
    context.setOption("abort-after", 1);
    context.setOption("test-suite", tc->suite.str);
    context.setOption("test-case", tc->name.str);
    context.setOption("reporters", "console,foundation");
    context.setOption("no-intro", "true");
    context.setOption("no-version", "true");
    context.setOption("duration", "true");
    context.setOption("minimal", "false");
    context.setOption("success", "false");
    context.setOption("no-path-filenames", "true");

    if (!doctest::detail::isDebuggerActive())
    {
        context.setOption("no-breaks", true);
        context.setOption("no-debug-output", "true");
    }

    char test_log_path_buffer[BUILD_MAX_PATHLEN];
    string_t tests_log_path = path_make_temporary(STRING_CONST_CAPACITY(test_log_path_buffer));
    tests_log_path = string_concat(STRING_CONST_CAPACITY(test_log_path_buffer), STRING_ARGS(tests_log_path), STRING_CONST(".log"));
    tests_log_path = path_clean(STRING_ARGS(tests_log_path), BUILD_MAX_PATHLEN);
    string_const_t tests_log_dir = path_directory_name(STRING_ARGS(tests_log_path));
    if (!fs_is_directory(STRING_ARGS(tests_log_dir)))
        fs_make_directory(STRING_ARGS(tests_log_dir));
    context.setOption("out", tests_log_path.str);
        
    if (ImGui::Begin(tc->suite.str, nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoSavedSettings))
    {
        test_runner_clean_results(tc);
        auto c = context.run();
        if (c == 0 && context.failureCount() == 0)
            tc->status = 1;
        else
            tc->status = c >= 0 ? -context.failureCount() : c;

        tc->results = fs_read_text(STRING_ARGS(tests_log_path));
    }

    ImGui::End();
}

FOUNDATION_STATIC cell_t test_runner_case_name(table_element_ptr_t element, const column_t* column)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    return string_to_const(tc->name);
}

FOUNDATION_STATIC void test_runner_case_name_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    if (tc->description.length > 0)
        ImGui::TextUnformatted(STRING_RANGE(tc->description));
    else
        ImGui::TextUnformatted("No description");
}

FOUNDATION_STATIC void test_runner_case_status_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    if (tc->results.length > 0)
        ImGui::TextUnformatted(STRING_RANGE(tc->results));
    else
        ImGui::TextUnformatted("No Results");
}

FOUNDATION_STATIC cell_t test_runner_case_suite(table_element_ptr_t element, const column_t* column)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    return string_to_const(tc->suite);
}

FOUNDATION_STATIC cell_t test_runner_case_filename(table_element_ptr_t element, const column_t* column)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    return path_file_name(STRING_ARGS(tc->filename));
}

FOUNDATION_STATIC cell_t test_runner_case_actions(table_element_ptr_t element, const column_t* column)
{
    test_runner_case_t* tc = (test_runner_case_t*)element;
    if ((column->flags & COLUMN_RENDER_ELEMENT) != 0)
    {
        if (ImGui::SmallButton("Run"))
            test_runner_run_case(tc);
    }

    return (double)tc->skipped;
}

FOUNDATION_STATIC cell_t test_runner_case_status(table_element_ptr_t element, const column_t* column)
{
    const test_runner_case_t* tc = (const test_runner_case_t*)element;
    if (tc->status == 0)
        return "";

    if (tc->status >= 1)
        return ICON_MD_CHECK;

    return ICON_MD_ERROR;
}

FOUNDATION_STATIC void test_runner_case_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    test_runner_case_t* tc = (test_runner_case_t*)element;
    test_runner_run_case(tc);
}

FOUNDATION_STATIC void test_runner_create_table()
{
    _test_runner_table = table_allocate("test_runner#4");
    _test_runner_table->flags |= TABLE_HIGHLIGHT_HOVERED_ROW;
    _test_runner_table->search_filter = string_const(_test_runner_search_filter, string_length(_test_runner_search_filter));
    table_add_column(_test_runner_table, "Actions", test_runner_case_actions, COLUMN_FORMAT_NUMBER, COLUMN_CUSTOM_DRAWING | COLUMN_HIDE_HEADER_TEXT | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(90.0f));
    table_add_column(_test_runner_table, "Suite", test_runner_case_suite, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_SEARCHABLE)
        .set_selected_callback(test_runner_case_selected);
    table_add_column(_test_runner_table, "Name", test_runner_case_name, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_STRETCH | COLUMN_SEARCHABLE)
        .set_tooltip_callback(test_runner_case_name_tooltip)
        .set_selected_callback(test_runner_case_selected);
    table_add_column(_test_runner_table, "Filename", test_runner_case_filename, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_SEARCHABLE)
        .set_selected_callback(test_runner_case_selected);
    table_add_column(_test_runner_table, ICON_MD_CHECK "||Status", test_runner_case_status, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_FREEZE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(45.0f))
        .set_tooltip_callback(test_runner_case_status_tooltip)
        .set_selected_callback(test_runner_case_selected);

    test_runner_load_tests();
}

FOUNDATION_STATIC void test_runner_window_render()
{
    static bool window_opened_once = false;
    if (!window_opened_once)
        ImGui::SetNextWindowSizeConstraints(ImVec2(imgui_get_font_ui_scale(770.0f), 420), ImVec2(INFINITY, INFINITY));

    if (ImGui::Begin("Test Runner##1", &_test_runner_window_opened, ImGuiWindowFlags_AlwaysUseWindowPadding))
    {
        if (_test_runner_table == nullptr)
            test_runner_create_table();

        ImGui::BeginGroup();
        if (ImGui::InputTextWithHint("##SearchFilter", "Filter test cases...", 
            STRING_CONST_CAPACITY(_test_runner_search_filter), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EscapeClearsAll))
        {
            _test_runner_table->search_filter = string_const(_test_runner_search_filter, string_length(_test_runner_search_filter));
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            foreach(e, _test_runner_cases)
            {
                e->status = 0;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Run All"))
        {
            foreach(e, _test_runner_table->rows)
            {
                if ((int)i >= _test_runner_table->rows_visible_count)
                    break;
                test_runner_run_case((test_runner_case_t*)e->element);
            }
        }
        ImGui::EndGroup();

        ImGui::Spacing();
        table_render(_test_runner_table, _test_runner_cases, array_size(_test_runner_cases), sizeof(test_runner_case_t), 0.0f, 0.0f);
    }

    ImGui::End();

    if (_test_runner_window_opened == false)
    {
        table_deallocate(_test_runner_table);
        _test_runner_table = nullptr;
    }
}

FOUNDATION_STATIC void test_runner_menu()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem(ICON_MD_LOGO_DEV " Test Runner", nullptr, &_test_runner_window_opened);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (_test_runner_window_opened)
        test_runner_window_render();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void test_runner_initialize()
{
    _test_runner_window_opened = session_get_bool("test_runner_window_opened", _test_runner_window_opened);
    string_const_t test_runner_search_filter = session_get_string("test_runner_search_filter", "");
    string_copy(STRING_CONST_CAPACITY(_test_runner_search_filter), STRING_ARGS(test_runner_search_filter));
    service_register_menu(HASH_TEST_RUNNER, test_runner_menu);
}

FOUNDATION_STATIC void test_runner_shutdown()
{
    session_set_bool("test_runner_window_opened", _test_runner_window_opened);
    session_set_string("test_runner_search_filter", _test_runner_search_filter, string_length(_test_runner_search_filter));

    if (_test_runner_table)
        table_deallocate(_test_runner_table);

    test_runner_clean_cases();
}

REGISTER_REPORTER("test_runner", 2, TestRunnerReporter);
DEFINE_SERVICE(TEST_RUNNER, test_runner_initialize, test_runner_shutdown, SERVICE_PRIORITY_TESTS);

#endif