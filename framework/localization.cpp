/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "localization.h"

#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/config.h>
#include <framework/session.h>
#include <framework/array.h>
#include <framework/profiler.h>
#include <framework/dispatcher.h>
#include <framework/system.h>

#include <foundation/path.h>
#include <foundation/environment.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <framework/resource.h>
#include <foundation/windows.h>
#endif

#define HASH_LOCALIZATION static_hash_string("localization", 12, 0xf40f9a08f45a6556ULL)

struct localization_language_t
{
    string_const_t lang;
    string_const_t name;
};

constexpr const char* LOCALIZATION_DEFAULT_LANGUAGE = "en";
const localization_language_t LOCALIZATION_SUPPORTED_LANGUAGES[] = {
    { CTEXT("en"), CTEXT("English") },
    { CTEXT("fr"), CTEXT("Français") },
//  { CTEXT("de"), CTEXT(u8"Deutsch") },
//  { CTEXT("es"), CTEXT(u8"Español") },
//  { CTEXT("it"), CTEXT(u8"Italiano") },
//  { CTEXT("ja"), CTEXT(u8"日本語") },
};

typedef enum class LocaleType {

    None = 0,

    String = 1 << 0,
    Format = 1 << 1,
    Header = 1 << 2,
    
    Image = 1 << 10,

    Missing = 1 << 28,
    Built = 1 << 29,
    Default = 1 << 30

} locale_type_t;
DEFINE_ENUM_FLAGS(LocaleType);

struct string_locale_t
{
    hash_t                  key;
    string_table_symbol_t   symbol;
    locale_type_t           type;
    config_handle_t         cv;
};

struct localization_dictionary_t
{
    char lang[8]{ "en" };
    config_handle_t config{};
    string_table_t* strings{ nullptr };
    string_locale_t* locales{ nullptr };

    bool is_default_language{ false };

    bool config_updated{ false };
};

static struct LOCALIZATION_MODULE
{
    bool build_locales{ false };
    localization_dictionary_t* locales{ nullptr };

} *_localization_module = nullptr;

FOUNDATION_FORCEINLINE int localization_string_locale_key_compare(const string_locale_t& lc, const hash_t& key)
{
    if (lc.key < key)
        return -1;
    if (lc.key > key)
        return 1;
    return 0;
}

FOUNDATION_STATIC string_locale_t* localization_find_string_locale(const localization_dictionary_t* dict, const char* str, size_t length, bool literal, hash_t* out_key = nullptr)
{
    FOUNDATION_ASSERT(dict);

    // TODO: Add quick hashtable access for literals
    const hash_t key = string_hash(str, length);
    if (out_key)
        *out_key = key;
    const int lcidx = array_binary_search_compare(dict->locales, key, localization_string_locale_key_compare);
    if (lcidx < 0)
        return nullptr;

    return dict->locales + lcidx;
}

FOUNDATION_FORCEINLINE string_const_t localization_locale_to_string_const(const localization_dictionary_t* dict, string_locale_t* lc, const char* str, size_t length)
{
    FOUNDATION_ASSERT(lc);
    if (lc->symbol == 0 || test(lc->type, LocaleType::Default))
        return string_const(str, length);
    return string_table_to_string_const(dict->strings, lc->symbol);
}

FOUNDATION_STATIC string_const_t localization_get_locale(const localization_dictionary_t* dict, const char* str, size_t length, bool literal)
{
    FOUNDATION_ASSERT(dict);

    string_locale_t* string_locale = localization_find_string_locale(dict, str, length, literal);
    if (string_locale == nullptr)
        return string_const(str, length);

    return localization_locale_to_string_const(dict, string_locale, str, length);
}

FOUNDATION_STATIC config_handle_t localization_create_locale_config(localization_dictionary_t* dict)
{
    config_handle_t& config = dict->config;
    auto cv_strings = config["strings"];
    FOUNDATION_ASSERT(cv_strings);
    FOUNDATION_ASSERT(config_value_type(cv_strings) == CONFIG_VALUE_ARRAY);
    return config_array_push(cv_strings, CONFIG_VALUE_OBJECT);
}

FOUNDATION_STATIC string_const_t localization_dict_build_locale(localization_dictionary_t* dict, const char* str, size_t length, bool literal)
{
    FOUNDATION_ASSERT(dict);

    string_locale_t lc;
    string_locale_t* string_locale = localization_find_string_locale(dict, str, length, literal, &lc.key);
    if (string_locale != nullptr && none(string_locale->type, LocaleType::Missing))
        return localization_locale_to_string_const(dict, string_locale, str, length);

    if (string_locale && test(string_locale->type, LocaleType::Built))
        return localization_locale_to_string_const(dict, string_locale, str, length);

    lc.cv = string_locale ? string_locale->cv : localization_create_locale_config(dict);

    string_const_t key_hash_string = string_from_uint_static(lc.key, true, 0, 0);
    config_set(lc.cv, "hash", STRING_ARGS(key_hash_string));

    // Insert the key in the dict config
    if (!config_exists(lc.cv, STRING_CONST("en")))
        config_set(lc.cv, "en", str, length);
    if (!dict->is_default_language)
        config_set(lc.cv, dict->lang, STRING_CONST("@TODO"));

    dict->config_updated = true;

    if (string_locale == nullptr)
    {
        // Insert the key in the dict locales
        lc.symbol = 0;
        lc.type = LocaleType::String | LocaleType::Default | LocaleType::Built;

        const int lcidx = ~array_binary_search_compare(dict->locales, lc.key, localization_string_locale_key_compare);
        FOUNDATION_ASSERT(lcidx >= 0);
        array_insert_memcpy_safe(dict->locales, lcidx, &lc);
    }
    else
    {
        string_locale->type |= LocaleType::Built;
    }
    
    return string_const(str, length);
}

FOUNDATION_STATIC config_handle_t localization_locales_new_config()
{
    config_handle_t config = config_allocate(CONFIG_VALUE_OBJECT, CONFIG_OPTION_PRESERVE_INSERTION_ORDER | CONFIG_OPTION_PARSE_UNICODE_UTF8);
    config_set_array(config, STRING_CONST("string"));
    config_set_array(config, STRING_CONST("images"));
    return config;
}

FOUNDATION_STATIC string_locale_t* localization_sort_locales(string_locale_t* locales)
{
    return array_sort(locales, [](const string_locale_t& a, const string_locale_t& b)
    {
        return localization_string_locale_key_compare(a, b.key);
    });
}

FOUNDATION_STATIC string_t localization_build_locales_path()
{
    if (environment_argument("skip-dev-locales"))
        return {};
    #if BUILD_DEVELOPMENT
    // Get the locales.sjson path
    // Look if we can find the locales.sjson in the devs repo
    string_const_t build_path = environment_get_build_path();

    static thread_local char locales_json_path_buffer[BUILD_MAX_PATHLEN];
    string_t locales_json_path = string_copy(STRING_BUFFER(locales_json_path_buffer), STRING_ARGS(build_path));
    locales_json_path = path_append(STRING_ARGS(locales_json_path), BUILD_MAX_PATHLEN, STRING_CONST("../config/locales.sjson"));
    locales_json_path = path_clean(STRING_ARGS(locales_json_path), BUILD_MAX_PATHLEN);
    return locales_json_path;
    #else
    return {};
    #endif
}

FOUNDATION_STATIC string_const_t localization_system_locales_path()
{
    string_t locales_json_path = localization_build_locales_path();
    if (!fs_is_file(STRING_ARGS(locales_json_path)))
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        return system_executable_resource_to_file(MAKEINTRESOURCEA(IDR_LOCALES_SJSON), MAKEINTRESOURCEA(10));
        #else
        static thread_local char locales_json_path_buffer[BUILD_MAX_PATHLEN];
        string_const_t resources_path = environment_get_resources_path();
        
        // Look if we can find the locales.sjson in the same dir as the exe
        locales_json_path = string_copy(STRING_BUFFER(locales_json_path_buffer), STRING_ARGS(resources_path));
        locales_json_path = path_append(STRING_ARGS(locales_json_path), BUILD_MAX_PATHLEN, STRING_CONST("locales.sjson"));
        locales_json_path = path_clean(STRING_ARGS(locales_json_path), BUILD_MAX_PATHLEN);
        #endif
    }

    return string_to_const(locales_json_path);
}

FOUNDATION_STATIC bool localization_save_system_locales(config_handle_t config, const char* path, size_t path_length)
{
    return config_write_file(path, path_length, config, CONFIG_OPTION_PRESERVE_INSERTION_ORDER | CONFIG_OPTION_WRITE_ESCAPE_UTF8);
}

FOUNDATION_STATIC localization_dictionary_t* localization_load_system_locales(string_const_t user_lang = {})
{
    string_const_t locales_json_path = localization_system_locales_path();
    FOUNDATION_ASSERT_MSG(fs_is_file(STRING_ARGS(locales_json_path)), "Could not find locales.sjson");

    const bool has_config_locales = fs_is_file(STRING_ARGS(locales_json_path));

    config_handle_t cv = has_config_locales ? 
        config_parse_file(STRING_ARGS(locales_json_path), CONFIG_OPTION_PRESERVE_INSERTION_ORDER | CONFIG_OPTION_PARSE_UNICODE_UTF8) :
        localization_locales_new_config();
    FOUNDATION_ASSERT(cv);

    localization_dictionary_t* dict = MEM_NEW(HASH_LOCALIZATION, localization_dictionary_t);
    
    dict->config = cv;
    dict->strings = string_table_allocate(64 * 1024, 32);

    // Check if language is specified through the command line
    if (string_is_null(user_lang))
    {
        if (environment_argument("lang", &user_lang, false) && !string_is_null(user_lang))
        {
            user_lang = string_to_const(string_copy(STRING_BUFFER(dict->lang), STRING_ARGS(user_lang)));
        }
        else
        {
            // Get the user language from the session
            user_lang = string_to_const(session_get_string("lang", STRING_BUFFER(dict->lang), "en"));
        }
    }
    else
    {
        user_lang = string_to_const(string_copy(STRING_BUFFER(dict->lang), STRING_ARGS(user_lang)));
    }
    FOUNDATION_ASSERT(!string_is_null(user_lang));

    dict->is_default_language = string_equal_nocase(STRING_ARGS(user_lang), STRING_CONST("en"));

    // Load locale string
    auto strings = cv["strings"];
    for (auto str : strings)
    {
        string_const_t key = str["en"].as_string();
        if (string_is_null(key))
        {
            string_const_t obj_json = str.as_string();
            log_warnf(HASH_LOCALIZATION, WARNING_INVALID_VALUE, STRING_CONST("Missing string key (en) `%.*s`"), STRING_FORMAT(obj_json));
            continue;
        }

        string_locale_t locale;
        locale.key = string_hash(key.str, key.length);
        locale.type = LocaleType::String;
        locale.cv = str;

        if (dict->is_default_language)
        {
            locale.symbol = 0;
            locale.type |= LocaleType::Default;
        }
        else
        {
            string_const_t value = str[user_lang].as_string();
            if (string_is_null(value) || string_equal(STRING_ARGS(value), STRING_CONST("@TODO")))
            {
                locale.type |= LocaleType::Missing;
                locale.symbol = 0;
                log_warnf(HASH_LOCALIZATION, WARNING_INVALID_VALUE,
                    STRING_CONST("Missing language %.*s string value for key `%.*s`"),
                    STRING_FORMAT(user_lang),
                    STRING_FORMAT(key));
            }
            else
            {
                locale.symbol = string_table_to_symbol(dict->strings, value.str, value.length);
            }
        }

        array_push_memcpy(dict->locales, &locale);
    }

    dict->locales = localization_sort_locales(dict->locales);

    return dict;
}

FOUNDATION_STATIC void localization_dictionary_deallocate(localization_dictionary_t*& dict)
{
    array_deallocate(dict->locales);
    config_deallocate(dict->config);
    string_table_deallocate(dict->strings);
    MEM_DELETE(dict);
}

//
// PUBLIC API
//

const char* tr_cstr(const char* str, size_t length /*= SIZE_MAX*/)
{
    if (length == SIZE_MAX)
        length = string_length(str);

    return tr(str, length, false).str;
}

#if BUILD_ENABLE_LOCALIZATION
string_const_t tr(const char* str, size_t length, bool literal /*= false*/)
{
    PERFORMANCE_TRACKER("tr");

    if (str == nullptr || _localization_module == nullptr)
        return string_null();

    if (length == 0)
        length = string_length(str);

    auto dict = _localization_module->locales;
    if (!dict)
        return string_const(str, length);

    #if BUILD_DEVELOPMENT
    if (_localization_module->build_locales)
        return localization_dict_build_locale(dict, str, length, literal);
    #endif

    return localization_get_locale(dict, str, length, literal);
}

string_const_t localization_current_language()
{
    return string_const(_localization_module->locales->lang, string_length(_localization_module->locales->lang));
}

string_const_t localization_current_language_name()
{
    string_const_t locales_lang = localization_current_language();

    for (unsigned i = 0; i < ARRAY_COUNT(LOCALIZATION_SUPPORTED_LANGUAGES); ++i)
    {
        if (string_equal(STRING_ARGS(locales_lang), STRING_ARGS(LOCALIZATION_SUPPORTED_LANGUAGES[i].lang)))
            return LOCALIZATION_SUPPORTED_LANGUAGES[i].name;
    }    

    return CTEXT("Not supported");
}

unsigned int localization_supported_language_count()
{
    return ARRAY_COUNT(LOCALIZATION_SUPPORTED_LANGUAGES);
}

string_const_t localization_language_code(unsigned int index)
{
    FOUNDATION_ASSERT(index <= localization_supported_language_count());
    return LOCALIZATION_SUPPORTED_LANGUAGES[index].lang;
}

string_const_t localization_language_name(unsigned int index)
{
    FOUNDATION_ASSERT(index <= localization_supported_language_count());
    return LOCALIZATION_SUPPORTED_LANGUAGES[index].name;
}

bool localization_set_current_language(const char* lang, size_t lang_length)
{
    string_const_t locales_lang = localization_current_language();
    if (string_equal(STRING_ARGS(locales_lang), lang, lang_length))
        return false;

    locales_lang = string_const(lang, lang_length);
    for (unsigned i = 0; i < ARRAY_COUNT(LOCALIZATION_SUPPORTED_LANGUAGES); ++i)
    {
        if (string_equal(STRING_ARGS(locales_lang), STRING_ARGS(LOCALIZATION_SUPPORTED_LANGUAGES[i].lang)))
        {
            session_set_string("lang", STRING_ARGS(locales_lang));

            // Reload the locales
            localization_dictionary_deallocate(_localization_module->locales);
            _localization_module->locales = localization_load_system_locales(locales_lang);
            return dispatcher_post_event(EVENT_LOCALIZATION_LANGUAGE_CHANGED, (void*)locales_lang.str, locales_lang.length, DISPATCHER_EVENT_OPTION_COPY_DATA);
        }
    }

    return false;
}

string_t localization_string_from_time(char* buffer, size_t capacity, tick_t time, bool since /*= false*/)
{
    time_t ts = (time_t)(time / 1000LL);

    if (since)
        return string_template(buffer, capacity, "{0,since}", ts);

    FOUNDATION_ASSERT(buffer);

    if (capacity < 25) {
        if (capacity)
            buffer[0] = 0;
        return string_from_date(buffer, capacity, ts);
    }

    #if FOUNDATION_PLATFORM_WINDOWS && (FOUNDATION_COMPILER_MSVC || FOUNDATION_COMPILER_INTEL)
        struct tm tm;
        errno_t err = localtime_s(&tm, &ts);
    #elif FOUNDATION_PLATFORM_WINDOWS
        struct tm* gtm = local ? localtime(&ts) : gmtime(&ts);
    #else
        struct tm tm;
        struct tm* gtm = localtime_r(&ts, &tm);
    #endif

    const char* day_name = "Sunday";
    if (tm.tm_wday == 1) day_name = "Monday";
    else if (tm.tm_wday == 2) day_name = "Tuesday";
    else if (tm.tm_wday == 3) day_name = "Wednesday";
    else if (tm.tm_wday == 4) day_name = "Thursday";
    else if (tm.tm_wday == 5) day_name = "Friday";
    else if (tm.tm_wday == 6) day_name = "Saturday";

    const char* month_name = "January";
    if (tm.tm_mon == 1) month_name = "February";
    else if (tm.tm_mon == 2) month_name = "March";
    else if (tm.tm_mon == 3) month_name = "April";
    else if (tm.tm_mon == 4) month_name = "May";
    else if (tm.tm_mon == 5) month_name = "June";
    else if (tm.tm_mon == 6) month_name = "July";
    else if (tm.tm_mon == 7) month_name = "August";
    else if (tm.tm_mon == 8) month_name = "September";
    else if (tm.tm_mon == 9) month_name = "October";
    else if (tm.tm_mon == 10) month_name = "November";
    else if (tm.tm_mon == 11) month_name = "December";        

    if (capacity < 60) {
        return tr_format(buffer, capacity, "{0}-{1,2}-{2,2} {3:H}:{4,2:M}",
            tm.tm_year + 1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    return tr_format(buffer, capacity, "{0,translate:D} {1,translate:M} {2:D} {3:H}:{4,2:M}:{5,2:S} {6:Y}",
        day_name, month_name, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900, tm.tm_mon+1);
}

// 
// MODULE
//

FOUNDATION_STATIC void localization_initialize()
{
    _localization_module = MEM_NEW(HASH_LOCALIZATION, LOCALIZATION_MODULE);

    // Check if we need to build the locales as we load strings
    _localization_module->build_locales = environment_argument("build-locales");

    // Load translation tables
    _localization_module->locales = localization_load_system_locales();

    string_const_t locales_lang = localization_current_language();
    dispatcher_post_event(EVENT_LOCALIZATION_LANGUAGE_CHANGED, (void*)locales_lang.str, locales_lang.length, DISPATCHER_EVENT_OPTION_COPY_DATA);
}

FOUNDATION_STATIC void localization_shutdown()
{
    if (_localization_module->build_locales && _localization_module->locales->config_updated)
    {
        string_t build_locales_path = localization_build_locales_path();
        localization_save_system_locales(_localization_module->locales->config, STRING_ARGS(build_locales_path));
    }

    localization_dictionary_deallocate(_localization_module->locales);
    MEM_DELETE(_localization_module);
}

DEFINE_MODULE(LOCALIZATION, localization_initialize, localization_shutdown, MODULE_PRIORITY_BASE);

#endif
