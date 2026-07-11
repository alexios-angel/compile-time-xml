#ifndef CTXML__HPP
#define CTXML__HPP

#include "ctlark.hpp"
#include "ctxml/grammar.hpp"
#include "ctxml/types.hpp"
#include "ctxml/bind.hpp"
#include "ctxml/serialize.hpp"
#include "ctxml/views.hpp"

// ctxml: compile-time XML.
//
//   constexpr auto doc = ctxml::parse<R"(
//       <server host="example.com" port="8080">
//           <path>/api</path>
//       </server>)">();
//
//   static_assert(doc.name() == "server");
//   static_assert(doc.attribute<"port">() == "8080");
//   static_assert(doc.get<"path">().text() == "/api");
//   static_assert(ctxml::is_valid<"<a><b/></a>">);
//   static_assert(!ctxml::is_valid<"<a></b>">); // mismatched tags
//
// The document is parsed while your code compiles - malformed or
// ill-formed XML (mismatched close tags, duplicate attributes) is a
// compile error, or `false` from is_valid - and the result is a TYPE
// whose accessors are all constexpr. The grammar layer is ctlark
// (compile-time Lark): the XML grammar is a lark grammar string
// (grammar.hpp) that only tokenizes because ctlark's lexing is
// contextual, and bind.hpp lowers the parsed tree into the document
// types, decoding entities and enforcing well-formedness on the way.

namespace ctxml {

#if CTLL_CNTTP_COMPILER_CHECK
#define CTXML_STRING_INPUT ctll::fixed_string
#else
// C++17: pass a constexpr ctll::fixed_string variable with linkage
#define CTXML_STRING_INPUT const auto &
#endif

namespace detail {

// grammar validity is a given (static_assert in grammar.hpp); input
// validity is the parse plus the binder's well-formedness checks
template <CTXML_STRING_INPUT input> constexpr bool valid_document() noexcept {
	if constexpr (!ctlark::is_valid<xml_grammar, input, xml_start>) {
		return false;
	} else {
		return bind<decltype(ctlark::parse<xml_grammar, input, xml_start>())>::ok;
	}
}

} // namespace detail

// does the input parse as well-formed XML (within the supported subset)?
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr bool is_valid =
	detail::valid_document<input>();

// what failed and where, when it does not: kind, byte offset, line,
// column and the expected terminals (kind none = the syntax is fine)
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr ctlark::error_info_t error_info() noexcept {
	return ctlark::error_info<detail::xml_grammar, input, detail::xml_start>();
}

// the rendered diagnostic - location, snippet with a caret, expected
// terminals - as a static string ("" when the syntax is fine)
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr std::string_view error_message() noexcept {
	return ctlark::error_message<detail::xml_grammar, input, detail::xml_start>();
}

// why the binder rejected a document that PARSES - mismatched close
// tags, duplicate attributes, invalid character references; reason
// none when the document is valid or when the syntax already failed
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr bind_error_t bind_error() noexcept {
	if constexpr (!ctlark::is_valid<detail::xml_grammar, input, detail::xml_start>) {
		return bind_error_t{};
	} else {
		return detail::bind<decltype(ctlark::parse<detail::xml_grammar, input, detail::xml_start>())>::fail;
	}
}

// parse the input into its root element; invalid XML fails to compile
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr auto parse() noexcept {
#ifdef CTLARK_VERBOSE_ERRORS
	(void)ctlark::verbose_report<detail::xml_grammar, input, detail::xml_start>();
#endif
	static_assert(ctlark::is_valid<detail::xml_grammar, input, detail::xml_start>,
	              "ctxml: the input is not valid XML syntax - print ctxml::error_message<input>() "
	              "for the location and the expected tokens");
	static_assert(!ctlark::is_valid<detail::xml_grammar, input, detail::xml_start> || is_valid<input>,
	              "ctxml: the input parses but is not well-formed (mismatched close tag, duplicate "
	              "attribute, or bad character reference) - print ctxml::bind_error<input>() for the reason");
	if constexpr (is_valid<input>) {
		using bound = detail::bind<decltype(ctlark::parse<detail::xml_grammar, input, detail::xml_start>())>;
		return typename bound::type{};
	} else {
		return element<text<>, ctll::list<>>{};
	}
}

// the ctlark debugging toolbox with the XML grammar baked in: traced
// parses (also runnable at runtime under a debugger), runtime inputs
// against the compile-time tables, token and grammar dumps
namespace debug {

CTLL_EXPORT template <CTXML_STRING_INPUT input, size_t Cap = 4096> constexpr auto traced_parse() noexcept {
	return ctlark::debug::traced_parse<detail::xml_grammar, input, detail::xml_start, Cap>();
}

CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr std::string_view dump_tokens() noexcept {
	return ctlark::debug::dump_tokens<detail::xml_grammar, input, detail::xml_start>();
}

CTLL_EXPORT constexpr std::string_view dump_grammar() noexcept {
	return ctlark::debug::dump_grammar<detail::xml_grammar>();
}

CTLL_EXPORT template <size_t MaxTokens = 1024>
ctlark::debug::runtime_result parse_runtime(std::string_view in) {
	return ctlark::debug::parse_runtime<detail::xml_grammar, MaxTokens>(in, "start");
}

} // namespace debug

} // namespace ctxml

#endif
