#ifndef CTXML__HPP
#define CTXML__HPP

#include "ctlark.hpp"
#include "ctxml/grammar.hpp"
#include "ctxml/types.hpp"
#include "ctxml/bind.hpp"
#include "ctxml/serialize.hpp"

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

// parse the input into its root element; invalid XML fails to compile
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr auto parse() noexcept {
	static_assert(is_valid<input>, "ctxml: the input is not well-formed XML");
	if constexpr (is_valid<input>) {
		using bound = detail::bind<decltype(ctlark::parse<detail::xml_grammar, input, detail::xml_start>())>;
		return typename bound::type{};
	} else {
		return element<text<>, ctll::list<>>{};
	}
}

} // namespace ctxml

#endif
