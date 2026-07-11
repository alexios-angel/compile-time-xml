#ifndef CTLARK__LIFT__HPP
#define CTLARK__LIFT__HPP

#include "earley.hpp"
#include "tree.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <utility>
#endif

// Raising the flat constexpr parse result (earley.hpp) into the
// tree<>/token<> TYPES (tree.hpp): names come from the grammar's
// string pool, token values from the input, children by recursion
// over the node ids.

namespace ctlark::detail {

template <const auto & G, int Off> struct pool_text_of {
	template <size_t... I> static constexpr auto make(std::index_sequence<I...>) noexcept {
		return ctlark::text<G.pool[static_cast<size_t>(Off) + I]...>{};
	}
};

template <const auto & In, int Off> struct input_text_of {
	template <size_t... I> static constexpr auto make(std::index_sequence<I...>) noexcept {
		return ctlark::text<static_cast<char>(In[static_cast<size_t>(Off) + I])...>{};
	}
};

template <const auto & G, const auto & In, const auto & R, int Id> struct lift_node {
	static constexpr auto name() noexcept {
		return pool_text_of<G, R.nodes[Id].name_off>::make(
			std::make_index_sequence<static_cast<size_t>(R.nodes[Id].name_len)>{});
	}
	template <size_t... I> static constexpr auto make_tree(std::index_sequence<I...>) noexcept {
		return ctlark::tree<decltype(name()),
			typename lift_node<G, In, R, R.children[static_cast<size_t>(R.nodes[Id].child_off) + I]>::type...>{};
	}
	static constexpr auto make() noexcept {
		if constexpr (R.nodes[Id].is_token) {
			return ctlark::token<decltype(name()),
				decltype(input_text_of<In, R.nodes[Id].val_off>::make(
					std::make_index_sequence<static_cast<size_t>(R.nodes[Id].val_len)>{}))>{};
		} else {
			return make_tree(std::make_index_sequence<static_cast<size_t>(R.nodes[Id].child_count)>{});
		}
	}
	using type = decltype(make());
};

} // namespace ctlark::detail

#endif
