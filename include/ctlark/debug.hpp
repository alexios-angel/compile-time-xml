#ifndef CTLARK__DEBUG__HPP
#define CTLARK__DEBUG__HPP

// Tools for debugging constexpr parses. Include order matters: this
// header is included at the END of ctlark.hpp because it builds on the
// grammar_def/parse_def machinery defined there.
//
//   debug::traced_parse<grammar, input>()  - rerun the parse with a
//       recording trace log. It is a plain constexpr function, so the
//       SAME call also runs at runtime: call it from main() to step
//       through the parser under a debugger (or stream the trace live
//       with CTLARK_DEBUG_STDERR) on exactly the input that fails at
//       compile time.
//   debug::parse_runtime<grammar>(text)    - parse a RUNTIME string
//       against the compile-time tables; iterate on inputs without
//       recompiling.
//   debug::dump_tokens<grammar, input>()   - the lexed token stream as
//       a static string (up to the first error, if any).
//   debug::dump_grammar<grammar>()         - the lowered terminals and
//       productions, i.e. what the Earley parser actually runs.
//   CTLARK_CONSTEXPR_ASSERT (assert.hpp)   - internal invariants that
//       stop constant evaluation with a readable message; enabled by
//       defining CTLARK_DEBUG.

#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>
#ifdef CTLARK_DEBUG_STDERR
#include <cstdio>
#endif
#endif

namespace ctlark::debug {

namespace detail {

using namespace ctlark::detail;

// runtime-vs-constexpr detection, usable from C++17
constexpr bool constant_evaluated() noexcept {
#if defined(__cpp_lib_is_constant_evaluated)
	return std::is_constant_evaluated();
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_is_constant_evaluated();
#elif defined(_MSC_VER) && _MSC_VER >= 1925
	return __builtin_is_constant_evaluated();
#else
	return true; // unknown: assume constexpr, never attempt runtime I/O
#endif
}

constexpr error_kind kind_from_perr(perr e) noexcept {
	switch (e) {
		case perr::lex: return error_kind::lex;
		case perr::overflow: return error_kind::overflow;
		case perr::depth: return error_kind::depth;
		case perr::parse: return error_kind::parse;
		case perr::none: return error_kind::none;
	}
	return error_kind::parse;
}

// error_info_t built from a finished parse_result (shared by the
// traced and runtime paths; unlike error_info<>() this also works on
// results computed at runtime)
template <typename GT, typename PR>
constexpr error_info_t info_from_result(const GT & g, const PR & pr, std::string_view in) noexcept {
	error_info_t e{};
	if (pr.ok) { return e; }
	e.kind = kind_from_perr(pr.err);
	e.position = static_cast<size_t>(pr.err_pos);
	const source_position at = locate(in, e.position);
	e.line = at.line;
	e.column = at.column;
	e.expected_count = pr.expected_count;
	e.expected_total = pr.expected_total;
	for (int i = 0; i < pr.expected_count; ++i) { e.expected[i] = term_display(g, pr.expected[i]); }
	return e;
}

} // namespace detail

// --- the trace log: a flat text of one line per parser event

CTLL_EXPORT template <size_t Cap = 4096> struct trace_log {
	static constexpr bool enabled = true;
	static constexpr long no_value = -1;

	char buf[Cap]{};
	size_t len = 0;
	int events = 0;
	bool truncated = false;

	constexpr void put(std::string_view s) noexcept {
		for (const char c : s) {
			if (len + 1 < Cap) {
				buf[len++] = c;
			} else {
				truncated = true;
			}
		}
	}
	constexpr void put_num(long v) noexcept {
		if (v < 0) {
			put("-");
			v = -v;
		}
		char d[20]{};
		size_t n = 0;
		do {
			d[n++] = static_cast<char>('0' + v % 10);
			v /= 10;
		} while (v > 0);
		while (n > 0) { put(std::string_view{&d[--n], 1}); }
	}
	constexpr void event(std::string_view stage, std::string_view what = {},
	                     long a = no_value, long b = no_value) noexcept {
		++events;
		const size_t start = len;
		put(stage);
		if (!what.empty()) {
			put(" ");
			put(what);
		}
		if (a != no_value) {
			put(" ");
			put_num(a);
		}
		if (b != no_value) {
			put(" ");
			put_num(b);
		}
		put("\n");
#ifdef CTLARK_DEBUG_STDERR
		if (!detail::constant_evaluated()) { std::fwrite(buf + start, 1, len - start, stderr); }
#else
		(void)start;
#endif
	}
	constexpr std::string_view view() const noexcept {
		return std::string_view{buf, len};
	}
};

// --- traced_parse: the parse, narrated

CTLL_EXPORT template <size_t Cap> struct traced_result {
	bool ok = false;
	error_info_t error{};
	trace_log<Cap> log{};
};

CTLL_EXPORT template <CTLARK_STRING_INPUT grammar, CTLARK_STRING_INPUT input,
                      CTLARK_STRING_INPUT start = default_start_rule, size_t Cap = 4096>
constexpr traced_result<Cap> traced_parse() noexcept {
	using pd = ctlark::detail::parse_def<grammar, input, start>;
	traced_result<Cap> r{};
	if constexpr (!pd::def::text_ok) {
		r.error.kind = error_kind::bad_grammar_text;
		r.log.event("grammar: the grammar text is not valid Lark");
	} else if constexpr (!pd::def::tables.ok) {
		r.error.kind = error_kind::bad_grammar;
		r.log.event("grammar: not usable:", ctlark::grammar_error<grammar>());
	} else if constexpr (pd::start_sym < 0) {
		r.error.kind = error_kind::no_start_rule;
		r.log.event("grammar: the start rule is not defined");
	} else {
		const auto pr = ctlark::detail::run_parse_traced<pd::def::tables, pd::in, pd::start_sym>(&r.log);
		r.ok = pr.ok;
		if (!pr.ok) {
			// rebuild the input characters locally so this path also
			// works when the whole call runs at runtime
			char buf[pd::in.size() + 1]{};
			for (size_t i = 0; i < pd::in.size(); ++i) { buf[i] = static_cast<char>(pd::in[i]); }
			r.error = detail::info_from_result(pd::def::tables, pr, std::string_view{buf, pd::in.size()});
			// the string_views in error.expected point into the static
			// grammar tables, never into buf, so returning them is fine
		}
	}
	return r;
}

// --- parse_runtime: runtime input, compile-time grammar
//
// Recognition only (lexing + Earley); the tree-shaping stage that
// parse<>() adds cannot fail on a recognized input except by depth.
// MaxTokens bounds the token stream; inputs longer than that report
// overflow. The chart lives on the heap - this is not constexpr, on
// purpose.

CTLL_EXPORT struct runtime_token {
	std::string_view name;  // terminal name in the grammar tables
	std::string_view value; // the matched span of the input
	size_t offset = 0;
};

CTLL_EXPORT struct runtime_result {
	bool ok = false;
	error_info_t error{};
	std::vector<runtime_token> tokens{};
};

CTLL_EXPORT template <CTLARK_STRING_INPUT grammar, size_t MaxTokens = 1024>
runtime_result parse_runtime(std::string_view in, std::string_view start_rule = "start") {
	using gd = ctlark::detail::grammar_def<grammar>;
	runtime_result r{};
	if constexpr (!gd::text_ok) {
		r.error.kind = error_kind::bad_grammar_text;
		return r;
	} else if constexpr (!gd::tables.ok) {
		r.error.kind = error_kind::bad_grammar;
		return r;
	} else {
		static constexpr auto & g = gd::tables;
		using GT = std::remove_cv_t<std::remove_reference_t<decltype(g)>>;
		const int start_sym = g.find_rule(start_rule);
		if (start_sym < 0) {
			r.error.kind = error_kind::no_start_rule;
			return r;
		}
		if (in.size() > MaxTokens) {
			r.error.kind = error_kind::overflow;
			return r;
		}
		constexpr int item_cap =
			(ctlark::detail::dotted_positions(g) + 16) * (static_cast<int>(MaxTokens) + 2) * 2;
		constexpr int set_cap = static_cast<int>(MaxTokens) + 3;
		const auto ch = std::make_unique<ctlark::detail::chart<GT, item_cap, set_cap>>();
		const auto pipe = std::make_unique<ctlark::detail::pipeline_result<GT, MaxTokens>>(
			ctlark::detail::run_pipeline<GT, MaxTokens>(g, start_sym, in, *ch));
		r.ok = pipe->ok;
		if (!pipe->ok) { r.error = detail::info_from_result(g, *pipe, in); }
		const int ntoks = pipe->ok ? pipe->count : 0;
		r.tokens.reserve(static_cast<size_t>(ntoks));
		for (int i = 0; i < ntoks; ++i) {
			const auto & tk = pipe->toks[i];
			r.tokens.push_back(runtime_token{g.name_of(tk.sym),
			                                 in.substr(static_cast<size_t>(tk.off), static_cast<size_t>(tk.len)),
			                                 static_cast<size_t>(tk.off)});
		}
		return r;
	}
}

// --- dump_tokens: the lexed token stream as a static string

namespace detail {

// lex the input without extracting a tree (its own instantiation, so
// asking for a token dump does not disturb the cached parse)
template <typename PD> struct lex_def {
	static constexpr auto & g = PD::def::tables;
	using GT = std::remove_cv_t<std::remove_reference_t<decltype(g)>>;
	static constexpr size_t M = PD::in.size();

	static constexpr auto make() noexcept {
		char buf[M + 1]{};
		for (size_t i = 0; i < M; ++i) { buf[i] = static_cast<char>(PD::in[i]); }
		constexpr int item_cap = (dotted_positions(g) + 16) * (static_cast<int>(M) + 2) * 2;
		constexpr int set_cap = static_cast<int>(M) + 3;
		chart<GT, item_cap, set_cap> ch{};
		return run_pipeline<GT, M>(g, PD::start_sym, std::string_view{buf, M}, ch);
	}
	static constexpr auto pipe = make();
};

template <typename PD> struct tokens_storage {
	template <typename Sink> static constexpr void render(Sink & s) noexcept {
		const auto & pipe = lex_def<PD>::pipe;
		const std::string_view in = input_text<PD>::view;
		const int n = pipe.ok ? pipe.count : lexed_count();
		for (int i = 0; i < n; ++i) {
			const auto & tk = pipe.toks[i];
			s.put(lex_def<PD>::g.name_of(tk.sym));
			s.put(" '");
			s.put_escaped(in.substr(static_cast<size_t>(tk.off), static_cast<size_t>(tk.len)));
			s.put("' @");
			put_uint(s, static_cast<size_t>(tk.off));
			s.put("..");
			put_uint(s, static_cast<size_t>(tk.off + tk.len));
			s.put("\n");
		}
		if (!pipe.ok) {
			s.put("! ");
			s.put(to_string(kind_from_perr(pipe.err)));
			s.put(" at offset ");
			put_uint(s, static_cast<size_t>(pipe.err_pos));
			s.put("\n");
		}
	}
	// on failure pipe.count was left mid-stream; it is still the number
	// of tokens lexed so far
	static constexpr int lexed_count() noexcept {
		return lex_def<PD>::pipe.count;
	}

	static constexpr size_t measure() noexcept {
		count_sink s{};
		render(s);
		return s.at;
	}
	static constexpr size_t length = measure();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t compute() noexcept {
		out_t o{};
		repr_sink s{o.content};
		render(s);
		return o;
	}
	static constexpr out_t content = compute();
	static constexpr std::string_view view{content.content, length};
};

} // namespace detail

CTLL_EXPORT template <CTLARK_STRING_INPUT grammar, CTLARK_STRING_INPUT input,
                      CTLARK_STRING_INPUT start = default_start_rule>
constexpr std::string_view dump_tokens() noexcept {
	using pd = ctlark::detail::parse_def<grammar, input, start>;
	if constexpr (!pd::def::text_ok || !pd::def::tables.ok) {
		return ctlark::grammar_error<grammar>();
	} else if constexpr (pd::start_sym < 0) {
		return "ctlark: the start rule is not defined in the grammar";
	} else {
		return detail::tokens_storage<pd>::view;
	}
}

// --- dump_grammar: the lowered terminals and productions

namespace detail {

template <typename GD> struct grammar_storage {
	template <typename Sink> static constexpr void render(Sink & s) noexcept {
		const auto & g = GD::tables;
		for (int t = 0; t < g.sym_count; ++t) {
			if (!g.syms[t].terminal) { continue; }
			s.put("terminal ");
			s.put(g.name_of(t));
			if (term_is_literal(g, t)) {
				s.put(" '");
				s.put_escaped(g.pool_view(g.syms[t].lit_off, g.syms[t].lit_len));
				s.put("'");
			}
			if (g.syms[t].prio != 0) {
				s.put(" .");
				const int prio = g.syms[t].prio;
				if (prio < 0) { s.put("-"); }
				put_uint(s, static_cast<size_t>(prio < 0 ? -prio : prio));
			}
			if (g.syms[t].ignored) { s.put(" %ignore"); }
			s.put("\n");
		}
		for (int p = 0; p < g.prod_count; ++p) {
			const auto & pr = g.prods[p];
			s.put(g.name_of(pr.lhs));
			s.put(":");
			for (int i = 0; i < pr.rhs_len; ++i) {
				s.put(" ");
				s.put(g.name_of(g.rhs_pool[pr.rhs_off + i]));
			}
			if (pr.alias_off >= 0) {
				s.put(" -> ");
				s.put(g.pool_view(pr.alias_off, pr.alias_len));
			}
			s.put("\n");
		}
	}

	static constexpr size_t measure() noexcept {
		count_sink s{};
		render(s);
		return s.at;
	}
	static constexpr size_t length = measure();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t compute() noexcept {
		out_t o{};
		repr_sink s{o.content};
		render(s);
		return o;
	}
	static constexpr out_t content = compute();
	static constexpr std::string_view view{content.content, length};
};

} // namespace detail

CTLL_EXPORT template <CTLARK_STRING_INPUT grammar> constexpr std::string_view dump_grammar() noexcept {
	using gd = ctlark::detail::grammar_def<grammar>;
	if constexpr (!gd::text_ok || !gd::tables.ok) {
		return ctlark::grammar_error<grammar>();
	} else {
		return detail::grammar_storage<gd>::view;
	}
}

} // namespace ctlark::debug

#endif
