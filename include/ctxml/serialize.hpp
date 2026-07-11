#ifndef CTXML__SERIALIZE__HPP
#define CTXML__SERIALIZE__HPP

#include "types.hpp"
#ifndef CTXML_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#endif

// Compile-time serialization: ctxml::serialize(doc) renders any element
// back to minified XML in static storage and returns a std::string_view
// of it - nothing happens at runtime.
//
//   constexpr auto doc = ctxml::parse<"<a  x='1' >hi<b/></a>">();
//   static_assert(ctxml::serialize(doc) == R"(<a x="1">hi<b/></a>)");
//
// Attribute values are double-quoted with & < " escaped; text content
// escapes & < >; everything else, including multi-byte UTF-8, passes
// through as-is. Elements without children use the self-closing form.

namespace ctxml {

namespace detail {

constexpr size_t escaped_size(char c, bool in_attribute) noexcept {
	switch (c) {
		case '&': return 5;                       // &amp;
		case '<': return 4;                       // &lt;
		case '>': return in_attribute ? 1 : 4;    // &gt;
		case '"': return in_attribute ? 6 : 1;    // &quot;
		default: return 1;
	}
}

constexpr char * write_escaped(char * out, char c, bool in_attribute) noexcept {
	const auto put = [&](std::string_view s) {
		for (const char e : s) {
			*out++ = e;
		}
		return out;
	};
	switch (c) {
		case '&': return put("&amp;");
		case '<': return put("&lt;");
		case '>': return in_attribute ? (put(std::string_view{&c, 1})) : put("&gt;");
		case '"': return in_attribute ? put("&quot;") : put(std::string_view{&c, 1});
		default:
			*out++ = c;
			return out;
	}
}

// --- size pass

template <auto... Cs> constexpr size_t serialized_size(text<Cs...>) noexcept {
	size_t total = 0;
	((total += escaped_size(static_cast<char>(Cs), false)), ...);
	return total;
}

template <typename Name, typename... Attributes, typename... Children>
constexpr size_t serialized_size(element<Name, ctll::list<Attributes...>, Children...>) noexcept {
	size_t total = 1 + Name::size(); // <name
	// attributes: space name="value"
	((total += 1 + Attributes::name_type::size() + 2 + [] {
		size_t value = 0;
		constexpr auto view = Attributes::value_type::view();
		for (const char c : view) {
			value += escaped_size(c, true);
		}
		return value;
	}() + 1), ...);
	if constexpr (sizeof...(Children) == 0) {
		total += 2; // />
	} else {
		total += 1; // >
		((total += serialized_size(Children{})), ...);
		total += 2 + Name::size() + 1; // </name>
	}
	return total;
}

// --- write pass

template <auto... Cs> constexpr char * serialize_to(char * out, text<Cs...>) noexcept {
	((out = write_escaped(out, static_cast<char>(Cs), false)), ...);
	return out;
}

constexpr char * write_raw(char * out, std::string_view piece) noexcept {
	for (const char c : piece) {
		*out++ = c;
	}
	return out;
}

template <typename Name, typename... Attributes, typename... Children>
constexpr char * serialize_to(char * out, element<Name, ctll::list<Attributes...>, Children...>) noexcept {
	*out++ = '<';
	out = write_raw(out, Name::view());
	((out = write_raw(out, " "),
	  out = write_raw(out, Attributes::name_type::view()),
	  out = write_raw(out, "=\""),
	  [&] {
		for (const char c : Attributes::value_type::view()) {
			out = write_escaped(out, c, true);
		}
	  }(),
	  out = write_raw(out, "\"")), ...);
	if constexpr (sizeof...(Children) == 0) {
		out = write_raw(out, "/>");
	} else {
		*out++ = '>';
		((out = serialize_to(out, Children{})), ...);
		out = write_raw(out, "</");
		out = write_raw(out, Name::view());
		*out++ = '>';
	}
	return out;
}

// the rendered document lives in static storage, one array per type
template <typename Node> struct serialized_storage {
	static constexpr size_t length = serialized_size(Node{});
	// one extra element keeps the rendering null-terminated
	static constexpr std::array<char, length + 1> compute() noexcept {
		std::array<char, length + 1> out{};
		serialize_to(out.data(), Node{});
		return out;
	}
	static constexpr std::array<char, length + 1> content = compute();
};

} // namespace detail

// minified XML for any element or text node, in static storage
CTLL_EXPORT template <typename Node> constexpr std::string_view serialize(Node = Node{}) noexcept {
	using storage = detail::serialized_storage<Node>;
	return std::string_view{storage::content.data(), storage::length};
}

} // namespace ctxml

#endif
