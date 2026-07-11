#ifndef CTLARK__AST__HPP
#define CTLARK__AST__HPP

#include "text.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// The type-level AST of a Lark grammar, built by the semantic actions
// (actions.hpp) while CTLL parses the grammar text. Each node knows how
// to lower itself into the flat constexpr expression program that
// compile.hpp works with: emit(builder) appends nodes and returns the
// index of the root it created. The builder is duck-typed (see
// compile.hpp for the real one) so this header stays independent.

namespace ctlark::ast {

// flat program node kinds
enum class pk : unsigned char {
	seq,       // children in order
	alt,       // children are alternatives
	star,      // one child, zero or more
	plus,      // one child, one or more
	opt,       // one child, zero or one
	rep,       // one child, a=min, b=max (-1 = unbounded)
	chr,       // a = the character
	range,     // a = low, b = high
	any,       // any character (except newline, unless dotall)
	str,       // literal string: a = pool offset, b = length
	cls,       // character class, a = 1 if negated, children are members
	cw,        // \w   word character
	cd,        // \d   digit
	cs,        // \s   whitespace
	cnw,       // \W
	cnd,       // \D
	cns,       // \S
	rref,      // rule reference, a = symbol id
	tref,      // terminal reference, a = symbol id
	ci,        // one child, match case-insensitively
	dotall,    // one child, any also matches newline
	rx         // one child, provenance: came from a /regex/ literal
};

// --- expression nodes

template <typename... Es> struct seq {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::seq);
		int last = -1;
		((last = b.link(self, last, Es::emit(b))), ...);
		return self;
	}
};

template <typename... Es> struct alt {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::alt);
		int last = -1;
		((last = b.link(self, last, Es::emit(b))), ...);
		return self;
	}
};

template <typename E> struct star {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::star);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

template <typename E> struct plus {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::plus);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

template <typename E> struct opt {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::opt);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

template <typename E, int Min, int Max> struct rep {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::rep, Min, Max);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

template <auto C> struct chr {
	template <typename B> static constexpr int emit(B & b) {
		return b.add(pk::chr, static_cast<int>(C));
	}
};

template <auto Lo, auto Hi> struct crange {
	template <typename B> static constexpr int emit(B & b) {
		return b.add(pk::range, static_cast<int>(Lo), static_cast<int>(Hi));
	}
};

struct any_char {
	template <typename B> static constexpr int emit(B & b) {
		return b.add(pk::any);
	}
};

template <typename Text, bool Ci> struct lit {
	using text_type = Text;
	static constexpr bool case_insensitive = Ci;
	template <typename B> static constexpr int emit(B & b) {
		return b.add_str(Text::view(), Ci);
	}
};

struct word_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cw); }
};
struct digit_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cd); }
};
struct space_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cs); }
};
struct not_word_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cnw); }
};
struct not_digit_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cnd); }
};
struct not_space_class {
	template <typename B> static constexpr int emit(B & b) { return b.add(pk::cns); }
};

template <bool Neg, typename... Members> struct cls {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::cls, Neg ? 1 : 0);
		int last = -1;
		((last = b.link(self, last, Members::emit(b))), ...);
		return self;
	}
};

template <typename Name> struct rule_ref {
	using name_type = Name;
	template <typename B> static constexpr int emit(B & b) {
		return b.add_ref(Name::view(), false);
	}
};

template <typename Name> struct term_ref {
	using name_type = Name;
	template <typename B> static constexpr int emit(B & b) {
		return b.add_ref(Name::view(), true);
	}
};

template <typename E> struct ci {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::ci);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

template <typename E> struct dotall {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::dotall);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

// provenance wrapper: this expression came from a /regex/ literal, so
// in a rule body it is ONE anonymous terminal, not structure
template <typename E> struct rx {
	template <typename B> static constexpr int emit(B & b) {
		const int self = b.add(pk::rx);
		b.link(self, -1, E::emit(b));
		return self;
	}
};

// --- one alternative of a definition; Alias is void or a text

template <typename Seq, typename Alias> struct branch { };

namespace detail {

template <typename Br> struct branch_parts;
template <typename Seq, typename Alias> struct branch_parts<branch<Seq, Alias>> {
	using seq = Seq;
	using alias = Alias;
	static constexpr bool aliased = !std::is_void_v<Alias>;
	static constexpr std::string_view alias_view() noexcept {
		if constexpr (aliased) {
			return Alias::view();
		} else {
			return std::string_view{};
		}
	}
};

} // namespace detail

// --- top-level items

template <typename Name, bool Bang, bool Cond, int Priority, typename... Branches> struct rule_def {
	using name_type = Name;
	template <typename B> static constexpr void collect(B & b) {
		const int sym = b.def_rule(Name::view(), Bang, Cond, Priority);
		(b.add_alternative(sym, detail::branch_parts<Branches>::seq::emit(b),
		                   detail::branch_parts<Branches>::alias_view()), ...);
	}
};

template <typename Name, int Priority, typename... Branches> struct term_def {
	using name_type = Name;
	static_assert((!detail::branch_parts<Branches>::aliased && ...),
	              "ctlark: aliases (->) are not allowed in terminal definitions");
	template <typename B> static constexpr void collect(B & b) {
		const int sym = b.def_term(Name::view(), Priority);
		(b.add_term_alternative(sym, detail::branch_parts<Branches>::seq::emit(b)), ...);
	}
};

template <typename... Branches> struct ignore_def {
	static_assert((!detail::branch_parts<Branches>::aliased && ...),
	              "ctlark: aliases (->) are not allowed in %ignore");
	template <typename B> static constexpr void collect(B & b) {
		(b.add_ignore(detail::branch_parts<Branches>::seq::emit(b)), ...);
	}
};

// %import path -> alias (or the same name); Segs are the path segments
template <typename Alias, typename... Segs> struct import_def {
	template <typename B> static constexpr void collect(B & b) {
		constexpr size_t n = sizeof...(Segs);
		static_assert(n >= 2, "ctlark: %import needs a dotted path (only common.* is supported)");
		const std::string_view segs[]{Segs::view()...};
		b.import_builtin(segs, n, Alias::view());
	}
};

// the whole grammar, in source order
template <typename... Items> struct grammar {
	static constexpr size_t item_count = sizeof...(Items);
	template <typename B> static constexpr void collect(B & b) {
		(Items::collect(b), ...);
	}
};

} // namespace ctlark::ast

#endif
