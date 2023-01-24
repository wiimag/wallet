#define EXPR_IMPLEMENTATION

#if 0

#include "eval.h"

#include "eod.h"
#include "stock.h"
#include "report.h"
#include "title.h"

#include "framework/common.h"
#include "framework/imgui_utils.h"
#include "framework/session.h"

#include <foundation/string.h>

#include <imgui/imgui.h>
#include <imgui/implot.h>

#include <expr.h>

#ifdef FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#pragma comment( lib, "ws2_32" )
#include <WinSock2.h>
#endif

#include <easywsclient/easywsclient.hpp>

struct expr_result_t const expr_result_t::NIL{};

static thread_local expr_result_t NIL{};
static thread_local expr_var_list_t _global_vars = { 0 };
static thread_local const expr_result_t** _expr_lists = nullptr;

static easywsclient::WebSocket* _ws_client = nullptr;
static evaluator_t* _evaluators = nullptr;

string_const_t expr_result_t::as_string(const char* fmt /*= nullptr*/) const
{
	if (type == EXPR_RESULT_NUMBER)
	{
		if (fmt)
			return string_format_static(fmt, string_length(fmt), value);
		return string_from_real_static(value, 0, 0, 0);
	}
	if (type == EXPR_RESULT_TRUE)
		return CTEXT("true");
	if (type == EXPR_RESULT_FALSE)
		return CTEXT("false");
	if (type == EXPR_RESULT_SYMBOL)
		return string_table_decode_const(math_trunc(value));

	if (type == EXPR_RESULT_ARRAY)
		return string_join<const expr_result_t>(list, 
			[fmt](const expr_result_t& e){ return e.as_string(fmt); }, 
			CTEXT(", "), CTEXT("["), CTEXT("]"));

	return string_null();
}

static const expr_result_t eval_symbol(string_table_symbol_t symbol)
{
	expr_result_t r(EXPR_RESULT_SYMBOL);
	r.value = (double)symbol;
	return r;
}

static const expr_result_t* eval_list(const expr_result_t* list)
{
	if (list)
		array_push(_expr_lists, list);
	return list;
}

expr_result_t expr_eval_set(expr_t* e)
{
	expr_result_t* resolved_values = nullptr;
	
	for (int i = 0; i < e->args.len; ++i)
	{
		expr_result_t r = expr_eval(&e->args.buf[i]);
		array_push(resolved_values, r);
	}

	return eval_list(resolved_values);
}

static expr_result_t eval_pair(const expr_result_t& key, const expr_result_t& value)
{
	expr_result_t* kvp = nullptr;
	array_push(kvp, key);
	array_push(kvp, value);
	if (kvp)
		array_push(_expr_lists, kvp);
	return expr_result_t(kvp, 1ULL);
}

static expr_result_t eval_tuple(const expr_result_t& key, size_t value_count, ...)
{
	if (value_count == 0)
		return nullptr;

	expr_result_t* kvp = nullptr;
	array_push(kvp, key);

	va_list args;
	va_start(args, value_count);
	for (size_t i = 0; i < value_count; ++i)
	{ 
		const expr_result_t& value = va_arg(args, expr_result_t);
		array_push(kvp, value);
	}
	va_end(args);

	if (kvp)
		array_push(_expr_lists, kvp);

	return expr_result_t(kvp, value_count-1);
}

static string_const_t eval_get_string_arg(const vec_expr_t* args, size_t idx, const char* message)
{
	if (idx >= args->len)
		throw EvalError(EXPR_ERROR_INVALID_ARGUMENT, CTEXT("Missing argument"), message);

	const expr_t& arg = vec_nth(args, idx);
	if (arg.type != OP_VAR || arg.token.length == 0)
		throw EvalError(EXPR_ERROR_INVALID_ARGUMENT, arg.token, message);

	return arg.token;
}

static string_const_t eval_get_string_arg_replace_sharp(const vec_expr_t* args, size_t idx, const char* message)
{
	const auto& code = eval_get_string_arg(args, idx, message);

	string_t code_buffer = string_static_buffer(code.length+1);
	return string_to_const(string_replace(
		STRING_ARGS(string_copy(STRING_ARGS(code_buffer), STRING_ARGS(code))), code_buffer.length,
		STRING_CONST("#"), STRING_CONST("."), false));
}

static string_const_t eval_get_string_copy_arg(const vec_expr_t* args, size_t idx, const char* message)
{
	const auto& arg_string = eval_get_string_arg(args, idx, message);

	string_t arg_buffer = string_static_buffer(arg_string.length+1);
	return string_to_const(string_copy(STRING_ARGS(arg_buffer), STRING_ARGS(arg_string)));
}

static void eval_read_object_fields(const json_object_t& json, const json_token_t* obj, expr_result_t** field_names, const char* s = nullptr, size_t len = 0)
{
	unsigned int e = obj->child;
	for (size_t i = 0; i < obj->value_length; ++i)
	{
		const json_token_t* t = &json.tokens[e];
		e = t->sibling;
		if (t->id_length == 0)
			continue;

		char path[256];
		size_t path_length = 0;
		if (s && len > 0)
			path_length = string_format(STRING_CONST_CAPACITY(path), STRING_CONST("%.*s.%.*s"), len, s, t->id_length, &json.buffer[t->id]).length;
		else
			path_length = string_copy(STRING_CONST_CAPACITY(path), &json.buffer[t->id], t->id_length).length;

		if (t->type == JSON_OBJECT)
			eval_read_object_fields(json, t, field_names, path, path_length);
		else
		{
			expr_result_t s;
			s.type = EXPR_RESULT_SYMBOL;
			s.value = string_table_encode(path, path_length);
			array_push(*field_names, s);
		}
	}
}

static expr_result_t eval_stock_realtime(const expr_func_t* f, vec_expr_t* args, void* c)
{
	const auto& code = eval_get_string_copy_arg(args, 0, "Invalid symbol code");
	const auto& field = eval_get_string_arg(args, 1, "Invalid field name");
	
	double value = NAN;
	eod_fetch("real-time", code.str, FORMAT_JSON_CACHE, [field, &value](const json_object_t& json)
	{
		value = json[field].as_number();			
	}, 10ULL * 60ULL);

	return value;
}

static expr_result_t eval_list_fields(const expr_func_t* f, vec_expr_t* args, void* c)
{
	const auto& code = eval_get_string_arg(args, 0, "Invalid symbol code");
	const auto& api = eval_get_string_arg(args, 1, "Invalid API end-point");

	expr_result_t* field_names = nullptr;
	string_const_t url = eod_build_url(api.str, code.str, FORMAT_JSON_CACHE);
	query_execute_json(url.str, FORMAT_JSON_CACHE, [&field_names](const json_object_t& json)
	{
		if (json.root == nullptr)
			return;

		if (json.root->type == JSON_OBJECT)
			eval_read_object_fields(json, json.root, &field_names);

		if (json.root->type == JSON_ARRAY)
			eval_read_object_fields(json, &json.tokens[json.root->child], &field_names);

	}, 15 * 60 * 60ULL);

	return eval_list(field_names);
}

static expr_result_t eval_stock_fundamental(const expr_func_t* f, vec_expr_t* args, void* c)
{
	const auto& code = eval_get_string_arg(args, 0, "Invalid symbol code");
	const auto& field = eval_get_string_arg(args, 1, "Invalid field name");

	expr_result_t value;
	eod_fetch("fundamentals", code.str, FORMAT_JSON_CACHE, [field, &value](const json_object_t& json)
	{
		const bool allow_nulls = false;
		const json_object_t& ref = json.find(STRING_ARGS(field), allow_nulls);
		if (ref.is_null())
			return;

		if (ref.root->type == JSON_STRING)
		{
			value.type = EXPR_RESULT_SYMBOL;
			value.value = string_table_encode(ref.as_string());
		}
		else
			value = ref.as_number();
	}, 12 * 60ULL * 60ULL);

	return value;
}

static expr_result_t eval_time_now(const expr_func_t* f, vec_expr_t* args, void* c)
{
	return (double)time_now();
}

static expr_result_t eval_report(const expr_func_t* f, vec_expr_t* args, void* c)
{
	const auto& report_name = eval_get_string_arg(args, 0, "Invalid report name");

	report_handle_t report_handle = report_find_no_case(STRING_ARGS(report_name));
	if (!report_handle_is_valid(report_handle))
		throw EvalError(EXPR_ERROR_INVALID_ARGUMENT, report_name, "Cannot find report %.*s", STRING_FORMAT(report_name));

	report_t* report = report_get(report_handle);
	const auto& field_name = eval_get_string_arg(args, 1, "Invalid field name");

	if (!report_sync_titles(report))
		throw EvalError(EXPR_ERROR_EVALUATION_TIMEOUT, report_name, "Failed to resolve report %.*s titles", STRING_FORMAT(report_name));

	expr_result_t* results = nullptr;
	if (string_equal_nocase(STRING_CONST("ps"), STRING_ARGS(field_name)))
	{
		for (auto t : generics::fixed_array(report->titles))
		{
			const stock_t* s = t->stock;
			if (!s || s->has_resolve(FetchLevel::EOD | FetchLevel::REALTIME))
				continue;

			const expr_result_t& kvp = eval_pair(eval_symbol(s->code), t->ps.fetch());
			array_push(results, kvp);
		}
	}
	else
		throw EvalError(EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED, field_name, nullptr);

	return eval_list(results);
}

static expr_result_t eval_math_min(const expr_result_t* list)
{
	if (list == nullptr)
		return NIL;

	expr_result_t min;
	for (size_t i = 0; i < array_size(list); ++i)
	{
		expr_result_t e = list[i];

		if (e.is_set() && e.index == NO_INDEX)
			e = eval_math_min(e.list);

		if (e < min)
			min = e;
	}

	return min;
}

static expr_result_t eval_math_max(const expr_result_t* list)
{
	if (list == nullptr)
		return NIL;

	expr_result_t max;
	for (size_t i = 0; i < array_size(list); ++i)
	{
		expr_result_t e = list[i];

		if (e.is_set() && e.index == NO_INDEX)
			e = eval_math_max(e.list);

		if (e > max)
			max = e;
	}

	return max;
}

static expr_result_t eval_math_sum(const expr_result_t* list)
{
	if (list == nullptr)
		return NIL;

	expr_result_t sum(0.0);
	for (size_t i = 0; i < array_size(list); ++i)
	{
		expr_result_t e = list[i];

		if (e.is_set() && e.index == NO_INDEX)
			e = eval_math_sum(e.list);

		sum += e;
	}

	return sum;
}

static expr_result_t eval_math_avg(const expr_result_t* list)
{
	if (list == nullptr)
		return NIL;

	expr_result_t avg(0.0);
	size_t element_count = 0;
	for (size_t i = 0; i < array_size(list); ++i)
	{
		expr_result_t e = list[i];

		if (e.is_set() && e.index == NO_INDEX)
			e = eval_math_avg(e.list);

		if (e.is_null(e.index))
			continue;

		avg += e;
		element_count++;
	}

	return avg / (expr_result_t)(double)element_count;
}

static const expr_result_t* eval_expand_args(vec_expr_t* args)
{
	if (args == nullptr)
		return nullptr;

	if (args->len == 1 && (args->buf[0].type == OP_SET || args->buf[0].type == OP_FUNC))
		return expr_eval(&args->buf[0]).list;

	expr_result_t* list = nullptr;
	for (int i = 0; i < args->len; ++i)
	{
		const expr_result_t& e = expr_eval(&vec_nth(args, i));
		array_push(list, e);
	}

	return eval_list(list);
}

static expr_result_t eval_math_min(const expr_func_t* f, vec_expr_t* args, void* c)
{
	if (args == nullptr)
		return NIL;

	return eval_math_min(eval_expand_args(args));
}

static expr_result_t eval_math_max(const expr_func_t* f, vec_expr_t* args, void* c)
{
	if (args == nullptr)
		return NIL;

	return eval_math_max(eval_expand_args(args));
}

static expr_result_t eval_math_sum(const expr_func_t* f, vec_expr_t* args, void* c)
{
	if (args == nullptr)
		return NIL;

	return eval_math_sum(eval_expand_args(args));
}

static expr_result_t eval_math_avg(const expr_func_t* f, vec_expr_t* args, void* c)
{
	if (args == nullptr)
		return NIL;

	return eval_math_avg(eval_expand_args(args));
}

static expr_func_t user_funcs[] = {
	
	// Stock functions
	{STRING_CONST("S"), eval_stock_realtime, NULL, 0},		// S(GFL.TO, close)
	{STRING_CONST("F"), eval_stock_fundamental, NULL, 0},	// F(U.US, Highlights.WallStreetTargetPrice)
	{STRING_CONST("R"), eval_report, NULL, 0},				// MAX(R(finance, ps))

	// Set functions
	{STRING_CONST("MIN"), eval_math_min, NULL, 0},   		// MAX([-1, 0, 1])
	{STRING_CONST("MAX"), eval_math_max, NULL, 0},   		// MAX([1, 2, 3]) + MAX(4, 5, 6) = 9
	{STRING_CONST("SUM"), eval_math_sum, NULL, 0},   		// SUM(0, 0, 1, 3) == 4
	{STRING_CONST("AVG"), eval_math_avg, NULL, 0},   		// (AVG(1, [1, 1]) + AVG([1], [2], [3])) == 3

	// Meta functions
	{STRING_CONST("FIELDS"), eval_list_fields, NULL, 0},    // FIELDS(AAPL.US, 'real-time')

	// Time functions
	{STRING_CONST("NOW"), eval_time_now, NULL, 0},          // ELAPSED_DAYS(TO_DATE(F(SSE.V, General.UpdatedAt)), NOW())
	{NULL, 0, NULL, NULL, 0},
};

expr_result_t eval(string_const_t expression)
{
	memory_context_push(HASH_EXPR);

	for (size_t i = 0; i < array_size(_expr_lists); ++i)
		array_deallocate(_expr_lists[i]);
	array_clear(_expr_lists);

	expr_t* e = expr_create(STRING_ARGS(expression), &_global_vars, user_funcs);
	if (e == NULL)
	{
		memory_context_pop();
		return NIL;
	}

	expr_result_t result;
	try
	{
		EXPR_ERROR_CODE = EXPR_ERROR_NONE;
		result = expr_eval(e);
	}
	catch (EvalError err)
	{
		expr_error(err.code, expression, err.token.str, "Failed to evaluate expression: %.*s (%.*s)", 
			err.message_length, err.message, STRING_FORMAT(err.token));
	}

	expr_destroy(e, nullptr);
	memory_context_pop();
	return result;
}

expr_result_t eval(const char* expression, size_t expression_length /*= -1*/)
{
	return eval(string_const(expression, expression_length != -1 ? expression_length : string_length(expression)));
}

string_const_t eval_to_string(const expr_result_t& result, const char* fmt /*= "%.6g"*/)
{
	return result.as_string(fmt);
}

void eval_render_console()
{
	static bool has_ever_show_console = session_key_exists("show_stock_console");
	static bool show_stock_console = session_get_bool("show_stock_console", false);

	bool start_show_stock_console = show_stock_console;
	if (shortcut_executed(96/*GLFW_KEY_GRAVE_ACCENT,#*/))
		show_stock_console = !show_stock_console;

	if (!show_stock_console)
		return;

	if (!has_ever_show_console)
		ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);
	if (ImGui::Begin("Stock Console##2", &show_stock_console))
	{
		static bool focus_text_field = true;
		static char expression_buffer[4096]{ "" };
		static char result_buffer[4096 * 4]{ "" };
		static string_t result_log{ result_buffer, 0 };

		if (focus_text_field || ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
			focus_text_field = false;

			string_copy(STRING_CONST_CAPACITY(expression_buffer), 
				STRING_ARGS(session_get_string("stock_console_expression", "")));
		}

		const float control_height = 110.0f;
		bool evaluate = false;
		if (ImGui::InputTextMultiline("##Expression", STRING_CONST_CAPACITY(expression_buffer), 
			ImVec2(-198.0f, control_height), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine))
		{
			evaluate = true;
		}

		ImGui::SameLine();
		if (ImGui::Button("Eval", ImVec2(0, control_height)))
			evaluate = true;

		ImGui::SameLine();
		if (ImGui::Button("Clear", ImVec2(0, control_height)))
			result_log = string_copy(STRING_CONST_CAPACITY(result_buffer), STRING_CONST(""));

		if (evaluate)
		{
			if (result_log.length == ARRAY_COUNT(result_buffer)-1)
				result_log.length = 0;
			string_const_t expression_string = string_const(expression_buffer, string_length(expression_buffer));
			session_set_string("stock_console_expression", STRING_ARGS(expression_string));

			expr_result_t result = eval(expression_string);
			string_const_t result_string = eval_to_string(result);
			if (!string_is_null(result_string))
			{
				result_log = string_prepend(STRING_ARGS(result_log), ARRAY_COUNT(result_buffer), STRING_CONST("\n"));
				result_log = string_prepend(STRING_ARGS(result_log), ARRAY_COUNT(result_buffer), STRING_ARGS(result_string));
			}
			else if (EXPR_ERROR_CODE != 0)
			{
				result_log = string_prepend(STRING_ARGS(result_log), ARRAY_COUNT(result_buffer), STRING_CONST("\n"));
				result_log = string_prepend(STRING_ARGS(result_log), ARRAY_COUNT(result_buffer), EXPR_ERROR_MSG, string_length(EXPR_ERROR_MSG));
			}

			focus_text_field = true;
		}

		ImGui::InputTextMultiline("##Results", STRING_ARGS(result_log), ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
	} ImGui::End();

	if (start_show_stock_console != show_stock_console)
	{
		session_set_bool("show_stock_console", show_stock_console);
		start_show_stock_console = show_stock_console;
	}
}

static void eval_run_evaluators()
{
	for (size_t i = 0; i < array_size(_evaluators); ++i)
	{
		evaluator_t& e = _evaluators[i];

		if ((time_now() - e.last_run_time) > e.frequency)
		{
			e.last_run_time = time_now();

			const size_t expression_length = string_length(e.expression);
			if (expression_length == 0)
				continue;

			string_const_t expression = string_const(e.expression, expression_length);
			expr_result_t result = eval(expression);

			FOUNDATION_ASSERT(result.type != EXPR_RESULT_ARRAY);
			if (result.type == EXPR_RESULT_NULL)
				continue;

			record_t new_record{ time_now() };
			if (result.type == EXPR_RESULT_NUMBER)
				new_record.value = result.as_number();
			else if (result.type == EXPR_RESULT_SYMBOL)
				new_record.tag = (string_table_symbol_t)result.value;
			else if (result.type == EXPR_RESULT_TRUE)
				new_record.assertion = true;
			else if (result.type == EXPR_RESULT_FALSE)
				new_record.assertion = false;

			const size_t assertion_length = string_length(e.assertion);
			if (assertion_length > 0)
			{
				string_const_t assertion = string_const(e.assertion, assertion_length);
				string_const_t assertion_build = string_format_static(STRING_CONST("E = %.*s, %.*s"), STRING_FORMAT(expression), STRING_FORMAT(assertion));
				expr_result_t assertion_result = eval(assertion_build);

				if (assertion_result.type == EXPR_RESULT_TRUE)
					new_record.assertion = true;
				else if (assertion_result.type == EXPR_RESULT_FALSE)
					new_record.assertion = false;

				string_format(STRING_CONST_CAPACITY(e.assembled), STRING_CONST("%.*s > %.*s"), STRING_FORMAT(result.as_string("%.6lf")), STRING_FORMAT(assertion_result.as_string()));
			}

			array_push(e.records, new_record);
			e.last_run_time = time_now();
		}
	}
}

static int eval_format_date_range_label(double value, char* buff, int size, void* user_data)
{
	evaluator_t* ev = (evaluator_t*)user_data;
	if (math_real_is_nan(value))
		return 0;

	const double diff = _difftime64(ev->last_run_time, (time_t)value);
	value = diff / time_one_day();

	if (value >= 365)
	{
		value = math_round(value / 365);
		return (int)string_format(buff, size, STRING_CONST("%.0lfY"), value).length;
	}
	else if (value >= 30)
	{
		value = math_round(value / 30);
		return (int)string_format(buff, size, STRING_CONST("%.0lfM"), value).length;
	}
	else if (value >= 7)
	{
		value = math_round(value / 7);
		return (int)string_format(buff, size, STRING_CONST("%.0lfW"), value).length;
	}
	else if (value >= 1)
	{
		value = math_round(value);
		return (int)string_format(buff, size, STRING_CONST("%.0lfD"), value).length;
	}
	else if (value >= 0.042)
	{
		value = math_round(value * 24);
		return (int)string_format(buff, size, STRING_CONST("%.0lfH"), value).length;
	}

	value = math_round(value * 24 * 60);
	return (int)string_format(buff, size, STRING_CONST("%.3g mins."), value).length;
}

void eval_render_evaluators()
{
	static bool has_ever_show_evaluators = session_key_exists("show_evaluators");
	static bool show_evaluators = session_get_bool("show_evaluators", false);

	bool start_show_stock_console = show_evaluators;
	if (shortcut_executed(298/*GLFW_KEY_F9,#*/))
		show_evaluators = !show_evaluators;

	eval_run_evaluators();

	if (!show_evaluators)
		return;

	if (!has_ever_show_evaluators)
		ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);
	if (ImGui::Begin("Evaluators##1", &show_evaluators))
	{
		if (ImGui::BeginTable("##Evaluators", 5, 
			ImGuiTableFlags_SizingFixedFit |
			ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Label");
			ImGui::TableSetupColumn("Title");
			ImGui::TableSetupColumn("Expression");
			ImGui::TableSetupColumn("Assertion");
			ImGui::TableSetupColumn("Monitor", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			// New row
			ImGui::TableNextRow();
			{
				static evaluator_t new_entry;
				bool evaluate_expression = false;

				if (ImGui::TableNextColumn())
				{
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::InputTextWithHint("##Label", "Description", STRING_CONST_CAPACITY(new_entry.label), ImGuiInputTextFlags_EnterReturnsTrue))
						evaluate_expression = true;
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::InputTextWithHint("##Title", "AAPL.US", STRING_CONST_CAPACITY(new_entry.code), ImGuiInputTextFlags_EnterReturnsTrue))
						evaluate_expression = true;
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::InputTextWithHint("##Expression", "S(AAPL.US, price)", STRING_CONST_CAPACITY(new_entry.expression), ImGuiInputTextFlags_EnterReturnsTrue))
						evaluate_expression = true;
				}


				if (ImGui::TableNextColumn())
				{
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					if (ImGui::InputTextWithHint("##Assertion", "E > 200.0", STRING_CONST_CAPACITY(new_entry.assertion), ImGuiInputTextFlags_EnterReturnsTrue))
						evaluate_expression = true;
				}

				ImGui::TableNextColumn();

				if (evaluate_expression)
				{
					string_const_t expression = string_const(new_entry.expression, string_length(new_entry.expression));
					string_const_t assertion = string_const(new_entry.assertion, string_length(new_entry.assertion));
					string_const_t result = eval(expression).as_string("%.6lf");

					string_const_t assertion_build = string_format_static(STRING_CONST("E = %.*s, %.*s"), STRING_FORMAT(expression), STRING_FORMAT(assertion));
					string_const_t assertion_result = eval(assertion_build).as_string();

					string_format(STRING_CONST_CAPACITY(new_entry.assembled), STRING_CONST("%.*s > %.*s"), STRING_FORMAT(result), STRING_FORMAT(assertion_result));
				}

				if (ImGui::SmallButton("Add"))
				{
					array_push(_evaluators, new_entry);
					memset(&new_entry, 0, sizeof(evaluator_t));
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(new_entry.assembled);
			}

			for (size_t i = 0; i < array_size(_evaluators); ++i)
			{
				evaluator_t& ev = _evaluators[i];
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 200.0f);
				{
					bool evaluate_expression = false;

					ImGui::PushID(&ev);

					if (ImGui::TableNextColumn())
					{
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						if (ImGui::InputTextWithHint("##Label", "Description", STRING_CONST_CAPACITY(ev.label), ImGuiInputTextFlags_EnterReturnsTrue))
							evaluate_expression = true;
						ImGui::Text("%u Records", array_size(ev.records));
					}

					if (ImGui::TableNextColumn())
					{
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						if (ImGui::InputTextWithHint("##Title", "AAPL.US", STRING_CONST_CAPACITY(ev.code), ImGuiInputTextFlags_EnterReturnsTrue))
							evaluate_expression = true;

						ImGui::TextWrapped("%.*s", STRING_FORMAT(string_from_time_static(ev.last_run_time * 1000, true)));
					}

					if (ImGui::TableNextColumn())
					{
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						if (ImGui::InputTextWithHint("##Expression", "S(AAPL.US, price)", STRING_CONST_CAPACITY(ev.expression), ImGuiInputTextFlags_EnterReturnsTrue))
							evaluate_expression = true;

						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted(ICON_MD_UPDATE);
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						ImGui::InputDouble("##Frequency", &ev.frequency, 60.0, 0, "%.4g s.");
						if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Number of seconds to wait before re-evaluating these expressions.");
							ImGui::EndTooltip();
						}
					}

					if (ImGui::TableNextColumn())
					{
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						if (ImGui::InputTextWithHint("##Assertion", "E > 200.0", STRING_CONST_CAPACITY(ev.assertion), ImGuiInputTextFlags_EnterReturnsTrue))
							evaluate_expression = true;

						ImGui::TextWrapped(ev.assembled);

						if(ImGui::SmallButton("Clear records"))
						{
							array_deallocate(ev.records);
							evaluate_expression = true;
						}

						ImGui::PushStyleColor(ImGuiCol_Button, BACKGROUND_CRITITAL_COLOR);
						if (ImGui::SmallButton("Delete"))
						{
							array_deallocate(ev.records);
							array_erase(_evaluators, i--);
						}
						ImGui::PopStyleColor();
					}

					if (evaluate_expression)
						ev.last_run_time = 0;

					ImGui::TableNextColumn();

					if (array_size(ev.records) > 2 && ImPlot::BeginPlot("##MonitorGraph"))
					{
						double max = (double)array_last(ev.records)->time;
						double min = (double)ev.records[0].time;
						ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
						ImPlot::SetupAxisFormat(ImAxis_X1, eval_format_date_range_label, &ev);
						ImPlot::SetupAxisTicks(ImAxis_X1, min, max, 6);
						ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min - (max - min) * 0.05, max + (max - min) * 0.05);

						ImPlot::PlotLineG("##Values", [](int idx, void* user_data)->ImPlotPoint
						{
							evaluator_t* c = (evaluator_t*)user_data;
							const record_t* r = &c->records[idx];

							const double x = (double)r->time;
							const double y = r->value;
							return ImPlotPoint(x, y);
						}, &ev, array_size(ev.records), ImPlotLineFlags_SkipNaN);
						ImPlot::EndPlot();
					}

					ImGui::PopID();
				}
			}
			
			ImGui::EndTable();
		}
	} ImGui::End();

	if (start_show_stock_console != show_evaluators)
	{
		session_set_bool("show_evaluators", show_evaluators);
		start_show_stock_console = show_evaluators;
	}
}

static string_const_t eval_evaluators_file_path()
{
	return session_get_user_file_path(STRING_CONST("evaluators.json"));
}

static void eval_load_evaluators(config_handle_t evaluators_data)
{
	for (const auto& cv : evaluators_data)
	{
		evaluator_t e{};
		string_copy(STRING_CONST_CAPACITY(e.code), STRING_ARGS(cv["code"].as_string()));
		string_copy(STRING_CONST_CAPACITY(e.label), STRING_ARGS(cv["label"].as_string()));
		string_copy(STRING_CONST_CAPACITY(e.expression), STRING_ARGS(cv["expression"].as_string()));
		string_copy(STRING_CONST_CAPACITY(e.assertion), STRING_ARGS(cv["assertion"].as_string()));
		e.frequency = cv["frequency"].as_number(60.0);

		for (const auto& rcv : cv["records"])
		{
			record_t r{};
			r.time = (time_t)rcv["time"].as_number();

			// Only keep record 5 days old an discard the rest.
			if (time_elapsed_days(r.time, time_now()) <= 5.0)
			{
				r.value = rcv["value"].as_number();
				r.tag = string_table_encode(rcv["tag"].as_string());
				r.assertion = rcv["assertion"].as_boolean();
				array_push(e.records, r);
			}
		}

		array_push(_evaluators, e);
	}
}

static void eval_save_evaluators()
{
	config_write_file(eval_evaluators_file_path(), [](config_handle_t evaluators_data)
	{
		for (size_t i = 0; i < array_size(_evaluators); ++i)
		{
			evaluator_t& e = _evaluators[i];

			config_handle_t ecv = config_array_push(evaluators_data, CONFIG_VALUE_OBJECT);
			config_set(ecv, "code", e.code, string_length(e.code));
			config_set(ecv, "label", e.label, string_length(e.label));
			config_set(ecv, "expression", e.expression, string_length(e.expression));
			config_set(ecv, "assertion", e.assertion, string_length(e.assertion));
			config_set(ecv, "frequency", e.frequency);

			config_handle_t records_data = config_set_array(ecv, STRING_CONST("records"));

			for (size_t ri = 0; ri < array_size(e.records); ++ri)
			{
				const record_t& r = e.records[ri];
				auto rcv = config_array_push(records_data, CONFIG_VALUE_OBJECT);
				config_set(rcv, "time", (double)r.time);
				config_set(rcv, "value", r.value);
				config_set(rcv, "tag", string_table_decode_const(r.tag));
				config_set(rcv, "assertion", r.assertion);
			}

			array_deallocate(e.records);
		}

		return true;
	}, CONFIG_VALUE_ARRAY, CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
	CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES | CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
}

void eval_initialize()
{
	#ifdef FOUNDATION_PLATFORM_WINDOWS
	INT rc;
	WSADATA wsaData;

	rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (rc) {
		log_errorf(0, ERROR_EXCEPTION, STRING_CONST("WSAStartup Failed."));
	}
	#endif

	const auto json_flags =
		CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS |
		CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
		CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES |
		CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS |
		CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS;

	string_const_t evaluators_file_path = eval_evaluators_file_path();
	config_handle_t evaluators_data = config_parse_file(STRING_ARGS(evaluators_file_path), json_flags);
	if (evaluators_data)
	{
		eval_load_evaluators(evaluators_data);
		config_deallocate(evaluators_data);
	}

	//_ws_client = easywsclient::WebSocket::from_url("wss://ws.eodhistoricaldata.com/ws/us?api_token=TODO");
}

void eval_shutdown()
{
	eval_save_evaluators();
	array_deallocate(_evaluators);

	for (size_t i = 0; i < array_size(_expr_lists); ++i)
		array_deallocate(_expr_lists[i]);
	array_deallocate(_expr_lists);
	expr_destroy(nullptr, &_global_vars);

	delete _ws_client;
	_ws_client = nullptr;

	#ifdef FOUNDATION_PLATFORM_WINDOWS
		WSACleanup();
	#endif
}

#endif
