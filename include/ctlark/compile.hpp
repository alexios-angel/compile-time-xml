#ifndef CTLARK__COMPILE__HPP
#define CTLARK__COMPILE__HPP

#include "ast.hpp"
#include "assert.hpp"
#include "common.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#endif

// Lowering the type-level grammar AST into constexpr tables:
//
//   * symbols (rules and terminals) interned by name; string literals
//     in rule bodies become keyword terminals (merged with an existing
//     terminal defined as the same literal), regexes and ranges become
//     anonymous pattern terminals
//   * quantifiers, groups and alternations inside rule bodies are
//     desugared into synthetic helper rules marked splice, so the
//     productions Earley sees are a plain CFG
//   * every terminal pattern is compiled to a Thompson NFA over bytes
//     (edges carry 256-bit masks), with terminal-in-terminal
//     references inlined and case-insensitivity/dotall propagated
//   * nullability is computed for the Aycock-Horspool Earley fix
//
// Everything is plain constexpr evaluation - by the time Earley runs
// (earley.hpp) the grammar is data, not types. Errors set ok=false
// and a static message; the entry points static_assert on ok.

namespace ctlark::detail {

using ast::pk;

// capacities, derived from the grammar text length
template <size_t N> struct climits {
	static constexpr int nodes = static_cast<int>(4 * N + 320);
	static constexpr int pool = static_cast<int>(4 * N + 512);
	static constexpr int syms = static_cast<int>(N + 64);
	static constexpr int alts = static_cast<int>(N + 32);
	static constexpr int prods = static_cast<int>(4 * N + 64);
	static constexpr int rhs = static_cast<int>(8 * N + 128);
	static constexpr int states = static_cast<int>(16 * N + 1024);
	static constexpr int edges = static_cast<int>(16 * N + 1024);
	static constexpr int ignores = 64;
	static constexpr int max_rhs_len = 128;
	static constexpr int max_depth = 64;
};

struct cnode {
	pk kind;
	int a;
	int b;
	int child;
	int sib;
};

struct csym {
	int name_off;
	int name_len;
	bool terminal;
	bool defined;
	bool ignored;
	bool keyword;   // anonymous string literal from a rule body
	bool anon;      // anonymous pattern terminal
	bool ci;        // keyword literal with the i flag
	int lit_off;    // keyword content, for merging (-1 otherwise)
	int lit_len;
	int prio;
	int pattern;    // terminals: root node id (-1 = not defined)
	bool bang;      // rules: ! keep all tokens
	bool cond;      // rules: ? inline when a single child
	bool inlined;   // rules: name starts with _
	bool splice;    // rules: synthetic helper, always splice children
	int nfa_start;
	int nfa_accept;
};

struct calt {
	int sym;
	int root;
	int alias_off;
	int alias_len;
};

struct cprod {
	int lhs;
	int rhs_off;
	int rhs_len;
	int alias_off;
	int alias_len;
};

// a 256-bit byte set
struct mask256 {
	std::uint64_t w[4]{};
	constexpr void set(int c) noexcept {
		w[(c & 0xFF) >> 6] |= (std::uint64_t{1} << (c & 63));
	}
	constexpr bool get(int c) const noexcept {
		return (w[(c & 0xFF) >> 6] >> (c & 63)) & 1;
	}
	constexpr void set_range(int lo, int hi) noexcept {
		for (int c = lo; c <= hi && c <= 0xFF; ++c) { set(c); }
	}
	constexpr void invert() noexcept {
		for (auto & x : w) { x = ~x; }
	}
	constexpr void merge(const mask256 & o) noexcept {
		for (int i = 0; i < 4; ++i) { w[i] |= o.w[i]; }
	}
	// close under ASCII case flipping
	constexpr void ci_close() noexcept {
		for (int c = 'a'; c <= 'z'; ++c) {
			if (get(c)) { set(c - 32); }
		}
		for (int c = 'A'; c <= 'Z'; ++c) {
			if (get(c)) { set(c + 32); }
		}
	}
};

constexpr mask256 mask_digit() noexcept {
	mask256 m{};
	m.set_range('0', '9');
	return m;
}
constexpr mask256 mask_word() noexcept {
	mask256 m = mask_digit();
	m.set_range('a', 'z');
	m.set_range('A', 'Z');
	m.set('_');
	return m;
}
constexpr mask256 mask_space() noexcept {
	mask256 m{};
	m.set(' ');
	m.set_range(0x09, 0x0D); // \t \n \v \f \r
	return m;
}
constexpr mask256 mask_not(mask256 m) noexcept {
	m.invert();
	return m;
}

struct cedge {
	int to;
	bool eps;
	mask256 mask;
	int next; // next edge of the same state
};

struct nfa_frag {
	int in;
	int out;
};

// punctuation names for anonymous keyword terminals (lark-style)
constexpr std::string_view punct_name(char c) noexcept {
	switch (c) {
		case '+': return "PLUS";
		case '-': return "MINUS";
		case '*': return "STAR";
		case '/': return "SLASH";
		case '\\': return "BACKSLASH";
		case '(': return "LPAR";
		case ')': return "RPAR";
		case '[': return "LSQB";
		case ']': return "RSQB";
		case '{': return "LBRACE";
		case '}': return "RBRACE";
		case ',': return "COMMA";
		case ';': return "SEMICOLON";
		case ':': return "COLON";
		case '=': return "EQUAL";
		case '.': return "DOT";
		case '!': return "BANG";
		case '?': return "QMARK";
		case '<': return "LESSTHAN";
		case '>': return "MORETHAN";
		case '|': return "VBAR";
		case '&': return "AMPERSAND";
		case '^': return "CIRCUMFLEX";
		case '%': return "PERCENT";
		case '~': return "TILDE";
		case '@': return "AT";
		case '#': return "HASH";
		case '$': return "DOLLAR";
		case '"': return "DBLQUOTE";
		case '\'': return "QUOTE";
		case '`': return "BACKQUOTE";
		default: return "";
	}
}

template <size_t N> struct grammar_tables {
	using lim = climits<N>;

	bool ok = true;
	char error[128]{};

	cnode nodes[static_cast<size_t>(lim::nodes)]{};
	int node_count = 0;

	char pool[static_cast<size_t>(lim::pool)]{};
	int pool_count = 0;

	csym syms[static_cast<size_t>(lim::syms)]{};
	int sym_count = 0;

	calt alts[static_cast<size_t>(lim::alts)]{};
	int alt_count = 0;

	int ignore_roots[static_cast<size_t>(lim::ignores)]{};
	int ignore_count = 0;

	cprod prods[static_cast<size_t>(lim::prods)]{};
	int prod_count = 0;
	int rhs_pool[static_cast<size_t>(lim::rhs)]{};
	int rhs_count = 0;

	int state_first_edge[static_cast<size_t>(lim::states)]{};
	int state_count = 0;
	cedge edges[static_cast<size_t>(lim::edges)]{};
	int edge_count = 0;

	bool nullable[static_cast<size_t>(lim::syms)]{};
	int anon_counter = 0;
	int helper_counter = 0;

	// --- error handling

	constexpr void fail(std::string_view msg) noexcept {
		if (!ok) { return; }
		ok = false;
		size_t i = 0;
		for (; i < msg.size() && i + 1 < sizeof(error); ++i) { error[i] = msg[i]; }
		error[i] = '\0';
	}
	// the same, naming the offending symbol: "<msg> '<subject>'"
	constexpr void fail(std::string_view msg, std::string_view subject) noexcept {
		if (!ok) { return; }
		ok = false;
		size_t i = 0;
		const auto append = [&](std::string_view s) {
			for (size_t k = 0; k < s.size() && i + 1 < sizeof(error); ++k) { error[i++] = s[k]; }
		};
		append(msg);
		append(" '");
		append(subject);
		append("'");
		error[i] = '\0';
	}
	constexpr std::string_view error_view() const noexcept {
		size_t n = 0;
		while (n < sizeof(error) && error[n] != '\0') { ++n; }
		return std::string_view{error, n};
	}

	// --- pools

	constexpr int pool_add(std::string_view s) noexcept {
		if (pool_count + static_cast<int>(s.size()) > lim::pool) {
			fail("ctlark: grammar too large (string pool)");
			return 0;
		}
		const int off = pool_count;
		for (const char c : s) { pool[static_cast<size_t>(pool_count++)] = c; }
		return off;
	}
	constexpr std::string_view pool_view(int off, int len) const noexcept {
		return std::string_view{pool + off, static_cast<size_t>(len)};
	}
	constexpr std::string_view name_of(int sym) const noexcept {
		return pool_view(syms[sym].name_off, syms[sym].name_len);
	}

	// --- AST emission interface (called by ast::* nodes)

	constexpr int add(pk kind, int a = 0, int b = 0) noexcept {
		if (node_count >= lim::nodes) {
			fail("ctlark: grammar too large (node pool)");
			return 0;
		}
		nodes[node_count] = cnode{kind, a, b, -1, -1};
		return node_count++;
	}
	constexpr int link(int parent, int last, int child) noexcept {
		if (!ok) { return child; }
		if (last < 0) {
			nodes[parent].child = child;
		} else {
			nodes[last].sib = child;
		}
		return child;
	}
	constexpr int add_str(std::string_view s, bool ci) noexcept {
		const int off = pool_add(s);
		const int n = add(pk::str, off, static_cast<int>(s.size()));
		if (ci) {
			const int w = add(pk::ci);
			if (ok) { nodes[w].child = n; }
			return w;
		}
		return n;
	}
	constexpr int intern(std::string_view name, bool terminal) noexcept {
		for (int i = 0; i < sym_count; ++i) {
			if (syms[i].terminal == terminal && name_of(i) == name) { return i; }
		}
		if (sym_count >= lim::syms) {
			fail("ctlark: grammar too large (symbols)");
			return 0;
		}
		csym s{};
		s.name_off = pool_add(name);
		s.name_len = static_cast<int>(name.size());
		s.terminal = terminal;
		s.lit_off = -1;
		s.lit_len = 0;
		s.pattern = -1;
		s.nfa_start = -1;
		s.nfa_accept = -1;
		s.inlined = !terminal && !name.empty() && name[0] == '_';
		syms[sym_count] = s;
		return sym_count++;
	}
	constexpr int add_ref(std::string_view name, bool terminal) noexcept {
		return add(terminal ? pk::tref : pk::rref, intern(name, terminal));
	}
	constexpr int def_rule(std::string_view name, bool bang, bool cond, int prio) noexcept {
		const int s = intern(name, false);
		if (syms[s].defined) { fail("ctlark: duplicate rule definition", name); }
		syms[s].defined = true;
		syms[s].bang = bang;
		syms[s].cond = cond;
		syms[s].prio = prio;
		return s;
	}
	constexpr int def_term(std::string_view name, int prio) noexcept {
		const int s = intern(name, true);
		if (syms[s].defined) { fail("ctlark: duplicate terminal definition", name); }
		syms[s].defined = true;
		syms[s].prio = prio;
		return s;
	}
	constexpr void add_alternative(int sym, int root, std::string_view alias) noexcept {
		if (alt_count >= lim::alts) {
			fail("ctlark: grammar too large (alternatives)");
			return;
		}
		const int aoff = alias.empty() ? -1 : pool_add(alias);
		alts[alt_count++] = calt{sym, root, aoff, static_cast<int>(alias.size())};
	}
	constexpr void add_term_alternative(int sym, int root) noexcept {
		if (!ok) { return; }
		if (syms[sym].pattern < 0) {
			syms[sym].pattern = root;
			return;
		}
		// further alternatives merge under an alt node
		const int existing = syms[sym].pattern;
		if (nodes[existing].kind == pk::alt) {
			int last = nodes[existing].child;
			while (last >= 0 && nodes[last].sib >= 0) { last = nodes[last].sib; }
			nodes[last].sib = root;
		} else {
			const int a = add(pk::alt);
			if (!ok) { return; }
			nodes[a].child = existing;
			nodes[existing].sib = root;
			syms[sym].pattern = a;
		}
	}
	constexpr void add_ignore(int root) noexcept {
		if (ignore_count >= lim::ignores) {
			fail("ctlark: too many %ignore statements");
			return;
		}
		ignore_roots[ignore_count++] = root;
	}
	constexpr void import_builtin(const std::string_view * segs, size_t n, std::string_view alias) noexcept {
		if (n != 2 || segs[0] != "common") {
			fail("ctlark: %import supports only common.<NAME>");
			return;
		}
		const int root = emit_common(segs[1], *this);
		if (root < 0) {
			fail("ctlark: %import: not a supported common.* terminal", segs[1]);
			return;
		}
		const int s = intern(alias, true);
		if (syms[s].defined) {
			fail("ctlark: duplicate terminal definition (%import)", alias);
			return;
		}
		syms[s].defined = true;
		syms[s].pattern = root;
	}

	// --- lowering rule bodies to productions

	struct rhs_buf {
		int ids[static_cast<size_t>(lim::max_rhs_len)]{};
		int len = 0;
	};

	constexpr void buf_push(rhs_buf & b, int sym) noexcept {
		if (b.len >= lim::max_rhs_len) {
			fail("ctlark: rule alternative too long");
			return;
		}
		b.ids[b.len++] = sym;
	}

	constexpr void add_prod(int lhs, const rhs_buf & b, int alias_off, int alias_len) noexcept {
		if (prod_count >= lim::prods || rhs_count + b.len > lim::rhs) {
			fail("ctlark: grammar too large (productions)");
			return;
		}
		const int off = rhs_count;
		for (int i = 0; i < b.len; ++i) { rhs_pool[rhs_count++] = b.ids[i]; }
		prods[prod_count++] = cprod{lhs, off, b.len, alias_off, alias_len};
	}

	constexpr int new_helper() noexcept {
		char name[16]{'_', '_', 'h'};
		int len = 3;
		int v = helper_counter++;
		char digits[8]{};
		int nd = 0;
		do {
			digits[nd++] = static_cast<char>('0' + v % 10);
			v /= 10;
		} while (v > 0);
		while (nd > 0) { name[len++] = digits[--nd]; }
		const int s = intern(std::string_view{name, static_cast<size_t>(len)}, false);
		syms[s].defined = true;
		syms[s].splice = true;
		return s;
	}

	constexpr int new_anon_pattern(int root) noexcept {
		char name[20]{'_', '_', 'a', 'n', 'o', 'n', '_'};
		int len = 7;
		int v = anon_counter++;
		char digits[8]{};
		int nd = 0;
		do {
			digits[nd++] = static_cast<char>('0' + v % 10);
			v /= 10;
		} while (v > 0);
		while (nd > 0) { name[len++] = digits[--nd]; }
		const int s = intern(std::string_view{name, static_cast<size_t>(len)}, true);
		syms[s].defined = true;
		syms[s].anon = true;
		syms[s].pattern = root;
		return s;
	}

	// a keyword terminal for a string literal in a rule body; merged
	// with an existing identical keyword, or with a defined terminal
	// whose whole pattern is the same literal
	constexpr int keyword_sym(int str_node, bool ci) noexcept {
		const int off = nodes[str_node].a;
		const int len = nodes[str_node].b;
		const std::string_view content = pool_view(off, len);
		for (int i = 0; i < sym_count; ++i) {
			if (!syms[i].terminal) { continue; }
			if (syms[i].keyword && syms[i].ci == ci && pool_view(syms[i].lit_off, syms[i].lit_len) == content) {
				return i;
			}
			// a user terminal defined as exactly this literal
			if (syms[i].defined && !syms[i].keyword && syms[i].pattern >= 0) {
				int p = syms[i].pattern;
				bool pci = false;
				if (nodes[p].kind == pk::ci && nodes[p].child >= 0 && nodes[nodes[p].child].sib < 0) {
					pci = true;
					p = nodes[p].child;
				}
				// terminal bodies are seq-rooted; unwrap single-child seqs
				while (nodes[p].kind == pk::seq && nodes[p].child >= 0 && nodes[nodes[p].child].sib < 0) {
					p = nodes[p].child;
					if (nodes[p].kind == pk::ci && nodes[p].child >= 0) {
						pci = true;
						p = nodes[p].child;
					}
				}
				if (nodes[p].kind == pk::str && pci == ci && pool_view(nodes[p].a, nodes[p].b) == content) {
					return i;
				}
			}
		}
		// name it like lark does: IF for word literals, PLUS for + ...
		char name[64]{};
		int nlen = 0;
		bool wordy = !content.empty();
		for (const char c : content) {
			const bool w = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
			if (!w) { wordy = false; }
		}
		if (wordy && content.size() < 32 && !(content[0] >= '0' && content[0] <= '9')) {
			for (const char c : content) {
				name[nlen++] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
			}
		} else if (!content.empty() && content.size() * 12 < 60) {
			bool all_punct = true;
			for (const char c : content) {
				if (punct_name(c).empty()) { all_punct = false; }
			}
			if (all_punct) {
				for (const char c : content) {
					if (nlen > 0) { name[nlen++] = '_'; }
					for (const char pc : punct_name(c)) { name[nlen++] = pc; }
				}
			}
		}
		if (nlen == 0) {
			return finish_keyword(new_anon_pattern(str_node), str_node, ci);
		}
		// collision with an existing symbol name gets the anon treatment
		for (int i = 0; i < sym_count; ++i) {
			if (syms[i].terminal && name_of(i) == std::string_view{name, static_cast<size_t>(nlen)}) {
				return finish_keyword(new_anon_pattern(str_node), str_node, ci);
			}
		}
		const int s = intern(std::string_view{name, static_cast<size_t>(nlen)}, true);
		syms[s].defined = true;
		syms[s].pattern = ci ? wrap_ci(str_node) : str_node;
		return finish_keyword(s, str_node, ci);
	}

	constexpr int wrap_ci(int node) noexcept {
		const int w = add(pk::ci);
		if (ok) { nodes[w].child = node; }
		return w;
	}

	constexpr int finish_keyword(int s, int str_node, bool ci) noexcept {
		syms[s].keyword = true;
		syms[s].ci = ci;
		syms[s].lit_off = nodes[str_node].a;
		syms[s].lit_len = nodes[str_node].b;
		if (syms[s].anon) { syms[s].pattern = ci ? wrap_ci(str_node) : str_node; }
		return s;
	}

	// does this subtree contain a rule reference?
	constexpr bool has_rref(int node, int depth = 0) const noexcept {
		if (node < 0 || depth > lim::max_depth) { return false; }
		if (nodes[node].kind == pk::rref) { return true; }
		for (int c = nodes[node].child; c >= 0; c = nodes[c].sib) {
			if (has_rref(c, depth + 1)) { return true; }
		}
		return false;
	}

	// lower one expression node into a symbol sequence
	constexpr void lower_into(rhs_buf & out, int node, int depth) noexcept {
		if (!ok) { return; }
		if (depth > lim::max_depth) {
			fail("ctlark: rule body too deeply nested");
			return;
		}
		const cnode & n = nodes[node];
		switch (n.kind) {
			case pk::seq:
				for (int c = n.child; c >= 0; c = nodes[c].sib) { lower_into(out, c, depth + 1); }
				return;
			case pk::rref:
			case pk::tref:
				buf_push(out, n.a);
				return;
			case pk::str:
				buf_push(out, keyword_sym(node, false));
				return;
			case pk::ci:
				if (n.child >= 0 && nodes[n.child].kind == pk::str) {
					buf_push(out, keyword_sym(n.child, true));
				} else {
					buf_push(out, new_anon_pattern(node));
				}
				return;
			case pk::rx:
			case pk::dotall:
			case pk::range:
			case pk::any:
			case pk::chr:
			case pk::cls:
			case pk::cw:
			case pk::cd:
			case pk::cs:
			case pk::cnw:
			case pk::cnd:
			case pk::cns:
				buf_push(out, new_anon_pattern(node));
				return;
			case pk::alt: {
				const int h = new_helper();
				for (int c = n.child; c >= 0; c = nodes[c].sib) {
					rhs_buf t{};
					lower_into(t, c, depth + 1);
					add_prod(h, t, -1, 0);
				}
				buf_push(out, h);
				return;
			}
			case pk::star: {
				const int h = new_helper();
				rhs_buf one{};
				lower_into(one, n.child, depth + 1);
				rhs_buf rec{};
				buf_push(rec, h);
				for (int i = 0; i < one.len; ++i) { buf_push(rec, one.ids[i]); }
				add_prod(h, rec, -1, 0);
				const rhs_buf empty{};
				add_prod(h, empty, -1, 0);
				buf_push(out, h);
				return;
			}
			case pk::plus: {
				const int h = new_helper();
				rhs_buf one{};
				lower_into(one, n.child, depth + 1);
				rhs_buf rec{};
				buf_push(rec, h);
				for (int i = 0; i < one.len; ++i) { buf_push(rec, one.ids[i]); }
				add_prod(h, rec, -1, 0);
				add_prod(h, one, -1, 0);
				buf_push(out, h);
				return;
			}
			case pk::opt: {
				const int h = new_helper();
				rhs_buf one{};
				lower_into(one, n.child, depth + 1);
				add_prod(h, one, -1, 0);
				const rhs_buf empty{};
				add_prod(h, empty, -1, 0);
				buf_push(out, h);
				return;
			}
			case pk::rep: {
				const int lo = n.a;
				const int hi = n.b;
				rhs_buf one{};
				lower_into(one, n.child, depth + 1);
				for (int k = 0; k < lo; ++k) {
					for (int i = 0; i < one.len; ++i) { buf_push(out, one.ids[i]); }
				}
				if (hi < 0) {
					// open repetition: a star helper
					const int h = new_helper();
					rhs_buf rec{};
					buf_push(rec, h);
					for (int i = 0; i < one.len; ++i) { buf_push(rec, one.ids[i]); }
					add_prod(h, rec, -1, 0);
					const rhs_buf empty{};
					add_prod(h, empty, -1, 0);
					buf_push(out, h);
				} else if (hi > lo) {
					const int h = new_helper();
					add_prod(h, one, -1, 0);
					const rhs_buf empty{};
					add_prod(h, empty, -1, 0);
					for (int k = lo; k < hi; ++k) { buf_push(out, h); }
				}
				return;
			}
		}
		fail("ctlark: unexpected node in a rule body");
	}

	// --- %ignore resolution

	constexpr void resolve_ignores() noexcept {
		for (int i = 0; i < ignore_count && ok; ++i) {
			int root = ignore_roots[i];
			// unwrap single-child seqs
			while (nodes[root].kind == pk::seq && nodes[root].child >= 0 && nodes[nodes[root].child].sib < 0) {
				root = nodes[root].child;
			}
			if (nodes[root].kind == pk::tref) {
				syms[nodes[root].a].ignored = true;
				continue;
			}
			if (has_rref(root)) {
				fail("ctlark: %ignore must be a terminal pattern");
				return;
			}
			if (nodes[root].kind == pk::str) {
				syms[keyword_sym(root, false)].ignored = true;
			} else if (nodes[root].kind == pk::ci && nodes[root].child >= 0 && nodes[nodes[root].child].kind == pk::str) {
				syms[keyword_sym(nodes[root].child, true)].ignored = true;
			} else {
				syms[new_anon_pattern(root)].ignored = true;
			}
		}
	}

	// --- NFA construction

	constexpr int new_state() noexcept {
		if (state_count >= lim::states) {
			fail("ctlark: terminal patterns too large (states)");
			return 0;
		}
		state_first_edge[state_count] = -1;
		return state_count++;
	}
	constexpr void add_edge(int from, int to, bool eps, mask256 mask = {}) noexcept {
		if (edge_count >= lim::edges) {
			fail("ctlark: terminal patterns too large (edges)");
			return;
		}
		edges[edge_count] = cedge{to, eps, mask, state_first_edge[from]};
		state_first_edge[from] = edge_count++;
	}

	constexpr mask256 member_mask(int node, bool ci) const noexcept {
		mask256 m{};
		const cnode & n = nodes[node];
		switch (n.kind) {
			case pk::chr: m.set(n.a); break;
			case pk::range: m.set_range(n.a, n.b); break;
			case pk::cw: m = mask_word(); break;
			case pk::cd: m = mask_digit(); break;
			case pk::cs: m = mask_space(); break;
			case pk::cnw: m = mask_not(mask_word()); break;
			case pk::cnd: m = mask_not(mask_digit()); break;
			case pk::cns: m = mask_not(mask_space()); break;
			default: break;
		}
		if (ci) { m.ci_close(); }
		return m;
	}

	constexpr nfa_frag char_frag(mask256 m) noexcept {
		const int in = new_state();
		const int out = new_state();
		add_edge(in, out, false, m);
		return nfa_frag{in, out};
	}

	constexpr nfa_frag emit_nfa(int node, bool ci, bool dot, int depth) noexcept {
		if (!ok) { return nfa_frag{0, 0}; }
		if (depth > lim::max_depth) {
			fail("ctlark: terminal pattern too deep (recursive terminals are not supported)");
			return nfa_frag{0, 0};
		}
		const cnode & n = nodes[node];
		switch (n.kind) {
			case pk::seq: {
				nfa_frag f{new_state(), -1};
				f.out = f.in;
				for (int c = n.child; c >= 0; c = nodes[c].sib) {
					const nfa_frag g = emit_nfa(c, ci, dot, depth + 1);
					add_edge(f.out, g.in, true);
					f.out = g.out;
				}
				return f;
			}
			case pk::alt: {
				const int in = new_state();
				const int out = new_state();
				for (int c = n.child; c >= 0; c = nodes[c].sib) {
					const nfa_frag g = emit_nfa(c, ci, dot, depth + 1);
					add_edge(in, g.in, true);
					add_edge(g.out, out, true);
				}
				return nfa_frag{in, out};
			}
			case pk::star: {
				const int in = new_state();
				const int out = new_state();
				const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
				add_edge(in, g.in, true);
				add_edge(in, out, true);
				add_edge(g.out, g.in, true);
				add_edge(g.out, out, true);
				return nfa_frag{in, out};
			}
			case pk::plus: {
				const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
				add_edge(g.out, g.in, true);
				return g;
			}
			case pk::opt: {
				const int in = new_state();
				const int out = new_state();
				const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
				add_edge(in, g.in, true);
				add_edge(in, out, true);
				add_edge(g.out, out, true);
				return nfa_frag{in, out};
			}
			case pk::rep: {
				const int lo = n.a;
				const int hi = n.b;
				nfa_frag f{new_state(), -1};
				f.out = f.in;
				for (int k = 0; k < lo; ++k) {
					const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
					add_edge(f.out, g.in, true);
					f.out = g.out;
				}
				if (hi < 0) {
					const int in2 = new_state();
					const int out2 = new_state();
					const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
					add_edge(f.out, in2, true);
					add_edge(in2, g.in, true);
					add_edge(in2, out2, true);
					add_edge(g.out, g.in, true);
					add_edge(g.out, out2, true);
					f.out = out2;
				} else {
					for (int k = lo; k < hi; ++k) {
						const int in2 = new_state();
						const int out2 = new_state();
						const nfa_frag g = emit_nfa(n.child, ci, dot, depth + 1);
						add_edge(f.out, in2, true);
						add_edge(in2, g.in, true);
						add_edge(in2, out2, true);
						add_edge(g.out, out2, true);
						f.out = out2;
					}
				}
				return f;
			}
			case pk::chr:
			case pk::range:
			case pk::cw:
			case pk::cd:
			case pk::cs:
			case pk::cnw:
			case pk::cnd:
			case pk::cns:
				return char_frag(member_mask(node, ci));
			case pk::any: {
				mask256 m{};
				m.invert();
				if (!dot) {
					mask256 nl{};
					nl.set(0x0A);
					nl.invert();
					for (int i = 0; i < 4; ++i) { m.w[i] &= nl.w[i]; }
				}
				return char_frag(m);
			}
			case pk::str: {
				nfa_frag f{new_state(), -1};
				f.out = f.in;
				const std::string_view s = pool_view(n.a, n.b);
				for (const char c : s) {
					mask256 m{};
					m.set(static_cast<unsigned char>(c));
					if (ci) { m.ci_close(); }
					const int to = new_state();
					add_edge(f.out, to, false, m);
					f.out = to;
				}
				return f;
			}
			case pk::cls: {
				mask256 m{};
				for (int c = n.child; c >= 0; c = nodes[c].sib) {
					m.merge(member_mask(c, false));
				}
				// case closure applies to the MEMBERS, then negation
				// complements the closed set: [^a]i excludes both a
				// and A (closing after inverting would instead ADD the
				// flipped cases to the matches)
				if (ci) { m.ci_close(); }
				if (n.a) { m.invert(); }
				return char_frag(m);
			}
			case pk::ci:
				return emit_nfa(n.child, true, dot, depth + 1);
			case pk::dotall:
				return emit_nfa(n.child, ci, true, depth + 1);
			case pk::rx:
				return emit_nfa(n.child, ci, dot, depth + 1);
			case pk::tref: {
				const int t = n.a;
				if (syms[t].pattern < 0) {
					fail("ctlark: reference to an undefined terminal", name_of(t));
					return nfa_frag{0, 0};
				}
				return emit_nfa(syms[t].pattern, ci, dot, depth + 1);
			}
			case pk::rref:
				fail("ctlark: rule reference inside a terminal");
				return nfa_frag{0, 0};
		}
		fail("ctlark: unexpected node in a terminal pattern");
		return nfa_frag{0, 0};
	}

	constexpr void build_term_nfa(int s) noexcept {
		if (!ok || syms[s].pattern < 0) { return; }
		const nfa_frag f = emit_nfa(syms[s].pattern, false, false, 0);
		syms[s].nfa_start = f.in;
		syms[s].nfa_accept = f.out;
	}

	// --- checks and closures

	constexpr void validate() noexcept {
		for (int i = 0; i < sym_count && ok; ++i) {
			if (syms[i].terminal) {
				if (syms[i].pattern < 0) { fail("ctlark: reference to an undefined terminal", name_of(i)); }
			} else {
				if (!syms[i].defined) { fail("ctlark: reference to an undefined rule", name_of(i)); }
			}
		}
	}

	constexpr void compute_nullable() noexcept {
		bool changed = true;
		while (changed) {
			changed = false;
			for (int p = 0; p < prod_count; ++p) {
				if (nullable[prods[p].lhs]) { continue; }
				bool all = true;
				for (int i = 0; i < prods[p].rhs_len; ++i) {
					const int s = rhs_pool[prods[p].rhs_off + i];
					if (syms[s].terminal || !nullable[s]) {
						all = false;
						break;
					}
				}
				if (all) {
					nullable[prods[p].lhs] = true;
					changed = true;
				}
			}
		}
	}

	constexpr int find_rule(std::string_view name) const noexcept {
		for (int i = 0; i < sym_count; ++i) {
			if (!syms[i].terminal && name_of(i) == name) { return i; }
		}
		return -1;
	}

	// --- the driver

	constexpr void finish() noexcept {
		if (!ok) { return; }
		resolve_ignores();
		const int n_alts = alt_count; // helpers do not add alts
		for (int i = 0; i < n_alts && ok; ++i) {
			rhs_buf b{};
			lower_into(b, alts[i].root, 0);
			add_prod(alts[i].sym, b, alts[i].alias_off, alts[i].alias_len);
		}
		for (int s = 0; s < sym_count && ok; ++s) {
			if (syms[s].terminal) { build_term_nfa(s); }
		}
		validate();
		compute_nullable();
	}

	// should this terminal's tokens be dropped from trees by default?
	constexpr bool filtered(int s) const noexcept {
		return syms[s].keyword || syms[s].anon
		    || (syms[s].name_len > 0 && pool[syms[s].name_off] == '_');
	}
};

// build the tables from a type-level grammar AST
template <typename Ast, size_t N> constexpr auto compile_tables() noexcept {
	grammar_tables<N> g{};
	Ast::collect(g);
	g.finish();
	return g;
}

} // namespace ctlark::detail

#endif
