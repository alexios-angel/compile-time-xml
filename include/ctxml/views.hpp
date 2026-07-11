#ifndef CTXML__VIEWS__HPP
#define CTXML__VIEWS__HPP

#include "types.hpp"
#ifndef CTXML_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#endif

// Iterators over compile-time documents. Every child of an element has
// its own TYPE, so no ordinary iterator can hand them out one by one;
// what CAN be uniform is a view - the kind, the name and the text
// behind it. begin/end yield exactly that, out of static storage, so
// range-for, standard algorithms and constexpr loops all work:
//
//   for (const auto & n : doc) { ... n.type, n.name, n.text ... }
//   for (const auto & a : ctxml::attributes(doc)) { ... a.name, a.value ... }
//
// Element children view their tag and direct text, text children just
// their content - dispatch on .type when the distinction matters. For
// type-preserving iteration (each child with its own accessors),
// for_each_child and for_each_attribute remain the tools.

namespace ctxml {

CTLL_EXPORT constexpr attribute_range attributes(node_view node) noexcept {
	return node.attributes();
}

namespace detail {

// one static array per element type, materialized only when iterated
template <typename... Children> struct child_views {
	static constexpr std::array<node_view, sizeof...(Children)> data{view_of<Children>()...};
};

template <typename... Attributes> struct attr_views {
	static constexpr std::array<attribute_view, sizeof...(Attributes)> data{
	    attribute_view{Attributes::name_type::view(), Attributes::value_type::view()}...};
};

template <typename Node> struct child_views_for_impl;
template <typename Name, typename... Attributes, typename... Children>
struct child_views_for_impl<element<Name, ctll::list<Attributes...>, Children...>> : child_views<Children...> {};
template <typename Node> using child_views_for = child_views_for_impl<Node>;

template <typename Node> struct attr_views_for_impl;
template <typename Name, typename... Attributes, typename... Children>
struct attr_views_for_impl<element<Name, ctll::list<Attributes...>, Children...>> : attr_views<Attributes...> {};
template <typename Node> using attr_views_for = attr_views_for_impl<Node>;

template <typename Node> constexpr node_view view_of() noexcept {
	if constexpr (Node::type == kind::element) {
		return {kind::element, Node::name(), Node::text(),
		        child_views_for<Node>::data.data(), attr_views_for<Node>::data.data(),
		        Node::child_count(), Node::attribute_count()};
	} else {
		return {kind::text, {}, Node::view()};
	}
}

} // namespace detail

CTLL_EXPORT template <typename Name, typename... Attributes, typename... Children>
constexpr const node_view * begin(element<Name, ctll::list<Attributes...>, Children...>) noexcept {
	return detail::child_views<Children...>::data.data();
}
CTLL_EXPORT template <typename Name, typename... Attributes, typename... Children>
constexpr const node_view * end(element<Name, ctll::list<Attributes...>, Children...>) noexcept {
	return detail::child_views<Children...>::data.data() + sizeof...(Children);
}

// the attributes, as an iterable array of name/value views
CTLL_EXPORT template <typename Name, typename... Attributes, typename... Children>
constexpr const std::array<attribute_view, sizeof...(Attributes)> &
attributes(element<Name, ctll::list<Attributes...>, Children...>) noexcept {
	return detail::attr_views<Attributes...>::data;
}

} // namespace ctxml

#endif
