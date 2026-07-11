#ifndef CTLARK__DIAG__HPP
#define CTLARK__DIAG__HPP

#include "earley.hpp"
#include "tree.hpp"
#include "../ctll/utilities.hpp"
#ifndef CTLARK_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// Queryable diagnostics for failed constexpr parses. is_valid<> stays
// a plain bool; when it is false, error_info() says WHAT failed and
// WHERE (kind, byte offset, line, column, the expected terminals) and
// error_message() renders the whole story as one static string:
//
//   ctlark: lexical error at line 1, column 7: no expected terminal matches
//     [1, 2,]
//           ^
//   expected: SIGNED_NUMBER, ']', ESCAPED_STRING
//
// Everything is computed at compile time; the message lives in static
// storage via the same size-pass/fill-pass idiom as tree repr().

namespace ctlark {

// what stage of a parse failed
CTLL_EXPORT enum class error_kind : unsigned char {
	none,             // the parse succeeded
	bad_grammar_text, // the grammar text is not valid Lark
	bad_grammar,      // the grammar text parsed but does not lower to usable tables
	no_start_rule,    // the requested start rule is not defined in the grammar
	lex,              // no expected terminal matches the input at the position
	parse,            // the token stream does not derive from the start rule
	overflow,         // an internal pool was exhausted (input too large)
	depth             // the derivation recursion limit was hit
};

CTLL_EXPORT constexpr std::string_view to_string(error_kind k) noexcept {
	switch (k) {
		case error_kind::none: return "none";
		case error_kind::bad_grammar_text: return "bad grammar text";
		case error_kind::bad_grammar: return "bad grammar";
		case error_kind::no_start_rule: return "no start rule";
		case error_kind::lex: return "lexical error";
		case error_kind::parse: return "syntax error";
		case error_kind::overflow: return "capacity overflow";
		case error_kind::depth: return "depth limit";
	}
	return "unknown";
}

// a byte offset resolved to 1-based line and column
CTLL_EXPORT struct source_position {
	size_t offset = 0;
	size_t line = 1;
	size_t column = 1;
};

CTLL_EXPORT constexpr source_position locate(std::string_view text, size_t offset) noexcept {
	source_position p{};
	if (offset > text.size()) { offset = text.size(); }
	p.offset = offset;
	for (size_t i = 0; i < offset; ++i) {
		if (text[i] == '\n') {
			++p.line;
			p.column = 1;
		} else {
			++p.column;
		}
	}
	return p;
}

// everything a failed parse knows, as one value. For bad_grammar_text
// the position refers to the GRAMMAR text; for input failures (lex,
// parse, overflow, depth) it refers to the input. expected[] holds the
// terminals some Earley item was waiting for at the failure point -
// named terminals by name, anonymous literals by their spelling.
CTLL_EXPORT struct error_info_t {
	error_kind kind = error_kind::none;
	size_t position = 0;
	size_t line = 1;
	size_t column = 1;
	std::string_view expected[static_cast<size_t>(detail::expected_cap)]{};
	int expected_count = 0;
	int expected_total = 0; // > expected_count when the list was capped

	constexpr bool ok() const noexcept {
		return kind == error_kind::none;
	}
};

namespace detail {

// display spelling of a terminal: named terminals by name, anonymous
// keyword literals by their content
template <typename GT> constexpr bool term_is_literal(const GT & g, int sym) noexcept {
	return g.syms[sym].keyword && g.syms[sym].lit_off >= 0;
}
template <typename GT> constexpr std::string_view term_display(const GT & g, int sym) noexcept {
	if (term_is_literal(g, sym)) { return g.pool_view(g.syms[sym].lit_off, g.syms[sym].lit_len); }
	return g.name_of(sym);
}

// a sink that only measures (the size pass of the render)
struct count_sink {
	size_t at = 0;
	constexpr void put(std::string_view s) noexcept {
		at += s.size();
	}
	constexpr void put_escaped(std::string_view s) noexcept {
		at += repr_escaped_size(s);
	}
};

template <typename Sink> constexpr void put_uint(Sink & s, size_t v) noexcept {
	char buf[20]{};
	size_t n = 0;
	do {
		buf[n++] = static_cast<char>('0' + v % 10);
		v /= 10;
	} while (v > 0);
	for (size_t i = 0; i < n / 2; ++i) {
		const char t = buf[i];
		buf[i] = buf[n - 1 - i];
		buf[n - 1 - i] = t;
	}
	s.put(std::string_view{buf, n});
}

// the line around pos, windowed so the caret is always visible
inline constexpr size_t snippet_width = 72;
inline constexpr size_t snippet_caret_max = 60;

template <typename Sink>
constexpr void render_snippet(Sink & s, std::string_view text, size_t pos) noexcept {
	if (pos > text.size()) { pos = text.size(); }
	size_t ls = pos;
	while (ls > 0 && text[ls - 1] != '\n') { --ls; }
	size_t le = pos;
	while (le < text.size() && text[le] != '\n') { ++le; }
	size_t ws = ls;
	if (pos - ws > snippet_caret_max) { ws = pos - snippet_caret_max; }
	size_t we = le;
	if (we - ws > snippet_width) { we = ws + snippet_width; }
	s.put("\n  ");
	for (size_t i = ws; i < we; ++i) {
		const char c = text[i];
		s.put((c == '\t' || c == '\r') ? std::string_view{" "} : text.substr(i, 1));
	}
	s.put("\n  ");
	for (size_t i = ws; i < pos; ++i) { s.put(" "); }
	s.put("^");
}

// render the whole diagnostic; runs twice (a size pass with
// count_sink, then a fill pass with repr_sink into static storage)
template <typename GT, typename Sink>
constexpr void render_error(Sink & s, const GT & g, error_kind kind, std::string_view text, size_t pos,
                            const int * expected, int expected_count, int expected_total,
                            std::string_view start_rule) noexcept {
	if (kind == error_kind::none) { return; }
	if (kind == error_kind::bad_grammar) {
		s.put(g.error_view());
		return;
	}
	if (kind == error_kind::no_start_rule) {
		s.put("ctlark: the start rule '");
		s.put(start_rule);
		s.put("' is not defined in the grammar");
		return;
	}

	const source_position at = locate(text, pos);
	s.put("ctlark: ");
	switch (kind) {
		case error_kind::bad_grammar_text: s.put("the grammar text is not valid Lark"); break;
		case error_kind::lex: s.put("lexical error"); break;
		case error_kind::parse: s.put("syntax error"); break;
		case error_kind::overflow: s.put("capacity overflow"); break;
		case error_kind::depth: s.put("derivation depth limit hit"); break;
		default: break;
	}
	s.put(" at line ");
	put_uint(s, at.line);
	s.put(", column ");
	put_uint(s, at.column);
	switch (kind) {
		case error_kind::bad_grammar_text:
			break;
		case error_kind::lex:
			s.put(": no expected terminal matches");
			break;
		case error_kind::parse:
			s.put(pos >= text.size() ? ": unexpected end of input" : ": the input does not match the grammar here");
			break;
		case error_kind::overflow:
			s.put(": an internal pool was exhausted (the input is too large for the compiled limits)");
			break;
		case error_kind::depth:
			s.put(": the input nests too deeply");
			break;
		default:
			break;
	}
	render_snippet(s, text, pos);
	if (expected_count > 0) {
		s.put("\nexpected: ");
		for (int i = 0; i < expected_count; ++i) {
			if (i > 0) { s.put(", "); }
			const int sym = expected[i];
			if (term_is_literal(g, sym)) {
				s.put("'");
				s.put_escaped(term_display(g, sym));
				s.put("'");
			} else {
				s.put(term_display(g, sym));
			}
		}
		if (expected_total > expected_count) {
			s.put(", and ");
			put_uint(s, static_cast<size_t>(expected_total - expected_count));
			s.put(" more");
		}
	}
}

// the characters of a def's fixed_string members, as string_views in
// static storage (fixed_strings hold char32_t units carrying bytes)
template <typename PD> struct input_text {
	static constexpr size_t length = PD::in.size();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t make() noexcept {
		out_t o{};
		for (size_t i = 0; i < length; ++i) { o.content[i] = static_cast<char>(PD::in[i]); }
		return o;
	}
	static constexpr out_t content = make();
	static constexpr std::string_view view{content.content, length};
};

template <typename GD> struct grammar_text {
	static constexpr size_t length = GD::text.size();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t make() noexcept {
		out_t o{};
		for (size_t i = 0; i < length; ++i) { o.content[i] = static_cast<char>(GD::text[i]); }
		return o;
	}
	static constexpr out_t content = make();
	static constexpr std::string_view view{content.content, length};
};

template <typename PD> struct start_text {
	static constexpr size_t length = PD::start_name.size();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t make() noexcept {
		out_t o{};
		for (size_t i = 0; i < length; ++i) { o.content[i] = static_cast<char>(PD::start_name[i]); }
		return o;
	}
	static constexpr out_t content = make();
	static constexpr std::string_view view{content.content, length};
};

// which error_kind a parse_def failed with (none when it succeeded)
template <typename PD> constexpr error_kind classify() noexcept {
	if constexpr (!PD::def::text_ok) {
		return error_kind::bad_grammar_text;
	} else if constexpr (!PD::def::tables.ok) {
		return error_kind::bad_grammar;
	} else if constexpr (PD::start_sym < 0) {
		return error_kind::no_start_rule;
	} else if constexpr (PD::result.ok) {
		return error_kind::none;
	} else {
		switch (PD::result.err) {
			case perr::lex: return error_kind::lex;
			case perr::overflow: return error_kind::overflow;
			case perr::depth: return error_kind::depth;
			default: return error_kind::parse;
		}
	}
}

template <typename PD> constexpr error_info_t error_info_of() noexcept {
	error_info_t e{};
	e.kind = classify<PD>();
	if constexpr (!PD::def::text_ok) {
		e.position = PD::def::text_error_pos;
		const source_position at = locate(grammar_text<typename PD::def>::view, e.position);
		e.line = at.line;
		e.column = at.column;
	} else if constexpr (PD::def::tables.ok && PD::start_sym >= 0) {
		if constexpr (!PD::result.ok) {
			e.position = static_cast<size_t>(PD::result.err_pos);
			const source_position at = locate(input_text<PD>::view, e.position);
			e.line = at.line;
			e.column = at.column;
			e.expected_count = PD::result.expected_count;
			e.expected_total = PD::result.expected_total;
			for (int i = 0; i < PD::result.expected_count; ++i) {
				e.expected[i] = term_display(PD::def::tables, PD::result.expected[i]);
			}
		}
	}
	return e;
}

// the rendered message in static storage (only instantiated on demand,
// and only for failed parses)
template <typename PD> struct message_storage {
	static constexpr error_kind kind = classify<PD>();

	static constexpr std::string_view text() noexcept {
		if constexpr (kind == error_kind::bad_grammar_text) {
			return grammar_text<typename PD::def>::view;
		} else {
			return input_text<PD>::view;
		}
	}
	static constexpr size_t pos() noexcept {
		if constexpr (kind == error_kind::bad_grammar_text) {
			return PD::def::text_error_pos;
		} else {
			return static_cast<size_t>(PD::result.err_pos);
		}
	}

	template <typename Sink> static constexpr void render(Sink & s) noexcept {
		render_error(s, PD::def::tables, kind, text(), pos(), PD::result.expected, PD::result.expected_count,
		             PD::result.expected_total, start_text<PD>::view);
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

} // namespace ctlark

#endif
