#ifndef CTXML__TYPES__HPP
#define CTXML__TYPES__HPP

#include "../ctll/fixed_string.hpp"
#ifndef CTXML_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#endif

// The document types a parse produces. The whole document is a TYPE -
// every name, attribute and nesting level is encoded in template
// parameters - so the values here are empty structs whose accessors are
// all constexpr and static.
//
// Text content is stored as UTF-8 bytes with entities already decoded.
// An element's children are its child elements and its non-whitespace
// text nodes, in document order.

namespace ctxml {

enum class kind {
	element,
	text
};

// --- text, used for text nodes, names and attribute values alike

template <auto... Chars> struct text {
	static constexpr kind type = kind::text;

	// null-terminated so c_str()/data() work as C strings; size() excludes it
	static constexpr char storage[sizeof...(Chars) + 1]{static_cast<char>(Chars)..., '\0'};

	static constexpr const char * c_str() noexcept {
		return storage;
	}
	static constexpr size_t size() noexcept {
		return sizeof...(Chars);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Chars) == 0;
	}
	static constexpr std::string_view view() noexcept {
		return std::string_view{storage, sizeof...(Chars)};
	}
	constexpr operator std::string_view() const noexcept {
		return view();
	}
	template <auto... Rhs> constexpr bool operator==(text<Rhs...>) const noexcept {
		return view() == text<Rhs...>::view();
	}
	friend constexpr bool operator==(text, std::string_view rhs) noexcept {
		return view() == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, text) noexcept {
		return lhs == view();
	}
};

// --- attributes

template <typename Name, typename Value> struct attribute {
	using name_type = Name;
	using value_type = Value;
};

namespace detail {

// compare a compile-time key against a text type's content
#if CTLL_CNTTP_COMPILER_CHECK
template <ctll::fixed_string Key, typename Text> constexpr bool text_matches() noexcept {
#else
template <const auto & Key, typename Text> constexpr bool text_matches() noexcept {
#endif
	constexpr auto view = Text::view();
	if (Key.size() != view.size()) {
		return false;
	}
	for (size_t i = 0; i < view.size(); ++i) {
		if (static_cast<char32_t>(static_cast<unsigned char>(view[i])) != Key[i]) {
			return false;
		}
	}
	return true;
}

// concatenated storage for an element's direct text children
template <typename Child> constexpr size_t text_size_of() noexcept {
	if constexpr (Child::type == kind::text) {
		return Child::size();
	} else {
		return 0;
	}
}

template <typename Child> constexpr std::string_view text_view_of() noexcept {
	if constexpr (Child::type == kind::text) {
		return Child::view();
	} else {
		return std::string_view{};
	}
}

template <typename... Children> struct joined_text {
	static constexpr size_t length = (text_size_of<Children>() + ... + 0);
	static constexpr auto compute() noexcept {
		struct out_t {
			char content[length + 1]{};
		} out{};
		size_t at = 0;
		const auto append = [&](std::string_view piece) {
			for (const char c : piece) {
				out.content[at++] = c;
			}
		};
		(append(text_view_of<Children>()), ...);
		return out;
	}
	static constexpr auto content = compute();
	static constexpr std::string_view view{content.content, length};
};

template <size_t Index, typename Head, typename... Tail> constexpr auto nth() noexcept {
	if constexpr (Index == 0) {
		return Head{};
	} else {
		return nth<Index - 1, Tail...>();
	}
}

} // namespace detail

// --- elements

template <typename Name, typename Attributes, typename... Children> struct element;

template <typename Name, typename... Attributes, typename... Children>
struct element<Name, ctll::list<Attributes...>, Children...> {
	static constexpr kind type = kind::element;

	using name_type = Name;

	static constexpr std::string_view name() noexcept {
		return Name::view();
	}

	// --- attributes

	static constexpr size_t attribute_count() noexcept {
		return sizeof...(Attributes);
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Key> static constexpr bool has_attribute() noexcept {
		return (detail::text_matches<Key, typename Attributes::name_type>() || ...);
	}
	// the attribute's value; a missing name is a compile-time error
	template <ctll::fixed_string Key> static constexpr auto attribute() noexcept {
		static_assert((detail::text_matches<Key, typename Attributes::name_type>() || ...), "ctxml: no attribute with this name");
		return find_attribute<Key, Attributes...>();
	}
#else
	// C++17: the key is a ctll::fixed_string variable with linkage
	template <const auto & Key> static constexpr bool has_attribute() noexcept {
		return (detail::text_matches<Key, typename Attributes::name_type>() || ...);
	}
	template <const auto & Key> static constexpr auto attribute() noexcept {
		static_assert((detail::text_matches<Key, typename Attributes::name_type>() || ...), "ctxml: no attribute with this name");
		return find_attribute<Key, Attributes...>();
	}
#endif

	// positional access, for iterating attributes
	template <size_t Index> static constexpr auto attribute_name() noexcept {
		static_assert(Index < sizeof...(Attributes), "ctxml: attribute index out of range");
		return typename decltype(detail::nth<Index, Attributes...>())::name_type{};
	}
	template <size_t Index> static constexpr auto attribute_value() noexcept {
		static_assert(Index < sizeof...(Attributes), "ctxml: attribute index out of range");
		return typename decltype(detail::nth<Index, Attributes...>())::value_type{};
	}

	// --- children (child elements and non-whitespace text, in order)

	static constexpr size_t child_count() noexcept {
		return sizeof...(Children);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Children) == 0;
	}

	template <size_t Index> static constexpr auto child() noexcept {
		static_assert(Index < sizeof...(Children), "ctxml: child index out of range");
		return detail::nth<Index, Children...>();
	}

	// the concatenated direct text content: <a>x<b/>y</a> gives "xy"
	static constexpr std::string_view text() noexcept {
		if constexpr (sizeof...(Children) == 0) {
			return std::string_view{};
		} else {
			return detail::joined_text<Children...>::view;
		}
	}

	// --- child elements by tag name

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Tag> static constexpr bool contains() noexcept {
		return (child_matches<Tag, Children>() || ...);
	}
	template <ctll::fixed_string Tag> static constexpr size_t count() noexcept {
		return (static_cast<size_t>(child_matches<Tag, Children>()) + ... + 0);
	}
	// the first child element with this tag; missing is a compile error
	template <ctll::fixed_string Tag> static constexpr auto get() noexcept {
		static_assert((child_matches<Tag, Children>() || ...), "ctxml: no child element with this tag");
		return find_child<Tag, Children...>();
	}
#else
	template <const auto & Tag> static constexpr bool contains() noexcept {
		return (child_matches<Tag, Children>() || ...);
	}
	template <const auto & Tag> static constexpr size_t count() noexcept {
		return (static_cast<size_t>(child_matches<Tag, Children>()) + ... + 0);
	}
	template <const auto & Tag> static constexpr auto get() noexcept {
		static_assert((child_matches<Tag, Children>() || ...), "ctxml: no child element with this tag");
		return find_child<Tag, Children...>();
	}
#endif

	// get and child, spelled with brackets: the tag is a text TYPE, so
	// this works with the "..."_k literal (C++20) and with any name type
	// in any standard; an integral constant (1_i) picks a child by position
	template <auto... Chars> constexpr auto operator[](ctxml::text<Chars...>) const noexcept {
		static_assert((child_name_matches<ctxml::text<Chars...>, Children>() || ...),
		              "ctxml: no child element with this tag");
		return find_child_by_name<ctxml::text<Chars...>, Children...>();
	}
	template <size_t Index> constexpr auto operator[](std::integral_constant<size_t, Index>) const noexcept {
		return child<Index>();
	}

private:
#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Tag, typename Child> static constexpr bool child_matches() noexcept {
#else
	template <const auto & Tag, typename Child> static constexpr bool child_matches() noexcept {
#endif
		if constexpr (Child::type == kind::element) {
			return detail::text_matches<Tag, typename Child::name_type>();
		} else {
			return false;
		}
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Tag, typename Head, typename... Tail> static constexpr auto find_child() noexcept {
#else
	template <const auto & Tag, typename Head, typename... Tail> static constexpr auto find_child() noexcept {
#endif
		if constexpr (child_matches<Tag, Head>()) {
			return Head{};
		} else {
			return find_child<Tag, Tail...>();
		}
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Key, typename Head, typename... Tail> static constexpr auto find_attribute() noexcept {
#else
	template <const auto & Key, typename Head, typename... Tail> static constexpr auto find_attribute() noexcept {
#endif
		if constexpr (detail::text_matches<Key, typename Head::name_type>()) {
			return typename Head::value_type{};
		} else {
			return find_attribute<Key, Tail...>();
		}
	}

	template <typename Tag, typename Child> static constexpr bool child_name_matches() noexcept {
		if constexpr (Child::type == kind::element) {
			return Tag::view() == Child::name_type::view();
		} else {
			return false;
		}
	}

	template <typename Tag, typename Head, typename... Tail> static constexpr auto find_child_by_name() noexcept {
		if constexpr (child_name_matches<Tag, Head>()) {
			return Head{};
		} else {
			return find_child_by_name<Tag, Tail...>();
		}
	}
};

// compile-time iteration over an element's children (each with its own
// type) or its attributes (name, value pairs)
CTLL_EXPORT template <typename F, typename Name, typename... Attributes, typename... Children>
constexpr void for_each_child(element<Name, ctll::list<Attributes...>, Children...>, F && f) {
	(f(Children{}), ...);
}

CTLL_EXPORT template <typename F, typename Name, typename... Attributes, typename... Children>
constexpr void for_each_attribute(element<Name, ctll::list<Attributes...>, Children...>, F && f) {
	(f(typename Attributes::name_type{}, typename Attributes::value_type{}), ...);
}

// --- literal suffixes: tags and indexes as types, for operator[]

namespace detail {

#if CTLL_CNTTP_COMPILER_CHECK
template <ctll::fixed_string S, size_t... I> constexpr auto lift_key(std::index_sequence<I...>) noexcept {
	return text<static_cast<char>(S[I])...>{};
}
#endif

template <char... Digits> constexpr size_t parse_index() noexcept {
	constexpr char digits[]{Digits...};
	size_t value = 0;
	for (const char c : digits) {
		if (c != '\'') { // the digit separator
			value = value * 10 + static_cast<size_t>(c - '0');
		}
	}
	return value;
}

} // namespace detail

// opt in with `using namespace ctxml::literals`
namespace literals {

#if CTLL_CNTTP_COMPILER_CHECK
// "name"_k: a tag as a text type - doc["endpoint"_k]
CTLL_EXPORT template <ctll::fixed_string S> constexpr auto operator""_k() noexcept {
	return detail::lift_key<S>(std::make_index_sequence<S.size()>{});
}
#endif

// 1_i: an index as an integral constant - doc[1_i]
CTLL_EXPORT template <char... Digits> constexpr auto operator""_i() noexcept {
	return std::integral_constant<size_t, detail::parse_index<Digits...>()>{};
}

} // namespace literals

} // namespace ctxml

#endif
