#ifndef CTXML__BIND__HPP
#define CTXML__BIND__HPP

#include "grammar.hpp"
#include "types.hpp"
#ifndef CTXML_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <utility>
#endif

// Lowering a ctlark parse tree into ctxml's document types, and
// checking what the grammar cannot: a close tag must match its open
// tag (name equality is TYPE equality), attribute names must be
// unique within an element, and character references must denote
// valid code points. bind<Tree>::ok folds those checks over the whole
// document; is_valid includes it.
//
// Entities decode here (the grammar already validated their syntax),
// CDATA content is taken literally, adjacent text pieces merge into
// one text node - exactly as the old semantic actions accumulated
// them - and whitespace-only text nodes are dropped.

namespace ctxml {

// why the binder rejected a document that PARSES - the well-formedness
// rules the grammar itself cannot express
CTLL_EXPORT enum class bind_reason : unsigned char {
	none,
	mismatched_tag,      // a close tag does not match its open tag
	duplicate_attribute, // the same attribute name twice in one element
	bad_reference        // a character reference to an invalid code point
};

CTLL_EXPORT constexpr std::string_view to_string(bind_reason r) noexcept {
	switch (r) {
		case bind_reason::none: return "none";
		case bind_reason::mismatched_tag: return "a close tag does not match its open tag";
		case bind_reason::duplicate_attribute: return "duplicate attribute name in an element";
		case bind_reason::bad_reference: return "character reference to an invalid code point";
	}
	return "unknown";
}

// the first binder failure: which rule broke, and the raw offending
// token as written in the input
CTLL_EXPORT struct bind_error_t {
	bind_reason reason = bind_reason::none;
	std::string_view where{};

	constexpr bool ok() const noexcept {
		return reason == bind_reason::none;
	}
};

} // namespace ctxml

namespace ctxml::detail {

using bt_element = ctlark::text<'e', 'l', 'e', 'm', 'e', 'n', 't'>;
using bt_attr = ctlark::text<'a', 't', 't', 'r'>;
using bt_TEXT = ctlark::text<'T', 'E', 'X', 'T'>;
using bt_CDATA = ctlark::text<'C', 'D', 'A', 'T', 'A'>;
using bt_CLOSE = ctlark::text<'C', 'L', 'O', 'S', 'E'>;

constexpr int bind_hexval(char c) noexcept {
	if (c >= '0' && c <= '9') { return c - '0'; }
	if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
	return c - 'A' + 10;
}

constexpr bool is_xml_blank(char c) noexcept {
	return c == ' ' || c == '\x09' || c == '\x0A' || c == '\x0D';
}

// --- decoding entity references in a span (From, To are the number of
// characters to strip from either end: quotes for attribute values)

template <typename Text, size_t From, size_t To> struct decode_entities {
	struct out_t {
		char buf[Text::size() + 1]{};
		size_t len = 0;
		bool ok = true;
	};

	static constexpr void put_code_point(out_t & o, unsigned long cp) noexcept {
		// XML forbids NUL, surrogates and beyond-Unicode references
		if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
			o.ok = false;
			return;
		}
		if (cp < 0x80) {
			o.buf[o.len++] = static_cast<char>(cp);
		} else if (cp < 0x800) {
			o.buf[o.len++] = static_cast<char>(0xC0 | (cp >> 6));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			o.buf[o.len++] = static_cast<char>(0xE0 | (cp >> 12));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			o.buf[o.len++] = static_cast<char>(0xF0 | (cp >> 18));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	static constexpr out_t compute() noexcept {
		out_t o{};
		constexpr std::string_view raw = Text::view();
		size_t i = From;
		const size_t end = raw.size() - To;
		while (i < end && o.ok) {
			const char c = raw[i];
			if (c != '&') {
				o.buf[o.len++] = c;
				++i;
				continue;
			}
			// the grammar guarantees a well-formed reference with ;
			size_t semi = i + 1;
			while (raw[semi] != ';') { ++semi; }
			const std::string_view name = raw.substr(i + 1, semi - i - 1);
			if (name == "lt") {
				o.buf[o.len++] = '<';
			} else if (name == "gt") {
				o.buf[o.len++] = '>';
			} else if (name == "amp") {
				o.buf[o.len++] = '&';
			} else if (name == "apos") {
				o.buf[o.len++] = '\'';
			} else if (name == "quot") {
				o.buf[o.len++] = '"';
			} else if (name.size() > 2 && name[0] == '#' && (name[1] == 'x' || name[1] == 'X')) {
				unsigned long cp = 0;
				for (size_t k = 2; k < name.size(); ++k) {
					cp = cp * 16 + static_cast<unsigned long>(bind_hexval(name[k]));
					if (cp > 0x110000) { break; }
				}
				put_code_point(o, cp);
			} else {
				unsigned long cp = 0;
				for (size_t k = 1; k < name.size(); ++k) {
					cp = cp * 10 + static_cast<unsigned long>(name[k] - '0');
					if (cp > 0x110000) { break; }
				}
				put_code_point(o, cp);
			}
			i = semi + 1;
		}
		return o;
	}

	static constexpr out_t data = compute();
	static constexpr bool ok = data.ok;

	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return ctxml::text<data.buf[I]...>{};
	}
	using type = decltype(lift(std::make_index_sequence<data.len>{}));
};

// --- names and literal spans lifted without decoding

template <typename Text, size_t From, size_t To> struct strip_span {
	static constexpr size_t length = Text::size() - From - To;
	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return ctxml::text<Text::view()[From + I]...>{};
	}
	using type = decltype(lift(std::make_index_sequence<length>{}));
};

// "<name" -> name
template <typename Token> struct open_name {
	using type = typename strip_span<typename Token::value_type, 1, 0>::type;
};
// "</name  >" -> name (trailing whitespace and > trimmed)
template <typename Token> struct close_name {
	static constexpr size_t trailing() noexcept {
		constexpr std::string_view raw = Token::value_type::view();
		size_t n = 1; // the >
		while (is_xml_blank(raw[raw.size() - 1 - n])) { ++n; }
		return n;
	}
	using type = typename strip_span<typename Token::value_type, 2, trailing()>::type;
};

// --- text pieces: TEXT decodes entities, CDATA strips its markers

template <typename Node> struct text_piece;
template <typename Value> struct text_piece<ctlark::token<bt_TEXT, Value>> {
	using decoded = decode_entities<Value, 0, 0>;
	using type = typename decoded::type;
	static constexpr bool ok = decoded::ok;
	static constexpr bind_error_t fail =
		decoded::ok ? bind_error_t{} : bind_error_t{bind_reason::bad_reference, Value::view()};
};
template <typename Value> struct text_piece<ctlark::token<bt_CDATA, Value>> {
	// <![CDATA[ ... ]]>
	using type = typename strip_span<Value, 9, 3>::type;
	static constexpr bool ok = true;
	static constexpr bind_error_t fail{};
};

template <typename T> struct is_text_piece : std::false_type { };
template <typename V> struct is_text_piece<ctlark::token<bt_TEXT, V>> : std::true_type { };
template <typename V> struct is_text_piece<ctlark::token<bt_CDATA, V>> : std::true_type { };

template <auto... A, auto... B> constexpr auto text_cat(ctxml::text<A...>, ctxml::text<B...>) noexcept {
	return ctxml::text<A..., B...>{};
}

template <typename Text> constexpr bool text_blank(Text) noexcept {
	for (const char c : Text::view()) {
		if (!is_xml_blank(c)) { return false; }
	}
	return true;
}

// --- attributes

template <typename Node> struct bind_attr;
template <typename Name, typename Value> struct bind_attr<ctlark::tree<bt_attr, Name, Value>> {
	using name_type = typename strip_span<typename Name::value_type, 0, 0>::type;
	using decoded = decode_entities<typename Value::value_type, 1, 1>;
	using type = ctxml::attribute<name_type, typename decoded::type>;
	static constexpr bool ok = decoded::ok;
	static constexpr bind_error_t fail =
		decoded::ok ? bind_error_t{} : bind_error_t{bind_reason::bad_reference, Value::value_type::view()};
};

template <typename T> struct is_attr_node : std::false_type { };
template <typename N, typename V> struct is_attr_node<ctlark::tree<bt_attr, N, V>> : std::true_type { };

template <typename T> struct is_close_token : std::false_type { };
template <typename V> struct is_close_token<ctlark::token<bt_CLOSE, V>> : std::true_type { };

template <typename Name, typename... Ms> constexpr size_t count_name(ctll::list<Ms...>) noexcept {
	return (static_cast<size_t>(std::is_same_v<Name, typename Ms::name_type>) + ... + 0);
}
template <typename... As> constexpr bool attrs_unique(ctll::list<As...>) noexcept {
	return ((count_name<typename As::name_type>(ctll::list<As...>{}) == 1) && ... && true);
}

// --- the element binder: children are [OPEN, attr*, (content* CLOSE)?]

template <typename Node> struct bind;

template <bool B> using bc = std::bool_constant<B>;
struct no_text { };

// result carriers
template <typename List, bool Ok> struct content_done { };                    // self-closing
template <typename CloseTok, typename List, bool Ok> struct content_closed { }; // with a close tag
template <typename AttrList, bool Ok, typename... Rest> struct attr_phase { };

// phase 1: attributes, until the first non-attr node
template <typename... As, bool Ok>
constexpr auto take_attrs(ctll::list<As...>, bc<Ok>) noexcept {
	return attr_phase<ctll::list<As...>, Ok>{};
}
template <typename... As, bool Ok, typename Head, typename... Rest>
constexpr auto take_attrs(ctll::list<As...>, bc<Ok>, Head, Rest... rest) noexcept {
	if constexpr (is_attr_node<Head>::value) {
		using bound = bind_attr<Head>;
		return take_attrs(ctll::list<As..., typename bound::type>{}, bc<Ok && bound::ok>{}, rest...);
	} else {
		return attr_phase<ctll::list<As...>, Ok, Head, Rest...>{};
	}
}

// phase 2: content, merging adjacent text pieces and dropping blanks
template <typename... Ds, typename P, bool Ok>
constexpr auto flush_pending(ctll::list<Ds...>, P, bc<Ok>) noexcept {
	if constexpr (std::is_same_v<P, no_text>) {
		return content_done<ctll::list<Ds...>, Ok>{};
	} else if constexpr (text_blank(P{})) {
		return content_done<ctll::list<Ds...>, Ok>{};
	} else {
		return content_done<ctll::list<Ds..., P>, Ok>{};
	}
}

template <typename CloseTok, typename List, bool Ok>
constexpr auto close_with(content_done<List, Ok>) noexcept {
	return content_closed<CloseTok, List, Ok>{};
}

template <typename... Ds, typename P, bool Ok>
constexpr auto fold_content(ctll::list<Ds...> done, P pending, bc<Ok> ok) noexcept {
	return flush_pending(done, pending, ok);
}
template <typename... Ds, typename P, bool Ok, typename Head, typename... Rest>
constexpr auto fold_content(ctll::list<Ds...> done, P pending, bc<Ok>, Head, Rest... rest) noexcept {
	if constexpr (is_close_token<Head>::value) {
		// the grammar puts CLOSE last; flush and remember the token
		auto flushed = flush_pending(done, pending, bc<Ok>{});
		return close_with<Head>(flushed);
	} else if constexpr (is_text_piece<Head>::value) {
		using piece = text_piece<Head>;
		if constexpr (std::is_same_v<P, no_text>) {
			return fold_content(done, typename piece::type{}, bc<Ok && piece::ok>{}, rest...);
		} else {
			return fold_content(done, text_cat(pending, typename piece::type{}), bc<Ok && piece::ok>{}, rest...);
		}
	} else {
		// a child element: any pending text is flushed before it
		using child = bind<Head>;
		if constexpr (std::is_same_v<P, no_text>) {
			return fold_content(ctll::list<Ds..., typename child::type>{}, no_text{}, bc<Ok && child::ok>{}, rest...);
		} else if constexpr (text_blank(P{})) {
			return fold_content(ctll::list<Ds..., typename child::type>{}, no_text{}, bc<Ok && child::ok>{}, rest...);
		} else {
			return fold_content(ctll::list<Ds..., P, typename child::type>{}, no_text{}, bc<Ok && child::ok>{}, rest...);
		}
	}
}

// phase 3: assembly
template <typename Name, typename AttrList, typename Content> struct assemble;
template <typename Name, typename AttrList, typename... Cs>
struct assemble<Name, AttrList, ctll::list<Cs...>> {
	using type = ctxml::element<Name, AttrList, Cs...>;
};

template <typename Name, typename AttrList, bool AOk, typename ContentResult> struct finish_element;
// self-closing: no content, no close tag to match
template <typename Name, typename AttrList, bool AOk, typename List, bool COk>
struct finish_element<Name, AttrList, AOk, content_done<List, COk>> {
	using type = typename assemble<Name, AttrList, List>::type;
	static constexpr bool ok = AOk && COk;
};
// with a close tag: the names must be the same TYPE
template <typename Name, typename AttrList, bool AOk, typename CloseTok, typename List, bool COk>
struct finish_element<Name, AttrList, AOk, content_closed<CloseTok, List, COk>> {
	using type = typename assemble<Name, AttrList, List>::type;
	static constexpr bool ok = AOk && COk
	    && std::is_same_v<Name, typename close_name<CloseTok>::type>;
};

template <typename AttrList, bool AOk, typename... Rest> struct run_content;
template <typename... As, bool AOk, typename... Rest>
struct run_content<ctll::list<As...>, AOk, Rest...> {
	using result = decltype(fold_content(ctll::list<>{}, no_text{}, bc<true>{}, Rest{}...));
	static constexpr bool attrs_ok = AOk && attrs_unique(ctll::list<As...>{});
};

template <typename Open, typename AP> struct element_from;
template <typename Open, typename AttrList, bool AOk, typename... Rest>
struct element_from<Open, attr_phase<AttrList, AOk, Rest...>> {
	using name = typename open_name<Open>::type;
	using rc = run_content<AttrList, AOk, Rest...>;
	using fin = finish_element<name, AttrList, rc::attrs_ok, typename rc::result>;
	using type = typename fin::type;
	static constexpr bool ok = fin::ok;
};

// --- the diagnostic pass: the same checks as the ok fold, but as a
// plain value computation that reports the FIRST failure and the raw
// offending token (kept apart from the type-building fold so the
// reason costs nothing unless bind_error<>() is asked for)

template <typename T> struct attr_info {
	static constexpr std::string_view name{};
};
template <typename N, typename V> struct attr_info<ctlark::tree<bt_attr, N, V>> {
	static constexpr std::string_view name = N::value_type::view();
};

template <typename T> struct close_info {
	static constexpr std::string_view raw{};
};
template <typename V> struct close_info<ctlark::token<bt_CLOSE, V>> {
	static constexpr std::string_view raw = V::view();
};

template <typename K> constexpr bind_error_t kid_fail() noexcept {
	if constexpr (is_attr_node<K>::value) {
		return bind_attr<K>::fail;
	} else if constexpr (is_text_piece<K>::value) {
		return text_piece<K>::fail;
	} else if constexpr (is_close_token<K>::value) {
		return bind_error_t{};
	} else {
		return bind<K>::fail; // a child element, recursively
	}
}

template <typename Open, typename... Kids> constexpr bind_error_t element_fail() noexcept {
	// entity/reference failures and child elements, in document order
	const bind_error_t kid_fails[] = {kid_fail<Kids>()..., bind_error_t{}};
	for (const bind_error_t & f : kid_fails) {
		if (f.reason != bind_reason::none) { return f; }
	}
	// attribute names must be unique within this element
	constexpr size_t n = sizeof...(Kids);
	const std::string_view attr_names[] = {attr_info<Kids>::name..., std::string_view{}};
	for (size_t i = 0; i < n; ++i) {
		if (attr_names[i].empty()) { continue; }
		for (size_t j = i + 1; j < n; ++j) {
			if (attr_names[i] == attr_names[j]) {
				return bind_error_t{bind_reason::duplicate_attribute, attr_names[j]};
			}
		}
	}
	// the close tag (last kid, when not self-closing) must match
	const std::string_view closes[] = {close_info<Kids>::raw..., std::string_view{}};
	std::string_view close_raw{};
	for (const std::string_view & c : closes) {
		if (!c.empty()) { close_raw = c; }
	}
	if (!close_raw.empty()) {
		std::string_view cname = close_raw.substr(2, close_raw.size() - 3); // strip "</" and ">"
		while (!cname.empty() && is_xml_blank(cname[cname.size() - 1])) {
			cname = cname.substr(0, cname.size() - 1);
		}
		if (cname != Open::value_type::view().substr(1)) { // open is "<name"
			return bind_error_t{bind_reason::mismatched_tag, close_raw};
		}
	}
	return bind_error_t{};
}

template <typename Open, typename... Kids>
struct bind<ctlark::tree<bt_element, Open, Kids...>> {
	using phase = decltype(take_attrs(ctll::list<>{}, bc<true>{}, Kids{}...));
	using built = element_from<Open, phase>;
	using type = typename built::type;
	static constexpr bool ok = built::ok;
	static constexpr bind_error_t fail = element_fail<Open, Kids...>();
};

} // namespace ctxml::detail


#endif
