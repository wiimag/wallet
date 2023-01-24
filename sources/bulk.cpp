#include "bulk.h"

#include "eod.h"
#include "pattern.h"
#include "settings.h"

#include "framework/config.h"
#include "framework/session.h"
#include "framework/imgui_utils.h"
#include "framework/scoped_mutex.h"
#include "framework/table.h"

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/mutex.h>

#include <imgui/imgui.h>
#include <imgui/imgui_date_chooser.h>

#include <time.h>

static exchange_t* _exchanges = nullptr;
static const exchange_t** _selected_exchanges = nullptr;
static time_t _fetch_date = time_work_day(time_now(), -0.7);
static tm _fetch_date_tm = *_localtime64(&_fetch_date);

static mutex_t* _symbols_lock;
static bulk_t* _symbols = nullptr;
static table_t* _symbols_table = nullptr;

static bool _fetch_cap_zero{ false };
static bool _fetch_volume_zero{ false };
static bool _fetch_negative_beta{ false };

static void bulk_fetch_exchange_list(const json_object_t& json)
{
	size_t exchange_count = json.root->value_length;

	if (exchange_count == 0)
	{
		array_reserve(_exchanges, 1);
		return;
	}

	exchange_t* exchanges = nullptr;
	for (int i = 0; i < exchange_count; ++i)
	{
		exchange_t ex{};
		json_object_t ex_data = json[i];

		ex.code = string_table_encode(ex_data["Code"].as_string());
		ex.name = string_table_encode(ex_data["Name"].as_string());
		ex.country = string_table_encode(ex_data["Country"].as_string());
		ex.currency = string_table_encode(ex_data["Currency"].as_string());

		exchanges = array_push(exchanges, ex);
	}

	// Add some missing markets
	exchanges = array_push(exchanges, (exchange_t{
		string_table_encode("Toronto Venture"), // Name
		string_table_encode("V"),	            // Code
		string_table_encode("Canada"),          // Country
		string_table_encode("CAD"),             // Currency
	}));

	if (_exchanges)
		array_deallocate(_exchanges);
	_exchanges = exchanges;
}

static bool bulk_add_symbols(const bulk_t* batch)
{
	if (auto lock = scoped_mutex_t(_symbols_lock))
	{
		size_t bz = array_size(batch);
		size_t cz = array_size(_symbols);
		array_resize(_symbols, cz + bz);
		memcpy(_symbols + cz, batch, sizeof(bulk_t) * bz);
		return true;
	}
	return false;
}

static void bulk_fetch_exchange_symbols(const json_object_t& json)
{
	if (json.root->value_length == 0)
		return;

	bulk_t* batch = nullptr;
	for (int i = 0, end = json.root->value_length; i != end; ++i)
	{
		json_object_t e = json[i];
		bulk_t s{};
		s.market_capitalization = e["MarketCapitalization"].as_number();
		if (s.market_capitalization == 0 && !_fetch_cap_zero)
			continue;

		s.volume = e["volume"].as_number();
		s.avgvol_200d = e["avgvol_200d"].as_number();
		if (s.avgvol_200d == 0 && s.volume == 0 && !_fetch_volume_zero)
			continue;

		s.beta = e["Beta"].as_number();
		if (s.beta < 0.01 && !_fetch_negative_beta)
			continue;

		s.avgvol_14d = e["avgvol_14d"].as_number();
		s.avgvol_50d = e["avgvol_50d"].as_number();

		string_const_t code = e["code"].as_string();
		string_const_t ex = e["exchange_short_name"].as_string();
		code = string_format_static(STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(ex));

		s.date = string_to_date(STRING_ARGS(e["date"].as_string()));
		s.code = string_table_encode(code);
		s.name = string_table_encode_unescape(e["name"].as_string());
		s.type = string_table_encode(e["type"].as_string());
		s.exchange = string_table_encode(ex);

		s.open = e["open"].as_number();
		s.high = e["high"].as_number();
		s.low = e["low"].as_number();
		s.close = e["close"].as_number();
		s.adjusted_close = e["adjusted_close"].as_number();
		s.ema_50d = e["ema_50d"].as_number();
		s.ema_200d = e["ema_200d"].as_number();
		s.hi_250d = e["hi_250d"].as_number();
		s.lo_250d = e["lo_250d"].as_number();

		s.selected = pattern_find(STRING_ARGS(code)) >= 0;

		batch = array_push(batch, s);
		if (array_size(batch) > 999)
		{
			if (bulk_add_symbols(batch))
				array_clear(batch);
		}
	}

	bulk_add_symbols(batch);
	array_deallocate(batch);
}

static void bulk_load_symbols()
{
	if (auto lock = scoped_mutex_t(_symbols_lock))
		array_clear(_symbols);

	for (int i = 0, end = array_size(_selected_exchanges); i != end; ++i)
	{
		const exchange_t* ex = _selected_exchanges[i];
		const char* code = string_table_decode(ex->code);
		if (!eod_fetch_async("eod-bulk-last-day", code, FORMAT_JSON_CACHE, 
			"date", string_from_date(_fetch_date).str,
			"filter", "extended", bulk_fetch_exchange_symbols, 12 * 60 * 60ULL))
		{
			log_errorf(0, ERROR_ACCESS_DENIED, STRING_CONST("Failed to fetch %s bulk data"), code);
		}
	}
}

static string_const_t bulk_get_symbol_code(const bulk_t* b)
{
	return string_table_decode_const(b->code);
}

static cell_t bulk_column_symbol_code(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return bulk_get_symbol_code(b); 
}

static cell_t bulk_column_symbol_name(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->name;
}

static cell_t bulk_column_symbol_date(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->date;
}

static cell_t bulk_column_symbol_type(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->type;
}

static cell_t bulk_column_symbol_exchange(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->exchange;
}

static void bulk_column_today_cap_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
	bulk_t* b = (bulk_t*)element;

	if (!b->today_cap)
	{
		string_const_t code = bulk_get_symbol_code(b);
		if (stock_update(STRING_ARGS(code), b->stock_handle, FetchLevel::EOD))
		{
			size_t n = 0;
			double a = 0;
			const time_t today = time_now();
			const day_result_t* history = b->stock_handle->history;
			while (n < array_size(history) && time_elapsed_days(history[n].date, today) <= 14.0)
			{
				a += history[n].volume * (history[n].close - history[n].open);
				n++;
			}
			b->today_cap = a / n;
		}
	}

	ImGui::Text("Average capitalization movement since 14 days\n%.*s", STRING_FORMAT(string_from_currency(b->today_cap.fetch(), "9 999 999 999 $")));
}

static cell_t bulk_column_today_cap(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->avgvol_14d * (b->close - b->open);
}

static cell_t bulk_column_symbol_cap(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->market_capitalization;
}

static cell_t bulk_draw_symbol_beta(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->beta * 100.0;
}

static cell_t bulk_draw_symbol_open(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->open;
}

static cell_t bulk_draw_symbol_close(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->adjusted_close;
}

static cell_t bulk_draw_symbol_low(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->low;
}

static cell_t bulk_draw_symbol_high(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->high;
}

static cell_t bulk_draw_symbol_volume(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->volume;
}

static cell_t bulk_draw_symbol_ema_50d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->ema_50d;
}

static cell_t bulk_draw_symbol_ema_p(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return (b->ema_50d - b->adjusted_close) / b->close * 100.0;
}

static cell_t bulk_draw_symbol_change_p(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return (b->close - b->open) / b->open * 100.0;
}

static cell_t bulk_draw_symbol_lost_cap(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->market_capitalization * bulk_draw_symbol_change_p(element, column).number / 100.0;
}

static cell_t bulk_draw_symbol_ema_200d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->ema_200d;
}

static cell_t bulk_draw_symbol_lo_250d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->lo_250d;
}

static cell_t bulk_draw_symbol_hi_250d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->hi_250d;
}

static cell_t bulk_draw_symbol_avgvol_14d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->avgvol_14d;
}

static cell_t bulk_draw_symbol_avgvol_50d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->avgvol_50d;
}

static cell_t bulk_draw_symbol_avgvol_200d(table_element_ptr_t element, const column_t* column)
{
	bulk_t* b = (bulk_t*)element;
	return b->avgvol_200d;
}

static void bulk_table_context_menu(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
	if (element == nullptr)
		return ImGui::CloseCurrentPopup();

	bulk_t* b = (bulk_t*)element;

	if (ImGui::MenuItem("Load Pattern"))
	{
		string_const_t code = bulk_get_symbol_code(b);
		pattern_open(STRING_ARGS(code));
	}
}

static void bulk_column_title_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
	bulk_t* b = (bulk_t*)element;
	string_const_t code = bulk_get_symbol_code(b);
	pattern_open(STRING_ARGS(code));
	b->selected = true;
}

static void bulk_draw_symbol_code_color(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
	bulk_t* b = (bulk_t*)element;
	if (b->selected || (b->beta > 1 && b->close > b->open))
	{
		style.types |= COLUMN_COLOR_TEXT;
		style.text_color = ImColor::HSV(!b->selected ? 0.4f : 0.6f, 0.3f, 0.9f);
	}
}

void bulk_set_beta_styling(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
	bulk_t* b = (bulk_t*)element;
	if (b->beta > 1)
	{
		style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
		style.text_color = ImColor(0.051f, 0.051f, 0.051f);
		style.background_color = ImColor(218 / 255.0f, 234 / 255.0f, 210 / 255.0f);
	}
}

static bool bulk_table_search(table_element_ptr_const_t element, const char* filter, size_t filter_length)
{
	bulk_t* b = (bulk_t*)element;

	string_const_t code = bulk_get_symbol_code(b);
	if (string_contains_nocase(STRING_ARGS(code), filter, filter_length))
		return true;

	string_const_t name = string_table_decode_const(b->name);
	if (string_contains_nocase(STRING_ARGS(name), filter, filter_length))
		return true;

	return false;
}

static void bulk_create_symbols_table()
{
	if (_symbols_table)
		table_deallocate(_symbols_table);

	_symbols_table = table_allocate("Bulk##_2");
	_symbols_table->flags |= TABLE_HIGHLIGHT_HOVERED_ROW;
	_symbols_table->context_menu = bulk_table_context_menu;
	_symbols_table->search = bulk_table_search;

	table_add_column(_symbols_table, STRING_CONST("Title"), bulk_column_symbol_code, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE/* | COLUMN_FREEZE*/)
		.set_style_formatter(bulk_draw_symbol_code_color)
		.set_selected_callback(bulk_column_title_selected);

	table_add_column(_symbols_table, STRING_CONST("Name"), bulk_column_symbol_name, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST("Date"), bulk_column_symbol_date, COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST("Type"), bulk_column_symbol_type, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE);
	table_add_column(_symbols_table, STRING_CONST("Ex.||Exchange"), bulk_column_symbol_exchange, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN);

	table_add_column(_symbols_table, STRING_CONST(ICON_MD_EXPAND " Cap.||Moving Capitalization"), bulk_column_today_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT)
		.set_tooltip_callback(bulk_column_today_cap_tooltip);

	table_add_column(_symbols_table, STRING_CONST("  Cap.||Capitalization"), bulk_column_symbol_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION);
	table_add_column(_symbols_table, STRING_CONST("Lost Cap.||Lost Capitalization"), bulk_draw_symbol_lost_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);

	table_add_column(_symbols_table, STRING_CONST("  Beta||Beta"), bulk_draw_symbol_beta, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE)
		.set_style_formatter(bulk_set_beta_styling);

	table_add_column(_symbols_table, STRING_CONST("    Open||Open"), bulk_draw_symbol_open, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
	table_add_column(_symbols_table, STRING_CONST("   Close||Close"), bulk_draw_symbol_close, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
	table_add_column(_symbols_table, STRING_CONST("     Low||Low"), bulk_draw_symbol_low, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
	table_add_column(_symbols_table, STRING_CONST("    High||High"), bulk_draw_symbol_high, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);

	table_add_column(_symbols_table, STRING_CONST("    %||Day Change"), bulk_draw_symbol_change_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE);
	table_add_column(_symbols_table, STRING_CONST("EMA %||Exponential Moving Averages Gain"), bulk_draw_symbol_ema_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE);

	table_add_column(_symbols_table, STRING_CONST("EMA 50d||Exponential Moving Averages (50 days)"), bulk_draw_symbol_ema_50d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST("EMA 200d||Exponential Moving Averages (200 days)"), bulk_draw_symbol_ema_200d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST(" L. 250d||Low 250 days"), bulk_draw_symbol_lo_250d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST(" H. 250d||High 250 days"), bulk_draw_symbol_hi_250d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);

	table_add_column(_symbols_table, STRING_CONST("Volume"), bulk_draw_symbol_volume, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION);
	table_add_column(_symbols_table, STRING_CONST("V. 14d||Average Volume 14 days"), bulk_draw_symbol_avgvol_14d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION);
	table_add_column(_symbols_table, STRING_CONST("V. 50d||Average Volume 50 days"), bulk_draw_symbol_avgvol_50d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);
	table_add_column(_symbols_table, STRING_CONST("V. 200d||Average Volume 200 days"), bulk_draw_symbol_avgvol_200d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);
}

static void bulk_initialize_exchanges()
{
	if (!eod_fetch("exchanges-list", nullptr, FORMAT_JSON_CACHE, bulk_fetch_exchange_list))
		return;

	string_const_t selected_exchanges_file_path = session_get_user_file_path(STRING_CONST("exchanges.json"));
	if (fs_is_file(STRING_ARGS(selected_exchanges_file_path)))
	{
		int code_hash = 0;
		config_handle_t selected_exchanges_data = config_parse_file(STRING_ARGS(selected_exchanges_file_path));
		for (auto p : selected_exchanges_data)
		{
			string_table_symbol_t code_symbol = string_table_encode(p.as_string());
			for (int i = 0, end = array_size(_exchanges); i != end; ++i)
			{
				if (code_symbol == _exchanges[i].code)
					_selected_exchanges = array_push(_selected_exchanges, &_exchanges[i]);
			}
		}

		config_deallocate(selected_exchanges_data);
	}

	if (_symbols == nullptr)
		bulk_load_symbols();

	if (_symbols_table == nullptr)
		bulk_create_symbols_table();
}

static bool bulk_exchange_is_selected(const exchange_t* ex)
{
	for (int i = 0, end = array_size(_selected_exchanges); i != end; ++i)
	{
		if (ex == _selected_exchanges[i])
			return true;
	}

	return false;
}

bool bulk_render_exchange_selector()
{
	bool updated = false;
	char preview_buffer[64]{ '\0' };
	string_t preview{ preview_buffer, 0 };

	size_t selected_exchanges_count = array_size(_selected_exchanges);
	if (selected_exchanges_count == 0)
	{
		string_copy(STRING_CONST_CAPACITY(preview_buffer), STRING_CONST("None"));
	}
	else
	{
		for (int i = 0, end = array_size(_selected_exchanges); i != end; ++i)
		{
			if (i > 0)
			{
				preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_CONST(", "));
			}
			string_const_t ex_code = string_table_decode_const(_selected_exchanges[i]->code);
			preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_ARGS(ex_code));
		}
	}

	ImGui::SameLine();
	ImGui::MoveCursor(0, -2);
	ImGui::SetNextItemWidth(400.0f);
	if (ImGui::BeginCombo("##Exchange", preview.str, ImGuiComboFlags_None))
	{
		bool focused = false;
		for (int i = 0, end = array_size(_exchanges); i != end; ++i)
		{
			const exchange_t& ex = _exchanges[i];

			bool selected = bulk_exchange_is_selected(&ex);
			string_const_t ex_id = string_format_static(STRING_CONST("%s (%s)"), string_table_decode(ex.code), string_table_decode(ex.name));
			if (ImGui::Checkbox(ex_id.str, &selected))
			{
				if (selected)
					_selected_exchanges = array_push(_selected_exchanges, &ex);
				else
				{
					for (int i = 0, end = array_size(_selected_exchanges); i != end; ++i)
					{
						if (&ex == _selected_exchanges[i])
						{
							array_erase(_selected_exchanges, i);
							break;
						}
					}
				}

				updated = true;
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

void bulk_render()
{
	if (_exchanges == nullptr)
		bulk_initialize_exchanges();

	ImGui::MoveCursor(8, 8);
	ImGui::BeginGroup();
	ImGui::MoveCursor(0, -2);
	ImGui::TextUnformatted("Exchanges");
		
	bool exchanges_updated = bulk_render_exchange_selector();	
	
	ImGui::MoveCursor(0, -2, true);
	ImGui::SetNextItemWidth(300.0f);
	if (ImGui::DateChooser("##Date", _fetch_date_tm, "%Y-%m-%d", true))
	{
		_fetch_date = _mktime64(&_fetch_date_tm);
		exchanges_updated = true;
	}

	ImGui::MoveCursor(0, -2, true);
	if (ImGui::Checkbox("No capitalization", &_fetch_cap_zero))
		exchanges_updated = true;

	ImGui::MoveCursor(0, -2, true);
	if (ImGui::Checkbox("No Volume", &_fetch_volume_zero))
		exchanges_updated = true;

	ImGui::MoveCursor(0, -2, true);
	if (ImGui::Checkbox("No Beta", &_fetch_negative_beta))
		exchanges_updated = true;

	if (exchanges_updated)
		bulk_load_symbols();

	if (_symbols_table)
	{
		int symbol_count = array_size(_symbols);
		ImGui::MoveCursor(0, -2, true);
		ImGui::Text("       %d symbols", symbol_count);
		ImGui::EndGroup();

		if (auto lock = scoped_mutex_t(_symbols_lock))
		{
			_symbols_table->search_filter = string_to_const(SETTINGS.search_filter);
			table_render(_symbols_table, _symbols, symbol_count, sizeof(bulk_t), 0.0f, 0.0f);
		}
	}
}

void bulk_initialize()
{
	_symbols_lock = mutex_allocate(STRING_CONST("BulkLock"));
}

void bulk_shutdown()
{
	if (_selected_exchanges)
	{
		string_const_t selected_exchanges_file_path = session_get_user_file_path(STRING_CONST("exchanges.json"));
		config_write_file(selected_exchanges_file_path, [](config_handle_t selected_exchange_data)
		{
			const size_t selected_exchange_count = array_size(_selected_exchanges);
			for (int i = 0; i < selected_exchange_count; ++i)
			{
				const exchange_t* ex = _selected_exchanges[i];
				config_array_push(selected_exchange_data, STRING_ARGS(string_table_decode_const(ex->code)));
			}
			return true;
		}, CONFIG_VALUE_ARRAY);
	}

	table_deallocate(_symbols_table);
	array_deallocate(_selected_exchanges);
	array_deallocate(_exchanges);
	array_deallocate(_symbols);

	mutex_deallocate(_symbols_lock);
	_symbols_lock = nullptr;
}
