/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
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

/*! @def TSTRING_CONST
 * 
 *  @brief Macro to translate a string literal and unpack the resulted constant string 
 *         for function that takes str and length as two separated arguments.
 * 
 *  @param str String literal to translate.
 * 
 *  @return Translated constant string object and length.
 */
#define TSTRING_CONST(str) tr_out_size((str), sizeof((str)) - 1, true, &_tr_out_size), _tr_out_size

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

/* @internal 
 * @see TSTRING_CONST
 */
FOUNDATION_FORCEINLINE const char* tr_out_size(const char* str, size_t length, bool literal, size_t& out_size)
{
    string_const_t result = tr(str, length, literal);
    out_size = result.length;
    return result.str;
}

#else

#define RTEXT(str) CTEXT(str)
#define TSTRING_CONST(str) STRING_CONST(str)

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL const char* tr(const char* str, size_t length, bool literal = false) 
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
