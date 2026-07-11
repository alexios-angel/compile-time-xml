#ifndef CTLARK__ACTIONS__HPP
#define CTLARK__ACTIONS__HPP

#include "lark.hpp"
#include "ast.hpp"
#include "../ctll/list.hpp"
#include "../ctll/grammars.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <type_traits>
#endif

// Semantic actions building the type-level grammar AST while CTLL
// parses the grammar text (the same architecture as CTRE's
// pcre_actions). Atoms accumulate on a type stack above branch
// markers; | collapses them into a branch; closing a definition,
// group or regex collapses branches against the marker its opening
// action pushed. Malformed structure (an unclosed group, a quantifier
// with nothing to repeat, an alias on a group branch, ...) folds to
// ctll::reject, which makes the parse - and the build - fail.

namespace ctlark {

// the parser subject: just a stack of partial results
template <typename Stack = ctll::list<>> struct context {
	using stack_type = Stack;
	constexpr context() noexcept { }
	constexpr context(Stack) noexcept { }
};

template <typename... Content> context(ctll::list<Content...>) -> context<ctll::list<Content...>>;

// parse-time markers
namespace mk {

struct m_bang { };                                     // ! before a rule name
struct m_cond { };                                     // ? before a rule name
struct bmark { };                                      // start of a branch (alternative)
struct gmark { };                                      // ( group
struct mmark { };                                      // [ maybe
struct xmark { };                                      // / regex
struct cmark { };                                      // [ character class
struct cmark_neg { };                                  // [^ character class
template <typename Name, bool Bang, bool Cond, int Prio> struct rmark { }; // rule being defined
template <typename Name, int Prio> struct tmark { };   // terminal being defined
struct imark { };                                      // %ignore
struct pmark { };                                      // %import
template <int High> struct hmark { };                  // pending \x high nibble
template <int Value, bool Neg> struct num { };         // pending number
template <typename Text> struct listed { };            // %import (A, B) entry
template <typename Text> struct alias_mark { };        // %import ... -> alias

} // namespace mk

namespace detail {

// --- classification

template <typename T> struct is_expr : std::false_type { };
template <typename... E> struct is_expr<ast::seq<E...>> : std::true_type { };
template <typename... E> struct is_expr<ast::alt<E...>> : std::true_type { };
template <typename E> struct is_expr<ast::star<E>> : std::true_type { };
template <typename E> struct is_expr<ast::plus<E>> : std::true_type { };
template <typename E> struct is_expr<ast::opt<E>> : std::true_type { };
template <typename E, int Mi, int Ma> struct is_expr<ast::rep<E, Mi, Ma>> : std::true_type { };
template <auto C> struct is_expr<ast::chr<C>> : std::true_type { };
template <auto L, auto H> struct is_expr<ast::crange<L, H>> : std::true_type { };
template <> struct is_expr<ast::any_char> : std::true_type { };
template <typename T, bool Ci> struct is_expr<ast::lit<T, Ci>> : std::true_type { };
template <> struct is_expr<ast::word_class> : std::true_type { };
template <> struct is_expr<ast::digit_class> : std::true_type { };
template <> struct is_expr<ast::space_class> : std::true_type { };
template <> struct is_expr<ast::not_word_class> : std::true_type { };
template <> struct is_expr<ast::not_digit_class> : std::true_type { };
template <> struct is_expr<ast::not_space_class> : std::true_type { };
template <bool N, typename... M> struct is_expr<ast::cls<N, M...>> : std::true_type { };
template <typename N> struct is_expr<ast::rule_ref<N>> : std::true_type { };
template <typename N> struct is_expr<ast::term_ref<N>> : std::true_type { };
template <typename E> struct is_expr<ast::ci<E>> : std::true_type { };
template <typename E> struct is_expr<ast::dotall<E>> : std::true_type { };
template <typename E> struct is_expr<ast::rx<E>> : std::true_type { };

template <typename T> struct is_branch : std::false_type { };
template <typename S, typename A> struct is_branch<ast::branch<S, A>> : std::true_type { };

// --- small conversions

constexpr int hexval(char32_t c) noexcept {
	if (c >= U'0' && c <= U'9') { return static_cast<int>(c - U'0'); }
	if (c >= U'a' && c <= U'f') { return static_cast<int>(c - U'a') + 10; }
	return static_cast<int>(c - U'A') + 10;
}

constexpr char32_t esc_code(char32_t c) noexcept {
	switch (c) {
		case U'n': return U'\x0A';
		case U't': return U'\x09';
		case U'r': return U'\x0D';
		default:   return U'\x0C'; // f
	}
}

template <typename Text> struct first_char_of;
template <auto C0, auto... Cs> struct first_char_of<text<C0, Cs...>> {
	static constexpr auto value = C0;
};

// --- folding pending numbers: [num, expr, ...] becomes [rep, ...]

template <typename... Ts> constexpr auto normalize(ctll::list<Ts...> stack) noexcept {
	return stack;
}
template <int V, bool Neg, typename E, typename... Ts>
constexpr auto normalize(ctll::list<mk::num<V, Neg>, E, Ts...>) noexcept {
	if constexpr (!Neg && is_expr<E>::value) {
		return ctll::list<ast::rep<E, V, V>, Ts...>{};
	} else {
		return ctll::reject{};
	}
}
template <int V, bool Neg> constexpr auto normalize(ctll::list<mk::num<V, Neg>>) noexcept {
	return ctll::reject{};
}

// --- collapsing the atoms of the current branch against its bmark

// fold results travel as list<taken<Seq>, Rest...>
template <typename Seq> struct taken { };

template <typename... As> constexpr auto take_exprs(ast::seq<As...>, ctll::list<>) noexcept {
	return ctll::list<taken<ast::seq<As...>>>{};
}
template <typename... As, typename Head, typename... Ts>
constexpr auto take_exprs(ast::seq<As...>, ctll::list<Head, Ts...>) noexcept {
	if constexpr (is_expr<Head>::value) {
		return take_exprs(ast::seq<Head, As...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::list<taken<ast::seq<As...>>, Head, Ts...>{};
	}
}

// bmark on top: fold the gathered sequence into a branch
template <typename Seq, typename... Ts>
constexpr auto collapse_from(ctll::list<taken<Seq>, mk::bmark, Ts...>) noexcept {
	return ctll::list<ast::branch<Seq, void>, Ts...>{};
}
// no bmark: only legal when nothing was gathered (the branch was
// already collapsed, e.g. by an alias or a preceding newline)
template <typename Seq, typename... Ts>
constexpr auto collapse_from(ctll::list<taken<Seq>, Ts...>) noexcept {
	if constexpr (std::is_same_v<Seq, ast::seq<>>) {
		return ctll::list<Ts...>{};
	} else {
		return ctll::reject{};
	}
}

template <typename... Ts> constexpr auto collapse_branch(ctll::list<Ts...> stack) noexcept {
	auto normalized = normalize(stack);
	if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
		return ctll::reject{};
	} else {
		return collapse_from(take_exprs(ast::seq<>{}, normalized));
	}
}

// strict variant: there MUST be an open branch (the branch action)
template <typename... Ts> constexpr auto collapse_strict_from(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename Seq, typename... Ts>
constexpr auto collapse_strict_from(ctll::list<taken<Seq>, mk::bmark, Ts...>) noexcept {
	return ctll::list<ast::branch<Seq, void>, Ts...>{};
}
template <typename... Ts> constexpr auto collapse_branch_strict(ctll::list<Ts...> stack) noexcept {
	auto normalized = normalize(stack);
	if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
		return ctll::reject{};
	} else {
		return collapse_strict_from(take_exprs(ast::seq<>{}, normalized));
	}
}

// --- gathering collapsed branches back to a scope marker

template <typename... Bs> struct branch_pack { };

template <typename... Bs> constexpr auto take_branches(branch_pack<Bs...>, ctll::list<>) noexcept {
	return ctll::list<branch_pack<Bs...>>{};
}
template <typename... Bs, typename Head, typename... Ts>
constexpr auto take_branches(branch_pack<Bs...>, ctll::list<Head, Ts...>) noexcept {
	if constexpr (is_branch<Head>::value) {
		return take_branches(branch_pack<Head, Bs...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::list<branch_pack<Bs...>, Head, Ts...>{};
	}
}

// --- building the expression a closed group stands for

template <typename Br> struct branch_seq;
template <typename S, typename A> struct branch_seq<ast::branch<S, A>> {
	using type = S;
	static constexpr bool aliased = !std::is_void_v<A>;
};

template <typename... Bs> constexpr auto group_expr(branch_pack<Bs...>) noexcept {
	if constexpr (sizeof...(Bs) == 0 || (branch_seq<Bs>::aliased || ...)) {
		return ctll::reject{}; // no branches, or an alias inside a group
	} else if constexpr (sizeof...(Bs) == 1) {
		return typename branch_seq<Bs...>::type{};
	} else {
		return ast::alt<typename branch_seq<Bs>::type...>{};
	}
}

template <typename Marker, bool Maybe, typename... Ts>
constexpr auto close_scope_gathered(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename Marker, bool Maybe, typename... Bs, typename Found, typename... Ts>
constexpr auto close_scope_gathered(ctll::list<branch_pack<Bs...>, Found, Ts...>) noexcept {
	if constexpr (!std::is_same_v<Found, Marker>) {
		return ctll::reject{}; // mismatched or unclosed scope
	} else {
		auto expr = group_expr(branch_pack<Bs...>{});
		if constexpr (std::is_same_v<decltype(expr), ctll::reject>) {
			return ctll::reject{};
		} else if constexpr (Maybe) {
			return ctll::list<ast::opt<decltype(expr)>, Ts...>{};
		} else if constexpr (std::is_same_v<Marker, mk::xmark>) {
			return ctll::list<ast::rx<decltype(expr)>, Ts...>{};
		} else {
			return ctll::list<decltype(expr), Ts...>{};
		}
	}
}

// close a scope: collapse the open branch, gather branches, match the
// expected marker, push what the scope folds to
template <typename Marker, bool Maybe, typename... Ts>
constexpr auto close_scope(ctll::list<Ts...> stack) noexcept {
	auto collapsed = collapse_branch(stack);
	if constexpr (std::is_same_v<decltype(collapsed), ctll::reject>) {
		return ctll::reject{};
	} else {
		return close_scope_gathered<Marker, Maybe>(take_branches(branch_pack<>{}, collapsed));
	}
}

// --- folding a finished item (rule / terminal / %ignore / %import)

template <typename... Ts> constexpr auto fold_def(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename... Bs, typename Name, bool Bang, bool Cond, int Prio, typename... Ts>
constexpr auto fold_def(ctll::list<branch_pack<Bs...>, mk::rmark<Name, Bang, Cond, Prio>, Ts...>) noexcept {
	return ctll::list<ast::rule_def<Name, Bang, Cond, Prio, Bs...>, Ts...>{};
}
template <typename... Bs, typename Name, int Prio, typename... Ts>
constexpr auto fold_def(ctll::list<branch_pack<Bs...>, mk::tmark<Name, Prio>, Ts...>) noexcept {
	return ctll::list<ast::term_def<Name, Prio, Bs...>, Ts...>{};
}
template <typename... Bs, typename... Ts>
constexpr auto fold_def(ctll::list<branch_pack<Bs...>, mk::imark, Ts...>) noexcept {
	return ctll::list<ast::ignore_def<Bs...>, Ts...>{};
}

// %import folding

template <typename S> constexpr auto last_of(ctll::list<S>) noexcept {
	return S{};
}
template <typename S0, typename S1, typename... Ss>
constexpr auto last_of(ctll::list<S0, S1, Ss...>) noexcept {
	return last_of(ctll::list<S1, Ss...>{});
}

// gather the path segments down to the pmark (prepending restores
// source order)
template <typename... Segs> struct seg_pack { };

template <typename... Ss, typename... Ts>
constexpr auto take_segs(seg_pack<Ss...>, ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename... Ss, typename... Ts>
constexpr auto take_segs(seg_pack<Ss...>, ctll::list<mk::pmark, Ts...>) noexcept {
	return ctll::list<seg_pack<Ss...>, Ts...>{};
}
template <typename... Ss, auto... Cs, typename... Ts>
constexpr auto take_segs(seg_pack<Ss...>, ctll::list<text<Cs...>, Ts...>) noexcept {
	return take_segs(seg_pack<text<Cs...>, Ss...>{}, ctll::list<Ts...>{});
}

// %import path [-> alias]: one import_def
template <typename Alias, typename... Ss, typename... Ts>
constexpr auto import_single(ctll::list<seg_pack<Ss...>, Ts...>) noexcept {
	if constexpr (sizeof...(Ss) == 0) {
		return ctll::reject{};
	} else if constexpr (std::is_void_v<Alias>) {
		using last = decltype(last_of(ctll::list<Ss...>{}));
		return ctll::list<ast::import_def<last, Ss...>, Ts...>{};
	} else {
		return ctll::list<ast::import_def<Alias, Ss...>, Ts...>{};
	}
}
template <typename Alias> constexpr auto import_single(ctll::reject) noexcept {
	return ctll::reject{};
}

template <typename Alias, typename... Ts>
constexpr auto fold_import(ctll::list<Ts...> stack) noexcept {
	return import_single<Alias>(take_segs(seg_pack<>{}, stack));
}

// %import path (A, B): one import_def per listed name
template <typename... Done> struct listed_pack { };

template <typename... Done, typename... Ss, typename... Ts>
constexpr auto import_expand(listed_pack<Done...>, ctll::list<seg_pack<Ss...>, Ts...>) noexcept {
	if constexpr (sizeof...(Ss) == 0 || sizeof...(Done) == 0) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::import_def<Done, Ss..., Done>..., Ts...>{};
	}
}
template <typename... Done> constexpr auto import_expand(listed_pack<Done...>, ctll::reject) noexcept {
	return ctll::reject{};
}

template <typename... Done, typename... Ts>
constexpr auto fold_import_listed(listed_pack<Done...>, ctll::list<Ts...> stack) noexcept {
	return import_expand(listed_pack<Done...>{}, take_segs(seg_pack<>{}, stack));
}
template <typename... Done, typename N, typename... Ts>
constexpr auto fold_import_listed(listed_pack<Done...>, ctll::list<mk::listed<N>, Ts...>) noexcept {
	return fold_import_listed(listed_pack<N, Done...>{}, ctll::list<Ts...>{});
}

// item folding dispatch, by the head of the collapsed stack
template <typename... Ts> constexpr auto fold_item_by_head(ctll::list<Ts...> stack) noexcept {
	return fold_def(take_branches(branch_pack<>{}, stack));
}
template <typename A, typename... Ts>
constexpr auto fold_item_by_head(ctll::list<mk::alias_mark<A>, Ts...>) noexcept {
	return fold_import<A>(ctll::list<Ts...>{});
}
template <typename N, typename... Ts>
constexpr auto fold_item_by_head(ctll::list<mk::listed<N>, Ts...>) noexcept {
	return fold_import_listed(listed_pack<>{}, ctll::list<mk::listed<N>, Ts...>{});
}
template <auto... Cs, typename... Ts>
constexpr auto fold_item_by_head(ctll::list<text<Cs...>, Ts...>) noexcept {
	return fold_import<void>(ctll::list<text<Cs...>, Ts...>{});
}

template <typename... Ts> constexpr auto fold_item(ctll::list<Ts...> stack) noexcept {
	auto collapsed = collapse_branch(stack);
	if constexpr (std::is_same_v<decltype(collapsed), ctll::reject>) {
		return ctll::reject{};
	} else {
		return fold_item_by_head(collapsed);
	}
}

// --- character class member gathering

template <typename T> struct is_cls_member : std::false_type { };
template <auto C> struct is_cls_member<ast::chr<C>> : std::true_type { };
template <auto L, auto H> struct is_cls_member<ast::crange<L, H>> : std::true_type { };
template <> struct is_cls_member<ast::word_class> : std::true_type { };
template <> struct is_cls_member<ast::digit_class> : std::true_type { };
template <> struct is_cls_member<ast::space_class> : std::true_type { };
template <> struct is_cls_member<ast::not_word_class> : std::true_type { };
template <> struct is_cls_member<ast::not_digit_class> : std::true_type { };
template <> struct is_cls_member<ast::not_space_class> : std::true_type { };

template <typename... Ms> struct member_pack { };

template <typename... Ms, typename... Ts>
constexpr auto take_members(member_pack<Ms...>, ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename... Ms, typename... Ts>
constexpr auto take_members(member_pack<Ms...>, ctll::list<mk::cmark, Ts...>) noexcept {
	if constexpr (sizeof...(Ms) == 0) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::cls<false, Ms...>, Ts...>{};
	}
}
template <typename... Ms, typename... Ts>
constexpr auto take_members(member_pack<Ms...>, ctll::list<mk::cmark_neg, Ts...>) noexcept {
	if constexpr (sizeof...(Ms) == 0) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::cls<true, Ms...>, Ts...>{};
	}
}
template <typename... Ms, typename Head, typename... Ts>
constexpr auto take_members(member_pack<Ms...>, ctll::list<Head, Ts...>) noexcept {
	if constexpr (is_cls_member<Head>::value) {
		return take_members(member_pack<Head, Ms...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::reject{};
	}
}

} // namespace detail

// --- the action table

struct lark_actions {

	// helper: wrap a folded stack (or reject) back into a context
	template <typename Result> static constexpr auto wrap(Result) noexcept {
		if constexpr (std::is_same_v<Result, ctll::reject>) {
			return ctll::reject{};
		} else {
			return context<Result>{};
		}
	}

	// --- names and numbers

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::begin_name, ctll::term<V>, context<ctll::list<Ts...>>) {
		auto normalized = detail::normalize(ctll::list<Ts...>{});
		if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
			return ctll::reject{};
		} else {
			return context{ctll::push_front(text<>{}, normalized)};
		}
	}

	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::push_name_char, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<text<Cs..., V>, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::begin_number, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::num<0, false>, Ts...>>{};
	}

	template <auto V, int N, bool Neg, typename... Ts>
	static constexpr auto apply(lark_grammar::push_number_digit, ctll::term<V>, context<ctll::list<mk::num<N, Neg>, Ts...>>) {
		return context<ctll::list<mk::num<N * 10 + static_cast<int>(V - U'0'), Neg>, Ts...>>{};
	}

	template <auto V, int N, typename... Ts>
	static constexpr auto apply(lark_grammar::negate_number, ctll::term<V>, context<ctll::list<mk::num<N, false>, Ts...>>) {
		return context<ctll::list<mk::num<N, true>, Ts...>>{};
	}

	// --- item openers

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_bang, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::m_bang, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_cond, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::m_cond, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_ignore, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::imark, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_import, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::pmark, Ts...>>{};
	}

	// def_rule / def_term: pop [num?, name, m_cond?, m_bang?], push the
	// definition marker and the first branch marker
	template <auto V, int P, bool Neg, auto... Name, typename... Ts>
	static constexpr auto apply(lark_grammar::def_rule, ctll::term<V>, context<ctll::list<mk::num<P, Neg>, text<Name...>, Ts...>>) {
		return def_rule_flags<(Neg ? -P : P), text<Name...>>(ctll::list<Ts...>{});
	}
	template <auto V, auto... Name, typename... Ts>
	static constexpr auto apply(lark_grammar::def_rule, ctll::term<V>, context<ctll::list<text<Name...>, Ts...>>) {
		return def_rule_flags<0, text<Name...>>(ctll::list<Ts...>{});
	}

	template <int P, typename Name, typename... Ts>
	static constexpr auto def_rule_flags(ctll::list<mk::m_cond, mk::m_bang, Ts...>) {
		return context<ctll::list<mk::bmark, mk::rmark<Name, true, true, P>, Ts...>>{};
	}
	template <int P, typename Name, typename... Ts>
	static constexpr auto def_rule_flags(ctll::list<mk::m_cond, Ts...>) {
		return context<ctll::list<mk::bmark, mk::rmark<Name, false, true, P>, Ts...>>{};
	}
	template <int P, typename Name, typename... Ts>
	static constexpr auto def_rule_flags(ctll::list<mk::m_bang, Ts...>) {
		return context<ctll::list<mk::bmark, mk::rmark<Name, true, false, P>, Ts...>>{};
	}
	template <int P, typename Name, typename... Ts>
	static constexpr auto def_rule_flags(ctll::list<Ts...>) {
		return context<ctll::list<mk::bmark, mk::rmark<Name, false, false, P>, Ts...>>{};
	}

	template <auto V, int P, bool Neg, auto... Name, typename... Ts>
	static constexpr auto apply(lark_grammar::def_term, ctll::term<V>, context<ctll::list<mk::num<P, Neg>, text<Name...>, Ts...>>) {
		return context<ctll::list<mk::bmark, mk::tmark<text<Name...>, (Neg ? -P : P)>, Ts...>>{};
	}
	template <auto V, auto... Name, typename... Ts>
	static constexpr auto apply(lark_grammar::def_term, ctll::term<V>, context<ctll::list<text<Name...>, Ts...>>) {
		return context<ctll::list<mk::bmark, mk::tmark<text<Name...>, 0>, Ts...>>{};
	}

	// --- branches

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::branch, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::collapse_branch_strict(ctll::list<Ts...>{}));
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::new_branch, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::bmark, Ts...>>{};
	}

	template <auto V, auto... Cs, typename Seq, typename... Ts>
	static constexpr auto apply(lark_grammar::attach_alias, ctll::term<V>, context<ctll::list<text<Cs...>, ast::branch<Seq, void>, Ts...>>) {
		return context<ctll::list<ast::branch<Seq, text<Cs...>>, Ts...>>{};
	}

	// --- groups, maybes, regexes

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::open_group, ctll::term<V>, context<ctll::list<Ts...>>) {
		auto normalized = detail::normalize(ctll::list<Ts...>{});
		if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
			return ctll::reject{};
		} else {
			return context{ctll::push_front(mk::gmark{}, normalized)};
		}
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::open_maybe, ctll::term<V>, context<ctll::list<Ts...>>) {
		auto normalized = detail::normalize(ctll::list<Ts...>{});
		if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
			return ctll::reject{};
		} else {
			return context{ctll::push_front(mk::mmark{}, normalized)};
		}
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::open_regex, ctll::term<V>, context<ctll::list<Ts...>>) {
		auto normalized = detail::normalize(ctll::list<Ts...>{});
		if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
			return ctll::reject{};
		} else {
			return context{ctll::push_front(mk::xmark{}, normalized)};
		}
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::close_group, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::close_scope<mk::gmark, false>(ctll::list<Ts...>{}));
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::close_maybe, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::close_scope<mk::mmark, true>(ctll::list<Ts...>{}));
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::close_regex, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::close_scope<mk::xmark, false>(ctll::list<Ts...>{}));
	}

	// --- quantifiers

	template <auto V, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_star, ctll::term<V>, context<ctll::list<E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value) {
			return context<ctll::list<ast::star<E>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <auto V, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_plus, ctll::term<V>, context<ctll::list<E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value) {
			return context<ctll::list<ast::plus<E>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <auto V, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_opt, ctll::term<V>, context<ctll::list<E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value) {
			return context<ctll::list<ast::opt<E>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}

	template <auto V, int N, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_rep_exact, ctll::term<V>, context<ctll::list<mk::num<N, false>, E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value && N >= 0) {
			return context<ctll::list<ast::rep<E, N, N>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <auto V, int N, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_rep_open, ctll::term<V>, context<ctll::list<mk::num<N, false>, E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value && N >= 0) {
			return context<ctll::list<ast::rep<E, N, -1>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <auto V, int Hi, int Lo, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::make_rep_range, ctll::term<V>, context<ctll::list<mk::num<Hi, false>, mk::num<Lo, false>, E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value && Lo >= 0 && Hi >= Lo) {
			return context<ctll::list<ast::rep<E, Lo, Hi>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}

	// --- references

	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::make_rule_ref, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<ast::rule_ref<text<Cs...>>, Ts...>>{};
	}
	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::make_term_ref, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<ast::term_ref<text<Cs...>>, Ts...>>{};
	}

	// --- strings

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::begin_string, ctll::term<V>, context<ctll::list<Ts...>>) {
		auto normalized = detail::normalize(ctll::list<Ts...>{});
		if constexpr (std::is_same_v<decltype(normalized), ctll::reject>) {
			return ctll::reject{};
		} else {
			return context{ctll::push_front(text<>{}, normalized)};
		}
	}

	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::push_str_char, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<text<Cs..., V>, Ts...>>{};
	}
	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::push_esc_char, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<text<Cs..., V>, Ts...>>{};
	}
	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::push_esc_code, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<text<Cs..., detail::esc_code(V)>, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::push_hex_high, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::hmark<detail::hexval(V)>, Ts...>>{};
	}
	template <auto V, int H, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::push_hex_low, ctll::term<V>, context<ctll::list<mk::hmark<H>, text<Cs...>, Ts...>>) {
		return context<ctll::list<text<Cs..., static_cast<char32_t>(H * 16 + detail::hexval(V))>, Ts...>>{};
	}

	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::end_string, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<ast::lit<text<Cs...>, false>, Ts...>>{};
	}

	template <auto V, typename T, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_ci, ctll::term<V>, context<ctll::list<ast::lit<T, false>, Ts...>>) {
		return context<ctll::list<ast::lit<T, true>, Ts...>>{};
	}

	template <auto V, typename T2, typename T1, typename... Ts>
	static constexpr auto apply(lark_grammar::make_char_range, ctll::term<V>, context<ctll::list<ast::lit<T2, false>, ast::lit<T1, false>, Ts...>>) {
		if constexpr (T1::size() == 1 && T2::size() == 1) {
			return context<ctll::list<ast::crange<detail::first_char_of<T1>::value, detail::first_char_of<T2>::value>, Ts...>>{};
		} else {
			return ctll::reject{}; // range endpoints must be single characters
		}
	}

	// --- regex bodies

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_push_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<V>, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_dot, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::any_char, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_esc_code, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<detail::esc_code(V)>, Ts...>>{};
	}
	template <auto V, int H, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_hex_char, ctll::term<V>, context<ctll::list<mk::hmark<H>, Ts...>>) {
		return context<ctll::list<ast::chr<static_cast<char32_t>(H * 16 + detail::hexval(V))>, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_word, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::word_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_digit, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::digit_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_space, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::space_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_not_word, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_word_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_not_digit, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_digit_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_class_not_space, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_space_class, Ts...>>{};
	}

	template <auto V, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_flag_ci, ctll::term<V>, context<ctll::list<E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value) {
			return context<ctll::list<ast::ci<E>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}
	template <auto V, typename E, typename... Ts>
	static constexpr auto apply(lark_grammar::rx_flag_dotall, ctll::term<V>, context<ctll::list<E, Ts...>>) {
		if constexpr (detail::is_expr<E>::value) {
			return context<ctll::list<ast::dotall<E>, Ts...>>{};
		} else {
			return ctll::reject{};
		}
	}

	// --- character classes

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::open_class, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<mk::cmark, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::mark_class_neg, ctll::term<V>, context<ctll::list<mk::cmark, Ts...>>) {
		return context<ctll::list<mk::cmark_neg, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_push_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<V>, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_push_rbracket, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<U']'>, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_push_minus, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<U'-'>, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_esc_char, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<V>, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_esc_code, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::chr<detail::esc_code(V)>, Ts...>>{};
	}
	template <auto V, int H, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_hex_char, ctll::term<V>, context<ctll::list<mk::hmark<H>, Ts...>>) {
		return context<ctll::list<ast::chr<static_cast<char32_t>(H * 16 + detail::hexval(V))>, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_word, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::word_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_digit, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::digit_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_space, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::space_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_not_word, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_word_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_not_digit, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_digit_class, Ts...>>{};
	}
	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_class_not_space, ctll::term<V>, context<ctll::list<Ts...>>) {
		return context<ctll::list<ast::not_space_class, Ts...>>{};
	}

	template <auto V, auto C1, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_make_range, ctll::term<V>, context<ctll::list<ast::chr<C1>, Ts...>>) {
		return context<ctll::list<ast::crange<C1, V>, Ts...>>{};
	}
	template <auto V, auto C1, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_range_esc_char, ctll::term<V>, context<ctll::list<ast::chr<C1>, Ts...>>) {
		return context<ctll::list<ast::crange<C1, V>, Ts...>>{};
	}
	template <auto V, auto C1, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_range_esc_code, ctll::term<V>, context<ctll::list<ast::chr<C1>, Ts...>>) {
		return context<ctll::list<ast::crange<C1, detail::esc_code(V)>, Ts...>>{};
	}
	template <auto V, int H, auto C1, typename... Ts>
	static constexpr auto apply(lark_grammar::cls_range_hex_char, ctll::term<V>, context<ctll::list<mk::hmark<H>, ast::chr<C1>, Ts...>>) {
		return context<ctll::list<ast::crange<C1, static_cast<char32_t>(H * 16 + detail::hexval(V))>, Ts...>>{};
	}

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::close_class, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::take_members(detail::member_pack<>{}, ctll::list<Ts...>{}));
	}

	// --- imports

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::import_seg, ctll::term<V>, context<ctll::list<Ts...>> subject) {
		return subject; // segments stay on the stack as texts
	}
	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::import_alias, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<mk::alias_mark<text<Cs...>>, Ts...>>{};
	}
	template <auto V, auto... Cs, typename... Ts>
	static constexpr auto apply(lark_grammar::import_list_add, ctll::term<V>, context<ctll::list<text<Cs...>, Ts...>>) {
		return context<ctll::list<mk::listed<text<Cs...>>, Ts...>>{};
	}

	// --- finishing an item

	template <auto V, typename... Ts>
	static constexpr auto apply(lark_grammar::end_item, ctll::term<V>, context<ctll::list<Ts...>>) {
		return wrap(detail::fold_item(ctll::list<Ts...>{}));
	}
};

} // namespace ctlark

#endif
