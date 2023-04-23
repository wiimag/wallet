/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * This module provides a set of function to translate strings and to manage the
 * localization of the application.
 * 
 * The service module manages a global localization string table that is used to
 * translate strings. The table is loaded from a file and can be reloaded at
 * runtime.
 */

#pragma once

#include <framework/string.h>

#ifndef BUILD_ENABLE_LOCALIZATION
#define BUILD_ENABLE_LOCALIZATION 1
#endif // BUILD_ENABLE_LOCALIZATION

#if BUILD_ENABLE_LOCALIZATION

constexpr const char EVENT_LOCALIZATION_LANGUAGE_CHANGED[] = "LOCALIZATION_LANGUAGE_CHANGE";

extern thread_local size_t _tr_out_size;

/*! @def RTEXT
 * 
 *  @brief Macro to translate a string literal.
 * 
 *  @param str String literal to translate.
 * 
 *  @return Translated constant string object.
 */
#define RTEXT(str) tr((str), sizeof((str)) - 1, true)

/*! Translate a (default English) string to the current user language.
 * 
 *  @note It is safe to cache translate string until the current language changes. 
 *        You can listen to the dispatched event LOCALIZATION_LANG_CHANGED in order to clear any cached string.
 * 
 *  @param str String to translate.
 *  @param length Length of the string.
 *  @param literal If true, the string is a literal.
 *                 Compile time constant string use an hashtable to quickly retrieve the translated string.
 * 
 *  @return Translated constant string object.
 */
string_const_t tr(const char* str, size_t length, bool literal = false);

/*! Translate a string and return the translated string.
 * 
 *  @param str String to translate.
 *  @param length Length of the string.
 * 
 *  @return Translated string object.
 */
const char* tr_cstr(const char* str, size_t length = SIZE_MAX);

#else

#define RTEXT(str) CTEXT(str)

FOUNDATION_FORCEINLINE const char* tr(const char* str, size_t length, bool literal = false) 
{ 
    return string_const(str, length); 
}
#endif

/*! Translate fixed constant string literals.
 * 
 *  @param str String literal to translate.
 *  @template N Length of the string literal.
 * 
 *  @important Only use this function with "fixed" string literals and not any string that is not a compile time constant.
 * 
 *  @return Translated constant string object.
 */
template<size_t N>
FOUNDATION_FORCEINLINE const char* tr(const char(&str)[N])
{
    #if BUILD_ENABLE_LOCALIZATION
        return tr(str, N - 1, true).str;
    #else
        return str;
    #endif
}

/*! Format a string literal using #string_template and translates it.
 *
 *  @param fmt Format string.
 *  @param ... Format arguments.
 *
 *  @return Translated constant string object.
 */
template<typename... Args>
FOUNDATION_FORCEINLINE string_t tr_format(char* buffer, size_t capacity, const char* fmt, Args&&... args)
{
    string_const_t fmttr = tr(fmt, string_length(fmt), false);
    string_t formatted_tr_string = string_template(buffer, capacity, fmttr, std::forward<Args>(args)...);

    return formatted_tr_string;
}

/*! Format a string literal using #string_template and translates it.
 * 
 *  @param fmt Format string.
 *  @param ... Format arguments.
 * 
 *  @return Translated constant string object.
 */
template<typename... Args>
FOUNDATION_FORCEINLINE string_const_t tr_format_static(const char* fmt, Args&&... args)
{
    static thread_local char format_buffer[2048];

    string_const_t fmttr = tr(fmt, string_length(fmt), false);
    string_t formatted_tr_string = string_template(STRING_BUFFER(format_buffer), fmttr, std::forward<Args>(args)...);

    return string_to_const(formatted_tr_string);
}

/*! Format a string literal using #string_template and translates it.
 * 
 *  @remark This function is limited to 2048 characters.
 * 
 *  @important Make sure to use the returned string right away before calling this function again.
 *
 *  @param fmt Format string.
 *  @param ... Format arguments.
 *
 *  @return Translated constant string object.
 */
template<typename... Args>
FOUNDATION_FORCEINLINE const char* tr_format(const char* fmt, Args&&... args)
{
    static thread_local char format_buffer[2048];

    string_const_t fmttr = tr(fmt, string_length(fmt), false);
    string_t formatted_tr_string = string_template(STRING_BUFFER(format_buffer), fmttr, std::forward<Args>(args)...);

    return formatted_tr_string.str;
}

#if BUILD_ENABLE_LOCALIZATION

/*! Returns the current language code. (i.e. "en", "fr", etc.)
 * 
 *  @return Current language code.
 */
string_const_t localization_current_language();


/*! Returns the current language name. (i.e. "English", "Fran\xC3\xA7ais", etc.)
 * 
 *  @return Current language name.
 */
string_const_t localization_current_language_name();

/*! Returns the number of supported languages.
 * 
 *  @return Number of supported languages.
 */
unsigned int localization_supported_language_count();

/*! Returns the language code at the given index.
 * 
 *  @param index Index of the language code to retrieve.
 * 
 *  @return Language code at the given index.
 */
string_const_t localization_language_code(unsigned int index);

/*! Returns the language name at the given index.
 * 
 *  @param index Index of the language name to retrieve.
 * 
 *  @return Language name at the given index.
 */
string_const_t localization_language_name(unsigned int index);

/*! Returns the language code at the given index.
 * 
 *  @param index Index of the language code to retrieve.
 * 
 *  @return Language code at the given index.
 */
bool localization_set_current_language(const char* lang, size_t lang_length);

#endif
