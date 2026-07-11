#ifndef CTLARK__COMMON__HPP
#define CTLARK__COMMON__HPP

#include "ast.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <string_view>
#endif

// The supported subset of lark's common.lark, embedded as grammar AST
// snippets: %import common.X emits the matching pattern and defines it
// as a terminal. Everything here is self-contained (no references), so
// imports cannot recurse.

namespace ctlark::detail {

namespace builtin {

using namespace ctlark::ast;

using u_digit = crange<'0', '9'>;
using u_lcase = crange<'a', 'z'>;
using u_ucase = crange<'A', 'Z'>;
using u_letter = cls<false, u_lcase, u_ucase>;
using u_sign = opt<cls<false, chr<'+'>, chr<'-'>>>;

using DIGIT = u_digit;
using HEXDIGIT = cls<false, u_digit, crange<'a', 'f'>, crange<'A', 'F'>>;
using INT = plus<u_digit>;
using SIGNED_INT = seq<u_sign, INT>;
// INT "." INT? | "." INT
using DECIMAL = alt<seq<INT, chr<'.'>, opt<INT>>, seq<chr<'.'>, INT>>;
using u_exp = seq<cls<false, chr<'e'>, chr<'E'>>, u_sign, INT>;
using FLOAT = alt<seq<INT, u_exp>, seq<DECIMAL, opt<u_exp>>>;
using SIGNED_FLOAT = seq<u_sign, FLOAT>;
using NUMBER = alt<FLOAT, INT>;
using SIGNED_NUMBER = seq<u_sign, NUMBER>;

// "..." with backslash escapes, single line
using ESCAPED_STRING = seq<
	chr<'"'>,
	star<alt<seq<chr<'\\'>, any_char>,
	         cls<true, chr<'"'>, chr<'\\'>, chr<'\x0A'>>>>,
	chr<'"'>>;

using LCASE_LETTER = u_lcase;
using UCASE_LETTER = u_ucase;
using LETTER = u_letter;
using WORD = plus<u_letter>;
using CNAME = seq<cls<false, chr<'_'>, u_lcase, u_ucase>,
                  star<cls<false, chr<'_'>, u_lcase, u_ucase, u_digit>>>;

using WS_INLINE = plus<cls<false, chr<' '>, chr<'\x09'>>>;
using WS = plus<space_class>;
using CR = chr<'\x0D'>;
using LF = chr<'\x0A'>;
using NEWLINE = plus<seq<opt<CR>, LF>>;

using SH_COMMENT = seq<chr<'#'>, star<cls<true, chr<'\x0A'>>>>;
using CPP_COMMENT = seq<chr<'/'>, chr<'/'>, star<cls<true, chr<'\x0A'>>>>;
// /* ([^*] | *+[^*/])* *+ /
using C_COMMENT = seq<
	chr<'/'>, chr<'*'>,
	star<alt<cls<true, chr<'*'>>,
	         seq<plus<chr<'*'>>, cls<true, chr<'*'>, chr<'/'>>>>>,
	plus<chr<'*'>>, chr<'/'>>;
using SQL_COMMENT = seq<chr<'-'>, chr<'-'>, star<cls<true, chr<'\x0A'>>>>;

} // namespace builtin

// emit the named builtin into the builder; returns the root node id or
// -1 when the name is not a supported common.lark terminal
template <typename B> constexpr int emit_common(std::string_view name, B & b) {
	if (name == "DIGIT") { return builtin::DIGIT::emit(b); }
	if (name == "HEXDIGIT") { return builtin::HEXDIGIT::emit(b); }
	if (name == "INT") { return builtin::INT::emit(b); }
	if (name == "SIGNED_INT") { return builtin::SIGNED_INT::emit(b); }
	if (name == "DECIMAL") { return builtin::DECIMAL::emit(b); }
	if (name == "FLOAT") { return builtin::FLOAT::emit(b); }
	if (name == "SIGNED_FLOAT") { return builtin::SIGNED_FLOAT::emit(b); }
	if (name == "NUMBER") { return builtin::NUMBER::emit(b); }
	if (name == "SIGNED_NUMBER") { return builtin::SIGNED_NUMBER::emit(b); }
	if (name == "ESCAPED_STRING") { return builtin::ESCAPED_STRING::emit(b); }
	if (name == "LCASE_LETTER") { return builtin::LCASE_LETTER::emit(b); }
	if (name == "UCASE_LETTER") { return builtin::UCASE_LETTER::emit(b); }
	if (name == "LETTER") { return builtin::LETTER::emit(b); }
	if (name == "WORD") { return builtin::WORD::emit(b); }
	if (name == "CNAME") { return builtin::CNAME::emit(b); }
	if (name == "WS_INLINE") { return builtin::WS_INLINE::emit(b); }
	if (name == "WS") { return builtin::WS::emit(b); }
	if (name == "CR") { return builtin::CR::emit(b); }
	if (name == "LF") { return builtin::LF::emit(b); }
	if (name == "NEWLINE") { return builtin::NEWLINE::emit(b); }
	if (name == "SH_COMMENT") { return builtin::SH_COMMENT::emit(b); }
	if (name == "CPP_COMMENT") { return builtin::CPP_COMMENT::emit(b); }
	if (name == "C_COMMENT") { return builtin::C_COMMENT::emit(b); }
	if (name == "SQL_COMMENT") { return builtin::SQL_COMMENT::emit(b); }
	return -1;
}

} // namespace ctlark::detail

#endif
