#ifndef CTXML__ACTIONS__HPP
#define CTXML__ACTIONS__HPP

#include "xml.hpp"
#include "types.hpp"
#include "../ctll/list.hpp"
#include "../ctll/grammars.hpp"

// Semantic actions building the document on a type stack while CTLL
// parses (the same architecture as CTRE's pcre_actions): names, text
// and attribute values accumulate character by character, attributes
// pair up as they complete, and closing a tag collects everything back
// to its open marker.
//
// Well-formedness beyond the grammar happens here, as compile errors
// via ctll::reject: a close tag whose name differs from its open tag,
// and duplicate attribute names within one element.

namespace ctxml {

// the parser subject: just a stack of partial results
template <typename Stack = ctll::list<>> struct context {
	using stack_type = Stack;
	static constexpr inline auto stack = stack_type();
	constexpr context() noexcept { }
	constexpr context(Stack) noexcept { }
};

template <typename... Content> context(ctll::list<Content...>) -> context<ctll::list<Content...>>;

// parse-time markers
template <typename Name> struct open_marker { };
template <typename Name, typename Attributes> struct element_open { };
template <typename Name> struct attr_name_marker { };
template <size_t Value> struct hex_marker { };
template <size_t Value> struct dec_marker { };

namespace detail {

// append one code point to a text as UTF-8 bytes
template <char32_t CodePoint, auto... Cs> constexpr auto append_code_point(text<Cs...>) noexcept {
	constexpr auto cp = static_cast<size_t>(CodePoint);
	if constexpr (cp < 0x80) {
		return text<Cs..., static_cast<char32_t>(cp)>{};
	} else if constexpr (cp < 0x800) {
		return text<Cs..., static_cast<char32_t>(0xC0 | (cp >> 6)), static_cast<char32_t>(0x80 | (cp & 0x3F))>{};
	} else if constexpr (cp < 0x10000) {
		return text<Cs..., static_cast<char32_t>(0xE0 | (cp >> 12)), static_cast<char32_t>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char32_t>(0x80 | (cp & 0x3F))>{};
	} else {
		return text<Cs..., static_cast<char32_t>(0xF0 | (cp >> 18)), static_cast<char32_t>(0x80 | ((cp >> 12) & 0x3F)), static_cast<char32_t>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char32_t>(0x80 | (cp & 0x3F))>{};
	}
}

// append a raw unit to the text on top of the stack, starting a new
// text when the top is something else (a marker or a finished node);
// units above 0xFF only occur when the input was decoded to code
// points, and are encoded back to UTF-8
template <char32_t Unit, auto... Cs, typename... Ts> constexpr auto put_unit(ctll::list<text<Cs...>, Ts...>) noexcept {
	if constexpr (static_cast<size_t>(Unit) > 0xFF) {
		return ctll::list<decltype(append_code_point<Unit>(text<Cs...>{})), Ts...>{};
	} else {
		return ctll::list<text<Cs..., Unit>, Ts...>{};
	}
}
template <char32_t Unit, typename... Ts> constexpr auto put_unit(ctll::list<Ts...>) noexcept {
	if constexpr (static_cast<size_t>(Unit) > 0xFF) {
		return ctll::list<decltype(append_code_point<Unit>(text<>{})), Ts...>{};
	} else {
		return ctll::list<text<Unit>, Ts...>{};
	}
}

// the same for a full code point (character references)
template <char32_t CodePoint, auto... Cs, typename... Ts> constexpr auto put_code_point(ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<decltype(append_code_point<CodePoint>(text<Cs...>{})), Ts...>{};
}
template <char32_t CodePoint, typename... Ts> constexpr auto put_code_point(ctll::list<Ts...>) noexcept {
	return ctll::list<decltype(append_code_point<CodePoint>(text<>{})), Ts...>{};
}

// is a text node whitespace-only? (those between elements are dropped)
template <typename> struct is_blank_text: std::false_type { };
template <auto... Cs> struct is_blank_text<text<Cs...>>
	: std::bool_constant<((Cs == ' ' || Cs == '\t' || Cs == '\n' || Cs == '\r') && ...)> { };

// are the attribute names pairwise distinct? (name equality is type
// equality, since equal content means the same text<...> type)
constexpr bool attribute_names_unique(ctll::list<>) noexcept {
	return true;
}
template <typename Head, typename... Tail> constexpr bool attribute_names_unique(ctll::list<Head, Tail...>) noexcept {
	return (!std::is_same_v<typename Head::name_type, typename Tail::name_type> && ...)
		&& attribute_names_unique(ctll::list<Tail...>{});
}

} // namespace detail

struct xml_actions {

	// --- names accumulate onto a fresh text<>

	template <auto V, typename... Ts> static constexpr auto apply(xml::begin_name, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<text<>, Ts...>>{};
	}

	template <auto V, auto... Cs, typename... Ts> static constexpr auto apply(xml::push_name_char, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context{detail::put_unit<static_cast<char32_t>(V)>(ctll::list<text<Cs...>, Ts...>{})};
	}

	// --- element opening: the completed name becomes an open marker

	template <auto V, auto... Cs, typename... Ts> static constexpr auto apply(xml::open_element, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<open_marker<text<Cs...>>, Ts...>>{};
	}

	// --- attributes

	template <auto V, auto... Cs, typename... Ts> static constexpr auto apply(xml::attribute_name, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<attr_name_marker<text<Cs...>>, Ts...>>{};
	}

	template <auto V, auto... Cs, typename Name, typename... Ts> static constexpr auto apply(xml::make_attribute, ctll::term<V>, context<ctll::list<text<Cs...>, attr_name_marker<Name>, Ts...>>) {
		return context<ctll::list<attribute<Name, text<Cs...>>, Ts...>>{};
	}
	// an empty value: nothing accumulated on top of the name marker
	template <auto V, typename Name, typename... Ts> static constexpr auto apply(xml::make_attribute, ctll::term<V>, context<ctll::list<attr_name_marker<Name>, Ts...>>) {
		return context<ctll::list<attribute<Name, text<>>, Ts...>>{};
	}

	// collect the attributes back to the open marker; duplicate names
	// make the document ill-formed
	template <typename... Attrs, typename Name, typename... Rest> static constexpr auto collect_attributes(ctll::list<open_marker<Name>, Rest...>, ctll::list<Attrs...>) {
		if constexpr (detail::attribute_names_unique(ctll::list<Attrs...>{})) {
			return context<ctll::list<element_open<Name, ctll::list<Attrs...>>, Rest...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <typename AttrName, typename Value, typename... Attrs, typename... Rest> static constexpr auto collect_attributes(ctll::list<attribute<AttrName, Value>, Rest...>, ctll::list<Attrs...>) {
		return collect_attributes(ctll::list<Rest...>{}, ctll::list<attribute<AttrName, Value>, Attrs...>{});
	}

	template <auto V, typename... Ts> static constexpr auto apply(xml::finish_open_tag, ctll::term<V>, context<ctll::list<Ts...>>) {
		return collect_attributes(ctll::list<Ts...>{}, ctll::list<>{});
	}

	// a self-closing tag is a complete element immediately
	template <auto V, typename... Ts> static constexpr auto apply(xml::finish_self_closing, ctll::term<V>, context<ctll::list<Ts...>>) {
		return finish_element(collect_attributes(ctll::list<Ts...>{}, ctll::list<>{}));
	}
	template <typename Name, typename Attributes, typename... Rest> static constexpr auto finish_element(context<ctll::list<element_open<Name, Attributes>, Rest...>>) {
		return context<ctll::list<element<Name, Attributes>, Rest...>>{};
	}
	static constexpr auto finish_element(ctll::reject) {
		return ctll::reject{};
	}

	// --- content

	template <auto V, typename... Ts> static constexpr auto apply(xml::push_text_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<static_cast<char32_t>(V)>(ctll::list<Ts...>{})};
	}

	// the predefined entities
	template <auto V, typename... Ts> static constexpr auto apply(xml::entity_lt, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U'<'>(ctll::list<Ts...>{})};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::entity_gt, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U'>'>(ctll::list<Ts...>{})};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::entity_amp, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U'&'>(ctll::list<Ts...>{})};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::entity_apos, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U'\''>(ctll::list<Ts...>{})};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::entity_quot, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U'"'>(ctll::list<Ts...>{})};
	}

	// character references: &#123; and &#x1F600;
	template <auto V, typename... Ts> static constexpr auto apply(xml::begin_hex, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<hex_marker<0>, Ts...>>{};
	}
	template <auto V, size_t Value, typename... Ts> static constexpr auto apply(xml::push_hex, ctll::term<V>, context<ctll::list<hex_marker<Value>, Ts...>>) {
		constexpr size_t digit = [] {
			if constexpr (V >= '0' && V <= '9') {
				return static_cast<size_t>(V - '0');
			} else if constexpr (V >= 'a' && V <= 'f') {
				return static_cast<size_t>(V - 'a' + 10);
			} else {
				return static_cast<size_t>(V - 'A' + 10);
			}
		}();
		return context<ctll::list<hex_marker<Value * 16 + digit>, Ts...>>{};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::begin_dec, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<dec_marker<0>, Ts...>>{};
	}
	template <auto V, size_t Value, typename... Ts> static constexpr auto apply(xml::push_dec, ctll::term<V>, context<ctll::list<dec_marker<Value>, Ts...>>) {
		return context<ctll::list<dec_marker<Value * 10 + static_cast<size_t>(V - '0')>, Ts...>>{};
	}
	template <auto V, size_t CodePoint, typename... Ts> static constexpr auto apply(xml::end_char_reference, ctll::term<V>, context<ctll::list<hex_marker<CodePoint>, Ts...>>) {
		return context{detail::put_code_point<static_cast<char32_t>(CodePoint)>(ctll::list<Ts...>{})};
	}
	template <auto V, size_t CodePoint, typename... Ts> static constexpr auto apply(xml::end_char_reference, ctll::term<V>, context<ctll::list<dec_marker<CodePoint>, Ts...>>) {
		return context{detail::put_code_point<static_cast<char32_t>(CodePoint)>(ctll::list<Ts...>{})};
	}

	// CDATA: brackets that turned out to be content
	template <auto V, typename... Ts> static constexpr auto apply(xml::cdata_bracket, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<U']'>(ctll::list<Ts...>{})};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::cdata_bracket_and_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<static_cast<char32_t>(V)>(detail::put_unit<U']'>(ctll::list<Ts...>{}))};
	}
	template <auto V, typename... Ts> static constexpr auto apply(xml::cdata_brackets_and_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context{detail::put_unit<static_cast<char32_t>(V)>(detail::put_unit<U']'>(detail::put_unit<U']'>(ctll::list<Ts...>{})))};
	}

	// --- closing a tag: verify the name, collect the children

	// the children arrive reversed; prepending while popping restores
	// document order, and whitespace-only text nodes are dropped
	template <typename CloseName, typename... Collected, typename Name, typename Attributes, typename... Rest> static constexpr auto collect_children(CloseName, ctll::list<element_open<Name, Attributes>, Rest...>, ctll::list<Collected...>) {
		if constexpr (std::is_same_v<CloseName, Name>) {
			return context<ctll::list<element<Name, Attributes, Collected...>, Rest...>>{};
		} else {
			// mismatched close tag: the document is not well-formed
			return ctll::reject{};
		}
	}
	template <typename CloseName, typename Node, typename... Collected, typename... Rest> static constexpr auto collect_children(CloseName close, ctll::list<Node, Rest...>, ctll::list<Collected...>) {
		if constexpr (detail::is_blank_text<Node>::value) {
			return collect_children(close, ctll::list<Rest...>{}, ctll::list<Collected...>{});
		} else {
			return collect_children(close, ctll::list<Rest...>{}, ctll::list<Node, Collected...>{});
		}
	}

	template <auto V, auto... Cs, typename... Ts> static constexpr auto apply(xml::end_element, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return collect_children(text<Cs...>{}, ctll::list<Ts...>{}, ctll::list<>{});
	}
};

} // namespace ctxml

#endif
