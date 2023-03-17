/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Example how to run tests: 
 *      > ./build/<app>.exe --run-tests & cat artifacts/tests.log
 *      > ./build/<app>.exe --run-tests --minimal=false --duration=true && n artifacts/tests.log
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/glfw.h>

#include <foundation/array.h>
#include <foundation/assert.h>
#include <foundation/environment.h>
#include <foundation/fs.h>
#include <foundation/hashstrings.h>
#include <foundation/path.h>

#define _UUID_T
#define DOCTEST_CONFIG_IMPLEMENT
#if BUILD_RELEASE
    #define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#endif
#include <doctest/doctest.h>

#include <ostream>

extern GLFWwindow* _test_window = nullptr;

struct TestReporter : public doctest::IReporter
{
    const doctest::TestCaseData* tc;
    const doctest::SubcaseSignature* sc;
    const doctest::ContextOptions& opt;

    TestReporter(const doctest::ContextOptions& in)
        : opt(in)
        , tc(nullptr)
    {
    }

    void report_query(const doctest::QueryData& /*in*/) override
    {
        log_debug(HASH_TEST, STRING_CONST("report_query"));
    }

    void test_run_start() override
    {
        log_infof(HASH_TEST, STRING_CONST("\n\n\t\t========> Running %zu tests...\n\n"), doctest::getRegisteredTests().size());
    }

    void test_run_end(const doctest::TestRunStats& in) override
    {
        log_infof(HASH_TEST, STRING_CONST("\n\n\t\t<======== Running tests finished [Cases %u/%u, Successes %u/%u]\n\n"),
            in.numTestCasesPassingFilters - in.numTestCasesFailed, in.numTestCasesPassingFilters,
            in.numAsserts - in.numAssertsFailed, in.numAsserts);
    }

    void test_case_start(const doctest::TestCaseData& in) override
    {
        log_infof(HASH_TEST, STRING_CONST("+--- BEGIN %s::%s (%s)"), in.m_test_suite, 
            string_length(in.m_name) ? in.m_name : string_from_int_static(in.m_line, 0, 0).str, 
            in.m_description ? in.m_description : "...");
        tc = &in;
    }

    void test_case_reenter(const doctest::TestCaseData& in) override
    {
        test_case_start(in);
    }

    void test_case_end(const doctest::CurrentTestCaseStats& in) override
    {
        string_const_t file_name = string_const(tc->m_file.c_str(), tc->m_file.size());
        file_name = path_file_name(STRING_ARGS(file_name));
        log_infof(HASH_TEST, STRING_CONST("|----- END %s::%s (%s) took %.3lg seconds\n"), 
            tc->m_test_suite, 
            string_length(tc->m_name) ? tc->m_name : string_from_int_static(tc->m_line, 0, 0).str,
            tc->m_description ? tc->m_description : file_name.str, in.seconds);

        tc = nullptr;
        TEST_CLEAR_FRAME();
    }

    void test_case_exception(const doctest::TestCaseException& /*in*/) override
    {
        log_debug(HASH_TEST, STRING_CONST("test_case_exception"));
    }

    void subcase_start(const doctest::SubcaseSignature& in) override
    {
        log_infof(HASH_TEST, STRING_CONST("    +--- BEGIN SUB %s::%s::%s"), tc->m_test_suite,
            string_length(tc->m_name) ? tc->m_name : string_from_int_static(tc->m_line, 0, 0).str,
            in.m_name.c_str());
        sc = &in;
    }

    void subcase_end() override
    {
        log_infof(HASH_TEST, STRING_CONST("    |----- END SUB %s::%s::%s"), tc->m_test_suite,
            string_length(tc->m_name) ? tc->m_name : string_from_int_static(tc->m_line, 0, 0).str,
            sc->m_name.c_str());
        sc = nullptr;
    }

    void log_assert(const doctest::AssertData& in) override
    {
        // don't include successful asserts by default - this is done here
        // instead of in the framework itself because doctest doesn't know
        // if/when a reporter/listener cares about successful results
        if (!in.m_failed && !opt.success)
            return;

        // make sure there are no races - this is done here instead of in the
        // framework itself because doctest doesn't know if reporters/listeners
        // care about successful asserts and thus doesn't lock a mutex unnecessarily
        if (!opt.success)
        {
            log_errorf(HASH_TEST, ERROR_ASSERT, STRING_CONST("Failed to test `%s`\n\t%s(%d): %s"),
                in.m_expr,
                in.m_file, in.m_line,
                in.m_exception.size() ? in.m_exception.c_str() : "<empty>");
        }
        else
        {
            string_const_t file_name = string_const(tc->m_file.c_str(), tc->m_file.size());
            file_name = path_file_name(STRING_ARGS(file_name));
            log_infof(HASH_TEST, STRING_CONST("|--------- %s(%d) => `%s` => %s"),
                file_name.str, in.m_line,
                in.m_expr,
                in.m_decomp.c_str());
        }
    }

    void log_message(const doctest::MessageData& in) override
    {
        int level = get_num_active_contexts();
        if (in.m_severity & doctest::assertType::is_require)
        {
            log_errorf(HASH_TEST, ERROR_EXCEPTION, STRING_CONST("%s|--------- %s::%s::%s -> %s"),
                padding(level, "\t\t"),
                tc->m_test_suite, tc->m_name,
                doctest::failureString(in.m_severity),
                in.m_string.c_str());
        }
        else
        {
            log_infof(HASH_TEST, STRING_CONST("%s|--------- %s::%s::%s -> %s"),
                padding(level, "\t\t"),
                tc->m_test_suite, tc->m_name,
                doctest::failureString(in.m_severity),
                in.m_string.c_str());
        }
    }

    void test_case_skipped(const doctest::TestCaseData& /*in*/) override
    {
        //log_debug(HASH_TEST, STRING_CONST("test_case_skipped"));
    }

    const char* padding(int level, const char* pattern)
    {
        if (level <= 0)
            return "";

        const size_t pattern_length = string_length(pattern);
        static char filling_buffer[32] = {'\0'};
        string_t filling = string_copy(STRING_BUFFER(filling_buffer), pattern, pattern_length);
        for (int i = 1; i < level; ++i)
        {
            filling = string_concat(STRING_BUFFER(filling_buffer),
                STRING_ARGS(filling), pattern, pattern_length);
        }

        if (filling.length)
            return filling.str;
        return "";
    }
};

extern int main_tests(void* _context, GLFWwindow* window)
{
    doctest::Context context;

    // See https://github.com/doctest/doctest/blob/master/doc/markdown/commandline.md
    context.setOption("abort-after", 5);
    context.setOption("reporters", "console,foundation");
    context.setOption("no-intro", "true");
    context.setOption("no-version", "true");
    context.setOption("duration", "true");
    context.setOption("minimal", "false");
    context.setOption("no-path-filenames", "true");
    context.setOption("no-debug-output", "true");

    if (!environment_command_line_arg("out"))
    {
        char test_log_path_buffer[BUILD_MAX_PATHLEN];
        string_const_t exe_dir = environment_executable_directory();
        string_t test_log_path = path_concat(STRING_BUFFER(test_log_path_buffer),
            STRING_ARGS(exe_dir), 
        #if FOUNDATION_PLATFORM_WINDOWS
            STRING_CONST("../artifacts/tests.log"));
        #elif FOUNDATION_PLATFORM_MACOS
            STRING_CONST("../../../../artifacts/tests.log"));
        #else
            #error Platform not supported
        #endif
        test_log_path = path_clean(STRING_ARGS(test_log_path), BUILD_MAX_PATHLEN);
        string_const_t test_log_dir = path_directory_name(STRING_ARGS(test_log_path));

        if (fs_is_directory(STRING_ARGS(test_log_dir)))
            context.setOption("out", test_log_path.str);
        else
            log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Missing artifacts folder `%.*s`"), STRING_FORMAT(test_log_dir));
    }

    context.setAssertHandler([](const doctest::AssertData& in)
    {
        FOUNDATION_ASSERT_MSGFORMAT(in.m_failed, "%.*s",
            (int)in.m_exception.size(), in.m_exception.c_str());
    });

    size_t iarg, argsize;
    const char** argv = nullptr;
    const string_const_t* cmdline = environment_command_line();
    for (iarg = 0, argsize = array_size(cmdline); iarg < argsize; ++iarg)
        array_push(argv, cmdline[iarg].str);
    context.applyCommandLine((int)argsize, argv);
    array_deallocate(argv);

    #if BUILD_RELEASE
        // Don't break in the debugger when assertions fail
        context.setOption("no-breaks", true);             
    #endif

    _test_window = window;
    int res = context.run();
    if (context.shouldExit())
        return res;
    return res;
}

REGISTER_REPORTER("foundation", 1, TestReporter);

#endif
