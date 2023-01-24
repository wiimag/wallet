#pragma once

#if 0

#include "framework/config.h"
#include "framework/common.h"
#include "framework/string_table.h"

#include <expr.h>

struct EvalError
{
	expr_error_code_t code;
	string_const_t token;
	char message[1024];
	size_t message_length{ 0 };

	EvalError(expr_error_code_t code, string_const_t token = { nullptr, 0 }, const char* msg = nullptr, ...)
	{
		this->code = code;
		this->token = token;

		if (msg)
		{
			va_list list;
			va_start(list, msg);
			message_length = string_vformat(STRING_CONST_CAPACITY(message), msg, string_length(msg), list).length;
			va_end(list);
		}
		else
		{
			const char* expr_error_msg = expr_error_cstr(code);
			size_t expr_error_msg_length = string_length(expr_error_msg);
			message_length = string_copy(STRING_CONST_CAPACITY(message), expr_error_msg, expr_error_msg_length).length;
		}
	}
};

struct record_t
{
	time_t time{ 0 };
	bool assertion{ false };
	string_table_symbol_t tag{ 0 };
	double value{ NAN };
};

struct evaluator_t
{
	char code[32]{ '\0' };
	char label[64]{ '\0' };
	char expression[1024]{ '\0' };
	char assertion[256]{ '\0' };
	char assembled[ARRAY_COUNT(expression) + ARRAY_COUNT(assertion)]{ '\0' };
	double frequency{ 60.0 * 5 }; // 5 minutes

	record_t* records{ nullptr };
	time_t last_run_time{ 0 };
};

expr_result_t eval(string_const_t expression);
expr_result_t eval(const char* expression, size_t expression_length = -1);
string_const_t eval_to_string(const expr_result_t& result, const char* fmt = "%.6g");

void eval_render_console();
void eval_render_evaluators();

void eval_initialize();
void eval_shutdown();
#endif