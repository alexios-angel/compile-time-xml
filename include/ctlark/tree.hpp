#ifndef CTLARK__TREE__HPP
#define CTLARK__TREE__HPP

#include "text.hpp"
#include "../ctll/utilities.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <type_traits>
#endif

// The parse tree a parse produces, shaped like lark's: Tree nodes with
// a data name and children, Token leaves with a terminal type and a
// value. The whole tree is a TYPE - every name, value and nesting
// level is encoded in template parameters - so the objects are empty
// and every accessor is constexpr and static.
//
// repr() renders lark's textual form:
//
//   Tree(start, [Token(WORD, 'Hello'), Token(WORD, 'World')])
//
// and pretty() the indented form. Both live in static storage.

namespace ctlark {

enum class kind {
	tree,
	token
};

template <typename Type, typename Value> struct token;
template <typename Data, typename... Children> struct tree;

namespace detail {

template <size_t Index, typename Head, typename... Tail> constexpr auto nth() noexcept {
	if constexpr (Index == 0) {
		return Head{};
	} else {
		return nth<Index - 1, Tail...>();
	}
}

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

// --- repr rendering (a size pass, then a fill pass in static storage)

constexpr size_t repr_escaped_size(std::string_view v) noexcept {
	size_t n = 0;
	for (const char c : v) { n += (c == '\'' || c == '\\') ? 2 : 1; }
	return n;
}

struct repr_sink {
	char * out;
	size_t at = 0;
	constexpr void put(std::string_view s) noexcept {
		for (const char c : s) { out[at++] = c; }
	}
	constexpr void put_escaped(std::string_view s) noexcept {
		for (const char c : s) {
			if (c == '\'' || c == '\\') { out[at++] = '\\'; }
			out[at++] = c;
		}
	}
};

template <typename Node> struct repr_size;
template <typename Type, typename Value> struct repr_size<token<Type, Value>> {
	// Token(TYPE, 'value')
	static constexpr size_t value = 6 + Type::size() + 3 + repr_escaped_size(Value::view()) + 2;
};
template <typename Data, typename... Children> struct repr_size<tree<Data, Children...>> {
	// Tree(data, [c1, c2])
	static constexpr size_t value = 5 + Data::size() + 3
		+ (repr_size<Children>::value + ... + 0)
		+ (sizeof...(Children) > 1 ? (sizeof...(Children) - 1) * 2 : 0)
		+ 2;
};

template <typename Node> constexpr void repr_render(repr_sink & s) noexcept;

template <typename Type, typename Value, typename Sink = repr_sink>
constexpr void repr_render_token(repr_sink & s) noexcept {
	s.put("Token(");
	s.put(Type::view());
	s.put(", '");
	s.put_escaped(Value::view());
	s.put("')");
}

template <typename Data, typename... Children>
constexpr void repr_render_tree(repr_sink & s) noexcept {
	s.put("Tree(");
	s.put(Data::view());
	s.put(", [");
	bool first = true;
	((first ? (void)(first = false) : s.put(", "), repr_render<Children>(s)), ...);
	s.put("])");
}

template <typename Node> struct repr_dispatch;
template <typename Type, typename Value> struct repr_dispatch<token<Type, Value>> {
	static constexpr void render(repr_sink & s) noexcept {
		repr_render_token<Type, Value>(s);
	}
};
template <typename Data, typename... Children> struct repr_dispatch<tree<Data, Children...>> {
	static constexpr void render(repr_sink & s) noexcept {
		repr_render_tree<Data, Children...>(s);
	}
};
template <typename Node> constexpr void repr_render(repr_sink & s) noexcept {
	repr_dispatch<Node>::render(s);
}

template <typename Node> struct repr_storage {
	static constexpr size_t length = repr_size<Node>::value;
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t compute() noexcept {
		out_t o{};
		repr_sink s{o.content};
		repr_render<Node>(s);
		return o;
	}
	static constexpr out_t content = compute();
	static constexpr std::string_view view{content.content, length};
};

// --- pretty rendering: data per line, children indented two spaces

template <typename Node> struct pretty_size;
template <typename Type, typename Value> struct pretty_size<token<Type, Value>> {
	static constexpr size_t at(size_t indent) noexcept {
		return indent + Value::size() + 1;
	}
};
template <typename Data, typename... Children> struct pretty_size<tree<Data, Children...>> {
	static constexpr size_t at(size_t indent) noexcept {
		return indent + Data::size() + 1 + (pretty_size<Children>::at(indent + 2) + ... + 0);
	}
};

template <typename Node> struct pretty_dispatch;
template <typename Node> constexpr void pretty_render(repr_sink & s, size_t indent) noexcept {
	pretty_dispatch<Node>::render(s, indent);
}
template <typename Type, typename Value> struct pretty_dispatch<token<Type, Value>> {
	static constexpr void render(repr_sink & s, size_t indent) noexcept {
		for (size_t i = 0; i < indent; ++i) { s.put(" "); }
		s.put(Value::view());
		s.put("\n");
	}
};
template <typename Data, typename... Children> struct pretty_dispatch<tree<Data, Children...>> {
	static constexpr void render(repr_sink & s, size_t indent) noexcept {
		for (size_t i = 0; i < indent; ++i) { s.put(" "); }
		s.put(Data::view());
		s.put("\n");
		(pretty_render<Children>(s, indent + 2), ...);
	}
};

template <typename Node> struct pretty_storage {
	static constexpr size_t length = pretty_size<Node>::at(0);
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t compute() noexcept {
		out_t o{};
		repr_sink s{o.content};
		pretty_render<Node>(s, 0);
		return o;
	}
	static constexpr out_t content = compute();
	static constexpr std::string_view view{content.content, length};
};

} // namespace detail

// --- Token: a terminal match; type() is the terminal name

template <typename Type, typename Value> struct token {
	static constexpr ctlark::kind node_kind = kind::token;
	using type_type = Type;
	using value_type = Value;

	static constexpr bool is_token() noexcept {
		return true;
	}
	static constexpr bool is_tree() noexcept {
		return false;
	}
	static constexpr std::string_view type() noexcept {
		return Type::view();
	}
	static constexpr std::string_view value() noexcept {
		return Value::view();
	}
	static constexpr size_t size() noexcept {
		return Value::size();
	}
	// Token(WORD, 'Hello')
	static constexpr std::string_view repr() noexcept {
		return detail::repr_storage<token>::view;
	}

	constexpr operator std::string_view() const noexcept {
		return value();
	}
	friend constexpr bool operator==(token, std::string_view rhs) noexcept {
		return value() == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, token) noexcept {
		return lhs == value();
	}
#if __cplusplus < 202002L
	friend constexpr bool operator!=(token, std::string_view rhs) noexcept {
		return value() != rhs;
	}
	friend constexpr bool operator!=(std::string_view lhs, token) noexcept {
		return lhs != value();
	}
#endif
};

// --- Tree: a rule match; data() names the rule (or its -> alias)

template <typename Data, typename... Children> struct tree {
	static constexpr ctlark::kind node_kind = kind::tree;
	using data_type = Data;

	static constexpr bool is_token() noexcept {
		return false;
	}
	static constexpr bool is_tree() noexcept {
		return true;
	}
	static constexpr std::string_view data() noexcept {
		return Data::view();
	}

	static constexpr size_t child_count() noexcept {
		return sizeof...(Children);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Children) == 0;
	}
	template <size_t Index> static constexpr auto child() noexcept {
		static_assert(Index < sizeof...(Children), "ctlark: child index out of range");
		return detail::nth<Index, Children...>();
	}

	// --- child trees by data name (get: first match, a compile error
	// when absent)

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name> static constexpr bool contains() noexcept {
		return (child_matches<Name, Children>() || ...);
	}
	template <ctll::fixed_string Name> static constexpr size_t count() noexcept {
		return (static_cast<size_t>(child_matches<Name, Children>()) + ... + 0);
	}
	template <ctll::fixed_string Name> static constexpr auto get() noexcept {
		static_assert((child_matches<Name, Children>() || ...), "ctlark: no child tree with this data");
		return find_child<Name, Children...>();
	}
#else
	template <const auto & Name> static constexpr bool contains() noexcept {
		return (child_matches<Name, Children>() || ...);
	}
	template <const auto & Name> static constexpr size_t count() noexcept {
		return (static_cast<size_t>(child_matches<Name, Children>()) + ... + 0);
	}
	template <const auto & Name> static constexpr auto get() noexcept {
		static_assert((child_matches<Name, Children>() || ...), "ctlark: no child tree with this data");
		return find_child<Name, Children...>();
	}
#endif

	// Tree(start, [Token(WORD, 'Hello'), Token(WORD, 'World')])
	static constexpr std::string_view repr() noexcept {
		return detail::repr_storage<tree>::view;
	}
	// the indented multi-line form, like lark's Tree.pretty()
	static constexpr std::string_view pretty() noexcept {
		return detail::pretty_storage<tree>::view;
	}

private:
#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name, typename Child> static constexpr bool child_matches() noexcept {
#else
	template <const auto & Name, typename Child> static constexpr bool child_matches() noexcept {
#endif
		if constexpr (Child::node_kind == kind::tree) {
			return detail::text_matches<Name, typename Child::data_type>();
		} else {
			return false;
		}
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name, typename Head, typename... Tail> static constexpr auto find_child() noexcept {
#else
	template <const auto & Name, typename Head, typename... Tail> static constexpr auto find_child() noexcept {
#endif
		if constexpr (child_matches<Name, Head>()) {
			return Head{};
		} else {
			return find_child<Name, Tail...>();
		}
	}
};

// compile-time iteration over a tree's children, each with its own type
CTLL_EXPORT template <typename F, typename Data, typename... Children>
constexpr void for_each_child(tree<Data, Children...>, F && f) {
	(f(Children{}), ...);
}

} // namespace ctlark

#endif
