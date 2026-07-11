#ifndef CTLARK__EARLEY__HPP
#define CTLARK__EARLEY__HPP

#include "compile.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// The compile-time parsing pipeline over the lowered grammar tables:
//
//   1. lexer: all terminal NFAs simulated together, longest match wins
//      (ties: explicit priority, then literals over patterns, then
//      definition order); %ignore'd terminals are dropped
//   2. Earley over the token stream - handles every context-free
//      grammar, including left recursion and ambiguity, with the
//      Aycock-Horspool nullable fix
//   3. derivation extraction with lark's tree shaping: helper rules
//      from desugaring and _rules splice, ?rules inline when they have
//      a single child, anonymous/keyword/_TERMINAL tokens are filtered
//      (kept under !rules), -> aliases rename
//
// The result is a flat constexpr node array that lift.hpp raises into
// Tree/Token types. Ambiguous derivations are resolved
// deterministically: first-listed alternative, then longest-first
// splits.

namespace ctlark::detail {

enum class perr : unsigned char {
	none,
	lex,       // no terminal matches at err_pos
	parse,     // the token stream does not derive from the start rule
	overflow,  // an internal pool was exhausted
	depth      // derivation recursion limit (cyclic nullable rules)
};

struct eitem {
	int prod;
	int dot;
	int origin;
};

// one node of the extracted parse tree; names and values are spans
// into the grammar's string pool and the input, respectively
struct rnode {
	bool is_token;
	int name_off;
	int name_len;
	int val_off;    // tokens: value span in the input
	int val_len;
	int child_off;  // trees: children in parse_result::children
	int child_count;
};

template <size_t M> struct parse_result {
	static constexpr int node_cap = static_cast<int>(8 * (M + 2) + 128);
	static constexpr int child_cap = static_cast<int>(8 * (M + 2) + 128);

	bool ok = false;
	perr err = perr::none;
	int err_pos = 0;

	rnode nodes[static_cast<size_t>(node_cap)]{};
	int node_count = 0;
	int children[static_cast<size_t>(child_cap)]{};
	int child_count = 0;
	int root = -1;
};

// --- the contextual lexer + Earley pipeline
//
// Lexing is interleaved with parsing, like lark's contextual lexers:
// after an Earley set is closed, only the terminals some item expects
// (plus the %ignore set) are candidates at the current position. That
// is what lets keyword-vs-identifier and XML-style tag-vs-text
// languages tokenize: "true" lexes as an unquoted key where a key is
// expected and as the keyword where a value is.

struct lex_token {
	int sym;
	int off;
	int len;
};

// the number of dotted positions, an upper bound on distinct
// (prod, dot) pairs per set
template <typename GT> constexpr int dotted_positions(const GT & g) noexcept {
	int d = 0;
	for (int p = 0; p < g.prod_count; ++p) { d += g.prods[p].rhs_len + 1; }
	return d;
}

template <typename GT, int ItemCap, int SetCap> struct chart {
	eitem items[static_cast<size_t>(ItemCap)]{};
	int item_count = 0;
	int set_off[static_cast<size_t>(SetCap)]{};
	int set_count = 0;
	bool overflow = false;

	constexpr bool contains(int from, int prod, int dot, int origin) const noexcept {
		for (int i = from; i < item_count; ++i) {
			if (items[i].prod == prod && items[i].dot == dot && items[i].origin == origin) { return true; }
		}
		return false;
	}
	constexpr void push(int set_start, int prod, int dot, int origin) noexcept {
		if (contains(set_start, prod, dot, origin)) { return; }
		if (item_count >= ItemCap) {
			overflow = true;
			return;
		}
		items[item_count++] = eitem{prod, dot, origin};
	}
};

template <typename GT, size_t M> struct pipeline_result {
	lex_token toks[M + 1]{};
	int count = 0;
	bool ok = false;
	perr err = perr::none;
	int err_pos = 0;
};

// close set i: run predictions and completions to a fixpoint
template <typename GT, int ItemCap, int SetCap>
constexpr void close_set(const GT & g, chart<GT, ItemCap, SetCap> & ch, int i) noexcept {
	const int set_start = ch.set_off[i];
	for (int ix = set_start; ix < ch.item_count && !ch.overflow; ++ix) {
		const eitem it = ch.items[ix];
		const cprod & pr = g.prods[it.prod];
		if (it.dot < pr.rhs_len) {
			const int nxt_sym = g.rhs_pool[pr.rhs_off + it.dot];
			if (!g.syms[nxt_sym].terminal) {
				// predict
				for (int p = 0; p < g.prod_count; ++p) {
					if (g.prods[p].lhs == nxt_sym) { ch.push(set_start, p, 0, i); }
				}
				// Aycock-Horspool: nullable nonterminals also advance
				if (g.nullable[nxt_sym]) { ch.push(set_start, it.prod, it.dot + 1, it.origin); }
			}
		} else {
			// complete
			const int lhs = pr.lhs;
			const int parent_end = it.origin == i ? ch.item_count : ch.set_off[it.origin + 1];
			for (int j = ch.set_off[it.origin]; j < parent_end; ++j) {
				const eitem parent = ch.items[j];
				const cprod & ppr = g.prods[parent.prod];
				if (parent.dot < ppr.rhs_len && g.rhs_pool[ppr.rhs_off + parent.dot] == lhs) {
					ch.push(set_start, parent.prod, parent.dot + 1, parent.origin);
				}
			}
		}
	}
}

// is the start rule completed over the whole token span?
template <typename GT, int ItemCap, int SetCap>
constexpr bool accepted(const GT & g, int start_sym, const chart<GT, ItemCap, SetCap> & ch, int i) noexcept {
	for (int ix = ch.set_off[i]; ix < ch.item_count; ++ix) {
		const eitem it = ch.items[ix];
		if (g.prods[it.prod].lhs == start_sym && it.origin == 0 && it.dot == g.prods[it.prod].rhs_len) {
			return true;
		}
	}
	return false;
}

// the interleaved pipeline: close a set, lex among expected terminals,
// scan, repeat
template <typename GT, size_t M, int ItemCap, int SetCap>
constexpr pipeline_result<GT, M> run_pipeline(const GT & g, int start_sym, std::string_view in,
                                              chart<GT, ItemCap, SetCap> & ch) noexcept {
	pipeline_result<GT, M> r{};
	bool expected[static_cast<size_t>(GT::lim::syms)]{};
	bool cur[static_cast<size_t>(GT::lim::states)]{};
	bool nxt[static_cast<size_t>(GT::lim::states)]{};
	int accept_len[static_cast<size_t>(GT::lim::syms)]{};

	ch.set_off[0] = 0;
	for (int p = 0; p < g.prod_count; ++p) {
		if (g.prods[p].lhs == start_sym) { ch.push(0, p, 0, 0); }
	}

	size_t pos = 0;
	int i = 0;
	while (true) {
		close_set(g, ch, i);
		ch.set_off[i + 1] = ch.item_count;
		ch.set_count = i + 2;
		if (ch.overflow) {
			r.err = perr::overflow;
			return r;
		}

		// which terminals does some item expect here?
		for (int t = 0; t < g.sym_count; ++t) { expected[t] = false; }
		for (int ix = ch.set_off[i]; ix < ch.set_off[i + 1]; ++ix) {
			const eitem it = ch.items[ix];
			const cprod & pr = g.prods[it.prod];
			if (it.dot < pr.rhs_len) {
				const int nxt_sym = g.rhs_pool[pr.rhs_off + it.dot];
				if (g.syms[nxt_sym].terminal) { expected[nxt_sym] = true; }
			}
		}

		// lex: expected terminals plus the ignored ones; skips loop here
		int winner = -1;
		while (pos < in.size()) {
			for (int s = 0; s < g.state_count; ++s) { cur[s] = false; }
			for (int t = 0; t < g.sym_count; ++t) {
				accept_len[t] = -1;
				if (g.syms[t].terminal && g.syms[t].nfa_start >= 0 && (expected[t] || g.syms[t].ignored)) {
					cur[g.syms[t].nfa_start] = true;
				}
			}
			int len = 0;
			bool alive = true;
			while (alive) {
				bool grew = true;
				while (grew) {
					grew = false;
					for (int s = 0; s < g.state_count; ++s) {
						if (!cur[s]) { continue; }
						for (int e = g.state_first_edge[s]; e >= 0; e = g.edges[e].next) {
							if (g.edges[e].eps && !cur[g.edges[e].to]) {
								cur[g.edges[e].to] = true;
								grew = true;
							}
						}
					}
				}
				if (len > 0) {
					for (int t = 0; t < g.sym_count; ++t) {
						if (g.syms[t].terminal && g.syms[t].nfa_accept >= 0 && (expected[t] || g.syms[t].ignored)
						    && cur[g.syms[t].nfa_accept]) {
							accept_len[t] = len;
						}
					}
				}
				if (pos + static_cast<size_t>(len) >= in.size()) { break; }
				const int c = static_cast<unsigned char>(in[pos + static_cast<size_t>(len)]);
				bool any = false;
				for (int s = 0; s < g.state_count; ++s) { nxt[s] = false; }
				for (int s = 0; s < g.state_count; ++s) {
					if (!cur[s]) { continue; }
					for (int e = g.state_first_edge[s]; e >= 0; e = g.edges[e].next) {
						if (!g.edges[e].eps && g.edges[e].mask.get(c)) {
							nxt[g.edges[e].to] = true;
							any = true;
						}
					}
				}
				if (!any) {
					alive = false;
				} else {
					for (int s = 0; s < g.state_count; ++s) { cur[s] = nxt[s]; }
					++len;
				}
			}
			// longest, then priority, then literal over pattern, then
			// definition order; expected beats ignored-only on a tie
			int best = -1;
			for (int t = 0; t < g.sym_count; ++t) {
				if (accept_len[t] <= 0) { continue; }
				if (best < 0) {
					best = t;
					continue;
				}
				if (accept_len[t] != accept_len[best]) {
					if (accept_len[t] > accept_len[best]) { best = t; }
					continue;
				}
				if (expected[t] != expected[best]) {
					if (expected[t]) { best = t; }
					continue;
				}
				if (g.syms[t].prio != g.syms[best].prio) {
					if (g.syms[t].prio > g.syms[best].prio) { best = t; }
					continue;
				}
				const bool t_lit = g.syms[t].keyword;
				const bool b_lit = g.syms[best].keyword;
				if (t_lit != b_lit) {
					if (t_lit) { best = t; }
					continue;
				}
			}
			if (best < 0) {
				r.err = perr::lex;
				r.err_pos = static_cast<int>(pos);
				return r;
			}
			if (expected[best]) {
				winner = best;
				break;
			}
			// ignored: skip and lex again in the same set
			pos += static_cast<size_t>(accept_len[best]);
		}

		if (winner < 0) {
			// input exhausted (possibly after trailing ignored tokens)
			if (pos >= in.size() && accepted(g, start_sym, ch, i)) {
				r.ok = true;
				r.count = i;
				return r;
			}
			r.err = perr::parse;
			r.err_pos = static_cast<int>(pos);
			return r;
		}

		// scan: advance every item expecting the winner into set i+1
		if (r.count > static_cast<int>(M)) {
			r.err = perr::overflow;
			r.err_pos = static_cast<int>(pos);
			return r;
		}
		r.toks[r.count++] = lex_token{winner, static_cast<int>(pos), accept_len[winner]};
		const int set_start = ch.set_off[i];
		const int set_end = ch.set_off[i + 1];
		for (int ix = set_start; ix < set_end; ++ix) {
			const eitem it = ch.items[ix];
			const cprod & pr = g.prods[it.prod];
			if (it.dot < pr.rhs_len && g.rhs_pool[pr.rhs_off + it.dot] == winner) {
				ch.push(set_end, it.prod, it.dot + 1, it.origin);
			}
		}
		if (ch.item_count == set_end) {
			// nothing advanced: cannot happen (the winner was expected)
			r.err = perr::parse;
			r.err_pos = static_cast<int>(pos);
			return r;
		}
		pos += static_cast<size_t>(accept_len[winner]);
		++i;
		if (i + 2 >= SetCap) {
			r.err = perr::overflow;
			return r;
		}
	}
}

// --- derivation extraction

template <typename GT, typename RT, int ItemCap, int SetCap> struct tree_builder {
	const GT & g;
	const chart<GT, ItemCap, SetCap> & ch;
	const lex_token * toks;
	int ntoks;
	std::string_view input;
	RT & out;
	bool failed = false;
	perr fail_kind = perr::none;

	static constexpr int max_kids = 256;
	static constexpr int max_depth = 256;

	constexpr void fail(perr k) noexcept {
		if (!failed) {
			failed = true;
			fail_kind = k;
		}
	}

	constexpr bool completed(int prod, int origin, int end) const noexcept {
		for (int i = ch.set_off[end]; i < ch.set_off[end + 1]; ++i) {
			if (ch.items[i].prod == prod && ch.items[i].origin == origin
			    && ch.items[i].dot == g.prods[prod].rhs_len) {
				return true;
			}
		}
		return false;
	}

	constexpr bool derivable(int sym, int s, int e) const noexcept {
		for (int p = 0; p < g.prod_count; ++p) {
			if (g.prods[p].lhs == sym && completed(p, s, e)) { return true; }
		}
		return false;
	}

	// find split points for prod p over token span [s, e): splits[i] is
	// where rhs symbol i starts; splits[rhs_len] == e
	constexpr bool find_splits(int p, int s, int e, int * splits, int i, int pos, int depth) noexcept {
		if (depth > max_depth) {
			fail(perr::depth);
			return false;
		}
		const cprod & pr = g.prods[p];
		splits[i] = pos;
		if (i == pr.rhs_len) { return pos == e; }
		const int sym = g.rhs_pool[pr.rhs_off + i];
		if (g.syms[sym].terminal) {
			if (pos < e && toks[pos].sym == sym) {
				return find_splits(p, s, e, splits, i + 1, pos + 1, depth + 1);
			}
			return false;
		}
		// nonterminal: try candidate ends, longest first
		for (int q = e; q >= pos; --q) {
			if (!derivable(sym, pos, q)) { continue; }
			if (find_splits(p, s, e, splits, i + 1, q, depth + 1)) { return true; }
		}
		return false;
	}

	constexpr int add_node(rnode n) noexcept {
		if (out.node_count >= RT::node_cap) {
			fail(perr::overflow);
			return -1;
		}
		out.nodes[out.node_count] = n;
		return out.node_count++;
	}

	constexpr int commit_children(const int * kids, int count) noexcept {
		if (out.child_count + count > RT::child_cap) {
			fail(perr::overflow);
			return -1;
		}
		const int off = out.child_count;
		for (int i = 0; i < count; ++i) { out.children[out.child_count++] = kids[i]; }
		return off;
	}

	// derive nonterminal sym over [s, e), appending 0..n node ids into
	// kids (splicing flattens here); keep_all propagates ! through
	// helpers
	constexpr void derive(int sym, int s, int e, bool keep_all, int * kids, int & nkids, int depth) noexcept {
		if (failed) { return; }
		if (depth > max_depth) {
			fail(perr::depth);
			return;
		}
		// first completed production, in definition order
		int splits[static_cast<size_t>(GT::lim::max_rhs_len) + 1]{};
		int chosen = -1;
		for (int p = 0; p < g.prod_count && chosen < 0; ++p) {
			if (g.prods[p].lhs != sym) { continue; }
			if (!completed(p, s, e)) { continue; }
			if (find_splits(p, s, e, splits, 0, s, depth)) { chosen = p; }
			if (failed) { return; }
		}
		if (chosen < 0) {
			fail(perr::parse);
			return;
		}
		const cprod & pr = g.prods[chosen];
		const csym & sm = g.syms[sym];
		const bool splice = sm.splice || sm.inlined;
		const bool own_keep = sm.splice ? keep_all : sm.bang;

		int local[max_kids]{};
		int nlocal = 0;
		int * dst = splice ? kids : local;
		int & ndst = splice ? nkids : nlocal;

		for (int i = 0; i < pr.rhs_len && !failed; ++i) {
			const int child_sym = g.rhs_pool[pr.rhs_off + i];
			if (g.syms[child_sym].terminal) {
				const lex_token & tk = toks[splits[i]];
				if (!g.filtered(child_sym) || own_keep) {
					if (ndst >= max_kids) {
						fail(perr::overflow);
						return;
					}
					const int id = add_node(rnode{true, g.syms[child_sym].name_off, g.syms[child_sym].name_len,
					                              tk.off, tk.len, 0, 0});
					if (id < 0) { return; }
					dst[ndst++] = id;
				}
			} else {
				derive(child_sym, splits[i], splits[i + 1], own_keep, dst, ndst, depth + 1);
			}
		}
		if (failed || splice) { return; }

		// ?rule with a single child collapses to that child, unless the
		// chosen alternative is aliased (the alias forces a node)
		if (sm.cond && nlocal == 1 && pr.alias_off < 0) {
			if (nkids >= max_kids) {
				fail(perr::overflow);
				return;
			}
			kids[nkids++] = local[0];
			return;
		}
		// tree node: aliased name if the production has one
		int name_off = sm.name_off;
		int name_len = sm.name_len;
		if (pr.alias_off >= 0) {
			name_off = pr.alias_off;
			name_len = pr.alias_len;
		}
		const int coff = commit_children(local, nlocal);
		if (coff < 0) { return; }
		const int id = add_node(rnode{false, name_off, name_len, 0, 0, coff, nlocal});
		if (id < 0) { return; }
		if (nkids >= max_kids) {
			fail(perr::overflow);
			return;
		}
		kids[nkids++] = id;
	}
};

// --- the whole pipeline

template <const auto & G, const auto & In, int StartSym>
constexpr auto run_parse() noexcept {
	constexpr size_t M = In.size();
	using GT = std::remove_cv_t<std::remove_reference_t<decltype(G)>>;
	parse_result<M> r{};

	// the input is a ctll::fixed_string (char32_t units holding bytes)
	char buf[M + 1]{};
	for (size_t i = 0; i < M; ++i) { buf[i] = static_cast<char>(In[i]); }
	const std::string_view in{buf, M};

	if (!G.ok) {
		r.err = perr::parse;
		return r;
	}

	constexpr int item_cap = (dotted_positions(G) + 16) * (static_cast<int>(M) + 2) * 2;
	constexpr int set_cap = static_cast<int>(M) + 3;
	chart<GT, item_cap, set_cap> ch{};
	const auto pipe = run_pipeline<GT, M>(G, StartSym, in, ch);
	if (!pipe.ok) {
		r.err = pipe.err;
		r.err_pos = pipe.err_pos;
		return r;
	}

	using TB = tree_builder<GT, parse_result<M>, item_cap, set_cap>;
	TB tb{G, ch, pipe.toks, pipe.count, in, r};
	int kids[TB::max_kids]{};
	int nkids = 0;
	tb.derive(StartSym, 0, pipe.count, false, kids, nkids, 0);
	if (tb.failed) {
		r.err = tb.fail_kind;
		return r;
	}
	if (nkids == 1) {
		r.root = kids[0];
	} else {
		// a spliced or empty start: wrap what remains under the start name
		const int coff = tb.commit_children(kids, nkids);
		if (tb.failed) {
			r.err = tb.fail_kind;
			return r;
		}
		r.root = tb.add_node(rnode{false, G.syms[StartSym].name_off, G.syms[StartSym].name_len, 0, 0, coff, nkids});
		if (tb.failed) {
			r.err = tb.fail_kind;
			return r;
		}
	}
	r.ok = true;
	return r;
}

} // namespace ctlark::detail

#endif
