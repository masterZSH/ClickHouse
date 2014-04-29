#include <errno.h>
#include <cstdlib>

#include <DB/IO/ReadHelpers.h>

#include <DB/Parsers/IAST.h>
#include <DB/Parsers/ASTExpressionList.h>
#include <DB/Parsers/ASTFunction.h>
#include <DB/Parsers/ASTIdentifier.h>
#include <DB/Parsers/ASTLiteral.h>
#include <DB/Parsers/ASTAsterisk.h>
#include <DB/Parsers/ASTOrderByElement.h>
#include <DB/Parsers/ASTSelectQuery.h>
#include <DB/Parsers/ASTSubquery.h>

#include <DB/Parsers/CommonParsers.h>
#include <DB/Parsers/ExpressionListParsers.h>
#include <DB/Parsers/ParserSelectQuery.h>

#include <DB/Parsers/ExpressionElementParsers.h>


namespace DB
{


bool ParserArray::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;
	ASTPtr contents_node;
	ParserString open("["), close("]");
	ParserExpressionList contents;
	ParserWhiteSpaceOrComments ws;

	if (!open.ignore(pos, end, expected))
		return false;

	ws.ignore(pos, end);
	contents.parse(pos, end, contents_node, expected);
	ws.ignore(pos, end);

	if (!close.ignore(pos, end, expected))
		return false;

	ASTFunction * function_node = new ASTFunction(StringRange(begin, pos));
	function_node->name = "array";
	function_node->arguments = contents_node;
	function_node->children.push_back(contents_node);
	node = function_node;

	return true;
}


bool ParserParenthesisExpression::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;
	ASTPtr contents_node;
	ParserString open("("), close(")");
	ParserExpressionList contents;
	ParserWhiteSpaceOrComments ws;

	if (!open.ignore(pos, end, expected))
		return false;

	ws.ignore(pos, end);
	contents.parse(pos, end, contents_node, expected);
	ws.ignore(pos, end);

	if (!close.ignore(pos, end, expected))
		return false;

	ASTExpressionList & expr_list = dynamic_cast<ASTExpressionList &>(*contents_node);

	/// пустое выражение в скобках недопустимо
	if (expr_list.children.empty())
	{
		expected = "not empty list of expressions in parenthesis";
		return false;
	}

	if (expr_list.children.size() == 1)
	{
		node = expr_list.children.front();
	}
	else
	{
		ASTFunction * function_node = new ASTFunction(StringRange(begin, pos));
		function_node->name = "tuple";
		function_node->arguments = contents_node;
		function_node->children.push_back(contents_node);
		node = function_node;
	}

	return true;
}


bool ParserSubquery::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;
	ASTPtr select_node;
	ParserString open("("), close(")");
	ParserSelectQuery select;
	ParserWhiteSpaceOrComments ws;

	if (!open.ignore(pos, end, expected))
		return false;

	ws.ignore(pos, end);
	select.parse(pos, end, select_node, expected);
	ws.ignore(pos, end);

	if (!close.ignore(pos, end, expected))
		return false;

	node = new ASTSubquery(StringRange(begin, pos));
	dynamic_cast<ASTSubquery &>(*node).children.push_back(select_node);
	return true;
}
	

bool ParserIdentifier::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	/// Идентификатор в обратных кавычках
	if (pos != end && *pos == '`')
	{
		ReadBuffer buf(const_cast<char *>(pos), end - pos, 0);
		String s;
		readBackQuotedString(s, buf);
		pos += buf.count();
		node = new ASTIdentifier(StringRange(begin, pos), s);
		return true;
	}
	else
	{
		while (pos != end
			&& ((*pos >= 'a' && *pos <= 'z')
				|| (*pos >= 'A' && *pos <= 'Z')
				|| (*pos == '_')
				|| (pos != begin && *pos >= '0' && *pos <= '9')))
			++pos;

		if (pos != begin)
		{
			node = new ASTIdentifier(StringRange(begin, pos), String(begin, pos - begin));
			return true;
		}
		else
			return false;
	}
}


bool ParserCompoundIdentifier::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	/// Идентификатор в обратных кавычках
	if (pos != end && *pos == '`')
	{
		ReadBuffer buf(const_cast<char *>(pos), end - pos, 0);
		String s;
		readBackQuotedString(s, buf);
		pos += buf.count();
		node = new ASTIdentifier(StringRange(begin, pos), s);
		return true;
	}
	else
	{
		while (pos != end)
		{
			while (pos != end
				&& ((*pos >= 'a' && *pos <= 'z')
					|| (*pos >= 'A' && *pos <= 'Z')
					|| (*pos == '_')
					|| (pos != begin && *pos >= '0' && *pos <= '9')))
				++pos;
			
			/// Если следующий символ - точка '.' и за ней следует, не цифра,
			/// то продолжаем парсинг имени идентификатора
			if (pos != begin && pos + 1 < end && *pos == '.' &&
				!(*(pos + 1) >= '0' && *(pos + 1) <= '9'))
				++pos;
			else
				break;
		}

		if (pos != begin)
		{
			node = new ASTIdentifier(StringRange(begin, pos), String(begin, pos - begin));
			return true;
		}
		else
			return false;
	}
}


bool ParserFunction::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	ParserIdentifier id_parser;
	ParserString open("("), close(")");
	ParserExpressionList contents;
	ParserWhiteSpaceOrComments ws;

	ASTPtr identifier;
	ASTPtr expr_list_args;
	ASTPtr expr_list_params;

	if (!id_parser.parse(pos, end, identifier, expected))
		return false;

	ws.ignore(pos, end);

	if (!open.ignore(pos, end, expected))
		return false;

	ws.ignore(pos, end);
	Pos contents_begin = pos;
	contents.parse(pos, end, expr_list_args, expected);
	Pos contents_end = pos;
	ws.ignore(pos, end);

	if (!close.ignore(pos, end, expected))
		return false;

	/** Проверка на распространённый случай ошибки - часто из-за сложности квотирования аргументов командной строки,
	  *  в запрос попадает выражение вида toDate(2014-01-01) вместо toDate('2014-01-01').
	  * Если не сообщить, что первый вариант - ошибка, то аргумент будет проинтерпретирован как 2014 - 01 - 01 - некоторое число,
	  *  и запрос тихо вернёт неожиданный результат.
	  */
	if (dynamic_cast<const ASTIdentifier &>(*identifier).name == "toDate"
		&& contents_end - contents_begin == strlen("2014-01-01")
		&& contents_begin[0] >= '2' && contents_begin[0] <= '3'
		&& contents_begin[1] >= '0' && contents_begin[1] <= '9'
		&& contents_begin[2] >= '0' && contents_begin[2] <= '9'
		&& contents_begin[3] >= '0' && contents_begin[3] <= '9'
		&& contents_begin[4] == '-'
		&& contents_begin[5] >= '0' && contents_begin[5] <= '9'
		&& contents_begin[6] >= '0' && contents_begin[6] <= '9'
		&& contents_begin[7] == '-'
		&& contents_begin[8] >= '0' && contents_begin[8] <= '9'
		&& contents_begin[9] >= '0' && contents_begin[9] <= '9')
	{
		std::string contents(contents_begin, contents_end - contents_begin);
		throw Exception("Argument of function toDate is unquoted: toDate(" + contents + "), must be: toDate('" + contents + "')"
			, ErrorCodes::SYNTAX_ERROR);
	}

	/// У параметрической агрегатной функции - два списка (параметры и аргументы) в круглых скобках. Пример: quantile(0.9)(x).
	if (open.ignore(pos, end, expected))
	{
		expr_list_params = expr_list_args;
		expr_list_args = nullptr;
		
		ws.ignore(pos, end);
		contents.parse(pos, end, expr_list_args, expected);
		ws.ignore(pos, end);

		if (!close.ignore(pos, end, expected))
			return false;
	}

	ASTFunction * function_node = new ASTFunction(StringRange(begin, pos));
	function_node->name = dynamic_cast<ASTIdentifier &>(*identifier).name;

	function_node->arguments = expr_list_args;
	function_node->children.push_back(function_node->arguments);

	if (expr_list_params)
	{
		function_node->parameters = expr_list_params;
		function_node->children.push_back(function_node->parameters);
	}
	
	node = function_node;
	return true;
}


bool ParserNull::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;
	ParserString nested_parser("NULL", true);
	if (nested_parser.parse(pos, end, node, expected))
	{
		node = new ASTLiteral(StringRange(StringRange(begin, pos)), Null());
		return true;
	}
	else
		return false;
}


bool ParserNumber::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Field res;

	Pos begin = pos;
	if (pos == end)
		return false;

	/** Максимальная длина числа. 319 символов достаточно, чтобы записать максимальный double в десятичной форме.
	  * Лишнее копирование нужно, чтобы воспользоваться функциями strto*, которым нужна 0-терминированная строка.
	  */
	char buf[320];

	size_t bytes_to_copy = end - pos < 319 ? end - pos : 319;
	memcpy(buf, pos, bytes_to_copy);
	buf[bytes_to_copy] = 0;

	char * pos_double = buf;
	errno = 0;	/// Функции strto* не очищают errno.
	Float64 float_value = std::strtod(buf, &pos_double);
	if (pos_double == buf || errno == ERANGE)
	{
		expected = "number";
		return false;
	}
	res = float_value;

	/// попробуем использовать более точный тип - UInt64 или Int64

	char * pos_integer = buf;
	if (float_value < 0)
	{
		errno = 0;
		Int64 int_value = std::strtoll(buf, &pos_integer, 0);
		if (pos_integer == pos_double && errno != ERANGE)
			res = int_value;
	}
	else
	{
		errno = 0;
		UInt64 uint_value = std::strtoull(buf, &pos_integer, 0);
		if (pos_integer == pos_double && errno != ERANGE)
			res = uint_value;
	}

	pos += pos_double - buf;
	node = new ASTLiteral(StringRange(begin, pos), res);
	return true;
}


bool ParserStringLiteral::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;
	String s;

	if (pos == end || *pos != '\'')
	{
		expected = "opening single quote";
		return false;
	}

	++pos;

	while (pos != end)
	{
		size_t bytes = 0;
		for (; pos + bytes != end; ++bytes)
			if (pos[bytes] == '\\' || pos[bytes] == '\'')
				break;

		s.append(pos, bytes);
		pos += bytes;

		if (*pos == '\'')
		{
			++pos;
			node = new ASTLiteral(StringRange(begin, pos), s);
			return true;
		}

		if (*pos == '\\')
		{
			++pos;
			if (pos == end)
			{
				expected = "escape sequence";
				return false;
			}
			s += parseEscapeSequence(*pos);
			++pos;
		}
	}

	expected = "closing single quote";
	return false;
}


bool ParserLiteral::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	ParserNull null_p;
	ParserNumber num_p;
	ParserStringLiteral str_p;

	if (null_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (num_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (str_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	expected = "literal: one of nullptr, number, single quoted string";
	return false;
}


bool ParserAlias::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	ParserWhiteSpaceOrComments ws;
	ParserString s_as("AS", true, true);
	ParserIdentifier id_p;

	if (!s_as.parse(pos, end, node, expected))
		return false;

	ws.ignore(pos, end);

	if (!id_p.parse(pos, end, node, expected))
		return false;

	return true;
}


bool ParserExpressionElement::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	ParserParenthesisExpression paren_p;
	ParserSubquery subquery_p;
	ParserArray array_p;
	ParserLiteral lit_p;
	ParserFunction fun_p;
	ParserCompoundIdentifier id_p;
	ParserString asterisk_p("*");

	if (subquery_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (paren_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (array_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (lit_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (fun_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (id_p.parse(pos, end, node, expected))
		return true;
	pos = begin;

	if (asterisk_p.parse(pos, end, node, expected))
	{
		node = new ASTAsterisk(StringRange(begin, pos));
		return true;
	}
	pos = begin;

	expected = "expression element: one of array, literal, function, identifier, asterisk, parenthised expression, subquery";
	return false;
}


bool ParserWithOptionalAlias::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	ParserWhiteSpaceOrComments ws;
	ParserAlias alias_p;

	if (!elem_parser->parse(pos, end, node, expected))
		return false;

	ws.ignore(pos, end);

	ASTPtr alias_node;
	if (alias_p.parse(pos, end, alias_node, expected))
	{
		String alias_name = dynamic_cast<ASTIdentifier &>(*alias_node).name;
		
		if (ASTFunction * func = dynamic_cast<ASTFunction *>(&*node))
			func->alias = alias_name;
		else if (ASTIdentifier * ident = dynamic_cast<ASTIdentifier *>(&*node))
			ident->alias = alias_name;
		else if (ASTLiteral * lit = dynamic_cast<ASTLiteral *>(&*node))
			lit->alias = alias_name;
		else
		{
			expected = "alias cannot be here";
			return false;
		}
	}

	return true;
}


bool ParserOrderByElement::parseImpl(Pos & pos, Pos end, ASTPtr & node, const char *& expected)
{
	Pos begin = pos;

	ParserWhiteSpaceOrComments ws;
	ParserExpressionWithOptionalAlias elem_p;
	ParserString ascending("ASCENDING", true, true);
	ParserString descending("DESCENDING", true, true);
	ParserString asc("ASC", true, true);
	ParserString desc("DESC", true, true);
	ParserString collate("COLLATE", true, true);
	ParserStringLiteral collate_locale_parser;

	ASTPtr expr_elem;
	if (!elem_p.parse(pos, end, expr_elem, expected))
		return false;

	int direction = 1;
	ws.ignore(pos, end);

	if (descending.ignore(pos, end) || desc.ignore(pos, end))
		direction = -1;
	else
		ascending.ignore(pos, end) || asc.ignore(pos, end);
	
	ws.ignore(pos, end);
	
	Poco::SharedPtr<Collator> collator = nullptr;
	if (collate.ignore(pos, end))
	{
		ws.ignore(pos, end);
		
		ASTPtr locale_node;
		if (!collate_locale_parser.parse(pos, end, locale_node, expected))
			return false;
		
		const String & locale = dynamic_cast<const ASTLiteral &>(*locale_node).value.safeGet<String>();
		collator = new Collator(locale);
	}

	node = new ASTOrderByElement(StringRange(begin, pos), direction, collator);
	node->children.push_back(expr_elem);
	return true;
}


}

