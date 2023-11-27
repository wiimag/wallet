/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 * 
 * Module containing various Wallet specific ImGui widgets.
 */

#include "imwallet.h"

#include "eod.h"

#include <framework/module.h>
#include <framework/string_table.h>

#define HASH_IMWALLET static_hash_string("imwallet", 8, 0xd34e6a763c92e4d2ULL)

struct imwallet_exchange_t
{
    string_table_symbol_t name;
    string_table_symbol_t code;
    string_table_symbol_t country;
    string_table_symbol_t currency;
};

/*! The module is mostly used to cache data for the widgets 
 *  that do not change much during the course of the application. */
static struct IMWALLET_MODULE
{
    /*! List of stock exchanges. */
    imwallet_exchange_t* exchanges{ nullptr };

} *_imwallet_module;

//
// # IMPLEMENTATION
//

FOUNDATION_STATIC void imwallet_fetch_exchange_list(const json_object_t& json)
{
    size_t exchange_count = json.root->value_length;

    imwallet_exchange_t* exchanges = nullptr;
    array_reserve(exchanges, min(SIZE_C(1), exchange_count));

    for (size_t i = 0; i < exchange_count; ++i)
    {
        imwallet_exchange_t ex{};
        json_object_t ex_data = json[i];

        ex.code = string_table_encode(ex_data["Code"].as_string());
        ex.name = string_table_encode(ex_data["Name"].as_string());
        ex.country = string_table_encode(ex_data["Country"].as_string());
        ex.currency = string_table_encode(ex_data["Currency"].as_string());

        exchanges = array_push(exchanges, ex);
    }

    FOUNDATION_ASSERT(exchanges);

    if (_imwallet_module->exchanges)
        array_deallocate(_imwallet_module->exchanges);
    _imwallet_module->exchanges = exchanges;
}

FOUNDATION_STATIC bool imwallet_ensure_stock_exchanges_loaded()
{
    if (_imwallet_module->exchanges == nullptr)
    {
        // Fetch the list of stock exchanges.
        if (!eod_fetch("exchanges-list", nullptr, FORMAT_JSON_CACHE, imwallet_fetch_exchange_list))
        {
            // Make sure the stock exchange is initialized.
            array_reserve(_imwallet_module->exchanges, 1);
            return false;
        }
    }

    return array_size(_imwallet_module->exchanges) > 0;
}

FOUNDATION_STATIC bool imwallet_exchange_is_selected(const string_t* selected_exchanges, const imwallet_exchange_t* ex)
{
    string_const_t code = string_table_decode_const(ex->code);
    for (unsigned i = 0, end = array_size(selected_exchanges); i != end; ++i)
    {
        const string_t& selected = selected_exchanges[i];
        if (string_equal(STRING_ARGS(selected), STRING_ARGS(code)))
            return true;
    }

    return false;
}

FOUNDATION_STATIC bool imwallet_remove_exchange_from_selection(string_t*& selected_exchanges, const imwallet_exchange_t& ex)
{
    string_const_t code = string_table_decode_const(ex.code);
    for (unsigned i = 0, end = array_size(selected_exchanges); i != end; ++i)
    {
        string_t& exstr = selected_exchanges[i];
        if (string_equal(STRING_ARGS(exstr), STRING_ARGS(code)))
        {
            string_deallocate(exstr.str);
            array_erase(selected_exchanges, i);
            return true;
        }
    }

    return false;
}

//
// # PUBLIC WIDGETS
//

bool ImWallet::Exchanges(string_t*& selected_exchanges)
{
    imwallet_ensure_stock_exchanges_loaded();

    string_t preview{};
    char preview_buffer[64] = { 0 };
    if (_imwallet_module->exchanges == nullptr)
    {
        string_const_t label = RTEXT("Loading...");
        preview = string_copy(STRING_BUFFER(preview_buffer), STRING_ARGS(label));
    }
    else if (array_size(selected_exchanges) == 0)
    {
        string_const_t label = RTEXT("Select stock exchanges");
        preview = string_copy(STRING_BUFFER(preview_buffer), STRING_ARGS(label));
    }
    else
    {
        for (unsigned i = 0, end = array_size(selected_exchanges); i != end; ++i)
        {
            if (i > 0)
                preview = string_concat(STRING_BUFFER(preview_buffer), STRING_ARGS(preview), STRING_CONST(", "));
            preview = string_concat(STRING_BUFFER(preview_buffer), STRING_ARGS(preview), STRING_ARGS(selected_exchanges[i]));
        }
    }

    bool updated = false;
    if (ImGui::BeginCombo("##Exchanges", preview.str, ImGuiComboFlags_None))
    {
        bool focused = false;
        for (unsigned i = 0, end = array_size(_imwallet_module->exchanges); i != end; ++i)
        {
            const imwallet_exchange_t& ex = _imwallet_module->exchanges[i];
            
            string_const_t current_code = string_table_decode_const(ex.code);
            bool selected = imwallet_exchange_is_selected(selected_exchanges, &ex);
            const char* label = string_format_static_const("%.*s (%s)", STRING_FORMAT(current_code), string_table_decode(ex.name));
            if (ImGui::Checkbox(label, &selected))
            {
                if (selected)
                {
                    string_t new_entry = string_clone(STRING_ARGS(current_code));
                    array_push(selected_exchanges, new_entry);
                    updated = true;
                }
                else
                {
                    if (imwallet_remove_exchange_from_selection(selected_exchanges, ex))
                    {
                        updated = true;
                        break;
                    }
                }
            }

            if (!focused && selected)
            {
                ImGui::SetItemDefaultFocus();
                focused = true;
            }
        }
        ImGui::EndCombo();
    }

    return updated;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void imwallet_initialize()
{
    // Initialize the module.
    _imwallet_module = MEM_NEW(HASH_IMWALLET, IMWALLET_MODULE);
}

FOUNDATION_STATIC void imwallet_shutdown()
{
    array_deallocate(_imwallet_module->exchanges);

    // Destroy the module.
    MEM_DELETE(_imwallet_module);
}

DEFINE_MODULE(IMWALLET, imwallet_initialize, imwallet_shutdown, MODULE_PRIORITY_UI);
