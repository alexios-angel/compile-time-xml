#ifndef CTLARK__HPP
#define CTLARK__HPP

#include "ctll/parser.hpp"
#include "ctlark/lark.hpp"
#include "ctlark/text.hpp"
#include "ctlark/ast.hpp"
#include "ctlark/actions.hpp"
#include "ctlark/compile.hpp"
#include "ctlark/earley.hpp"
#include "ctlark/tree.hpp"
#include "ctlark/lift.hpp"

// ctlark: compile-time Lark. Grammars are data, not code:
//
//   constexpr ctll::fixed_string grammar = R"(
//       start: WORD "," WORD "!"
//       WORD: /\w+/
//       %ignore " "
//   )";
//
//   constexpr auto t = ctlark::parse<grammar, "Hello, World!">();
//   static_assert(t.repr() ==
//       "Tree(start, [Token(WORD, 'Hello'), Token(WORD, 'World')])");
//
// The grammar TEXT is parsed while your code compiles (by CTLL, the
// compile-time LL(1) parser from CTRE, driven by a table generated
// from lark.gram), lowered to constexpr tables, and the input is
// parsed by a constexpr Earley parser - so the full class of
// context-free grammars works, left recursion and all. The resulting
// tree is a TYPE shaped exactly like lark's: Tree nodes with data and
// children, Token leaves, after ?/_ inlining, anonymous-token
// filtering and -> aliases.

namespace ctlark {

#if CTLL_CNTTP_COMPILER_CHECK
#define CTLARK_STRING_INPUT ctll::fixed_string
#else
// C++17: pass constexpr ctll::fixed_string variables with linkage
#define CTLARK_STRING_INPUT const auto &
#endif

// the default start rule, like lark's
inline constexpr ctll::fixed_string default_start_rule = "start";

namespace detail {

template <typename... Acc> constexpr auto reverse_items(ctll::list<>, ast::grammar<Acc...> g) noexcept {
	return g;
}
template <typename H, typename... Ts, typename... Acc>
constexpr auto reverse_items(ctll::list<H, Ts...>, ast::grammar<Acc...>) noexcept {
	return reverse_items(ctll::list<Ts...>{}, ast::grammar<H, Acc...>{});
}

// one instantiation per grammar text: parse the text with CTLL, lower
// the AST to constexpr tables
template <CTLARK_STRING_INPUT grammar_text> struct grammar_def {
#if CTLL_CNTTP_COMPILER_CHECK
	static constexpr auto text = grammar_text;
#else
	static constexpr auto & text = grammar_text;
#endif
	using parser_type = ctll::parser<lark_grammar, text, lark_actions>;
	static constexpr bool text_ok = parser_type::template correct_with<context<>>;

	static constexpr auto make() noexcept {
		if constexpr (text_ok) {
			using stack = typename parser_type::template output<context<>>::output_type::stack_type;
			using ast_type = decltype(reverse_items(stack{}, ast::grammar<>{}));
			return compile_tables<ast_type, text.size()>();
		} else {
			grammar_tables<1> g{};
			g.fail("ctlark: the grammar text is not valid Lark");
			return g;
		}
	}
	static constexpr auto tables = make();
};

// one instantiation per (grammar, input, start): the flat parse
template <CTLARK_STRING_INPUT grammar_text, CTLARK_STRING_INPUT input, CTLARK_STRING_INPUT start> struct parse_def {
	using def = grammar_def<grammar_text>;
#if CTLL_CNTTP_COMPILER_CHECK
	static constexpr auto in = input;
	static constexpr auto start_name = start;
#else
	static constexpr auto & in = input;
	static constexpr auto & start_name = start;
#endif

	static constexpr int find_start() noexcept {
		char buf[start_name.size() + 1]{};
		for (size_t i = 0; i < start_name.size(); ++i) { buf[i] = static_cast<char>(start_name[i]); }
		return def::tables.find_rule(std::string_view{buf, start_name.size()});
	}
	static constexpr int start_sym = find_start();

	static constexpr auto make() noexcept {
		if constexpr (!def::tables.ok || start_sym < 0) {
			return parse_result<in.size()>{}; // ok = false
		} else {
			return run_parse<def::tables, in, start_sym>();
		}
	}
	static constexpr auto result = make();
};

} // namespace detail

// does the grammar text parse and lower to a usable grammar?
CTLL_EXPORT template <CTLARK_STRING_INPUT grammar> constexpr bool grammar_valid =
	detail::grammar_def<grammar>::tables.ok;

// a static description of what is wrong with the grammar ("" when ok)
CTLL_EXPORT template <CTLARK_STRING_INPUT grammar> constexpr std::string_view grammar_error() noexcept {
	if constexpr (detail::grammar_def<grammar>::tables.ok) {
		return std::string_view{};
	} else {
		return detail::grammar_def<grammar>::tables.error_view();
	}
}

// does the input parse? (false also when the grammar itself is bad -
// check grammar_valid to tell the two apart)
CTLL_EXPORT template <CTLARK_STRING_INPUT grammar, CTLARK_STRING_INPUT input,
                      CTLARK_STRING_INPUT start = default_start_rule>
constexpr bool is_valid = detail::parse_def<grammar, input, start>::result.ok;

// parse the input into a Tree/Token type; any failure is a compile
// error with a static_assert naming the stage that failed
CTLL_EXPORT template <CTLARK_STRING_INPUT grammar, CTLARK_STRING_INPUT input,
                      CTLARK_STRING_INPUT start = default_start_rule>
constexpr auto parse() noexcept {
	using pd = detail::parse_def<grammar, input, start>;
	static_assert(pd::def::text_ok,
	              "ctlark: the grammar text is not valid Lark (see the README for the supported subset)");
	static_assert(!pd::def::text_ok || pd::def::tables.ok,
	              "ctlark: the grammar is not usable - print ctlark::grammar_error<...>() for the reason");
	static_assert(!pd::def::tables.ok || pd::start_sym >= 0,
	              "ctlark: the start rule is not defined in the grammar");
	static_assert(pd::start_sym < 0 || pd::result.ok,
	              "ctlark: the input does not match the grammar");
	if constexpr (pd::result.ok) {
		return typename detail::lift_node<pd::def::tables, pd::in, pd::result, pd::result.root>::type{};
	} else {
		return tree<text<>>{}; // unreachable: the static_asserts fired
	}
}

// the Python-flavoured spelling: ctlark::lark<grammar>::parse<input>()
CTLL_EXPORT template <CTLARK_STRING_INPUT grammar_text> struct lark {
	static constexpr bool valid = grammar_valid<grammar_text>;

	static constexpr std::string_view error() noexcept {
		return grammar_error<grammar_text>();
	}

	template <CTLARK_STRING_INPUT input, CTLARK_STRING_INPUT start = default_start_rule>
	static constexpr auto parse() noexcept {
		return ctlark::parse<grammar_text, input, start>();
	}

	template <CTLARK_STRING_INPUT input, CTLARK_STRING_INPUT start = default_start_rule>
	static constexpr bool matches = ctlark::is_valid<grammar_text, input, start>;
};

} // namespace ctlark

#endif
