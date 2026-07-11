#ifndef CTXML__HPP
#define CTXML__HPP

#include "ctll/parser.hpp"
#include "ctxml/xml.hpp"
#include "ctxml/types.hpp"
#include "ctxml/actions.hpp"
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
// whose accessors are all constexpr. Built on CTLL, the compile-time
// LL(1) parser from the CTRE project.

namespace ctxml {

#if CTLL_CNTTP_COMPILER_CHECK
#define CTXML_STRING_INPUT ctll::fixed_string
#else
// C++17: pass a constexpr ctll::fixed_string variable with linkage
#define CTXML_STRING_INPUT const auto &
#endif

// does the input parse as well-formed XML (within the supported subset)?
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr bool is_valid =
	ctll::parser<xml, input, xml_actions>::template correct_with<context<>>;

// parse the input into its root element; invalid XML fails to compile
CTLL_EXPORT template <CTXML_STRING_INPUT input> constexpr auto parse() noexcept {
#if CTLL_CNTTP_COMPILER_CHECK
	constexpr auto _input = input; // workaround for GCC 9 bug 88092
#else
	constexpr auto & _input = input; // C++17: the argument has linkage
#endif
	using parsed = typename ctll::parser<xml, _input, xml_actions>::template output<context<>>;
	static_assert(parsed(), "ctxml: the input is not well-formed XML");
	return ctll::front(typename parsed::output_type::stack_type{});
}

} // namespace ctxml

#endif
