#ifndef CTXML__TYPES__HPP
#define CTXML__TYPES__HPP

#include "../ctll/fixed_string.hpp"
#ifndef CTXML_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <type_traits>
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

struct attribute_view {
	std::string_view name;
	std::string_view value;
};

struct attribute_range {
	const attribute_view * data = nullptr;
	size_t count = 0;
	constexpr const attribute_view * begin() const noexcept { return data; }
	constexpr const attribute_view * end() const noexcept { return data + (data ? count : 0); }
};

struct node_view {
	ctxml::kind type = kind::text;
	std::string_view tag{};
	std::string_view content{};
	const node_view * child_data = nullptr;
	const attribute_view * attribute_data = nullptr;
	size_t children = 0;
	size_t attrs = 0;

	constexpr std::string_view name() const noexcept { return tag; }
	constexpr std::string_view text() const noexcept { return content; }
	constexpr size_t child_count() const noexcept { return children; }
	constexpr bool empty() const noexcept { return children == 0; }
	constexpr node_view operator[](std::string_view name) const noexcept {
		for (size_t i = 0; i < children; ++i)
			if (child_data[i].type == kind::element && child_data[i].tag == name) return child_data[i];
		return {};
	}
	constexpr node_view operator[](size_t index) const noexcept {
		return index < children ? child_data[index] : node_view{};
	}
	constexpr bool contains(std::string_view name) const noexcept {
		for (size_t i = 0; i < children; ++i)
			if (child_data[i].type == kind::element && child_data[i].tag == name) return true;
		return false;
	}
	constexpr size_t count(std::string_view name) const noexcept {
		size_t n = 0;
		for (size_t i = 0; i < children; ++i) n += child_data[i].type == kind::element && child_data[i].tag == name;
		return n;
	}
	constexpr bool has_attribute(std::string_view name) const noexcept {
		for (size_t i = 0; i < attrs; ++i) if (attribute_data[i].name == name) return true;
		return false;
	}
	constexpr std::string_view attribute(std::string_view name) const noexcept {
		for (size_t i = 0; i < attrs; ++i) if (attribute_data[i].name == name) return attribute_data[i].value;
		return {};
	}
	constexpr const node_view * begin() const noexcept { return child_data; }
	constexpr const node_view * end() const noexcept { return child_data + (child_data ? children : 0); }
	constexpr attribute_range attributes() const noexcept { return {attribute_data, attrs}; }
};

namespace detail {

template <typename Node> constexpr node_view view_of() noexcept;

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

	constexpr node_view operator[](std::string_view tag) const noexcept { return detail::view_of<element>()[tag]; }
	constexpr node_view operator[](size_t index) const noexcept { return detail::view_of<element>()[index]; }
	constexpr bool contains(std::string_view tag) const noexcept { return detail::view_of<element>().contains(tag); }
	constexpr size_t count(std::string_view tag) const noexcept { return detail::view_of<element>().count(tag); }

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

} // namespace ctxml

#endif
