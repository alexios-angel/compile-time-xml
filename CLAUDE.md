# CLAUDE.md — compile-time-xml (ctxml)

Header-only, compile-time (constexpr) well-formed XML parser for a supported
subset of XML 1.0. A document is a *type*: `ctxml::parse<...>()` yields an
`element` whose every accessor is `constexpr`; malformed or ill-formed XML is a
compile error (or `false` from `is_valid`). Namespace `ctxml`. Compile-time
ONLY — no runtime document load. Work on `main`. Prefer `rg` over `grep`.

## Build & test — "compiling the tests IS the test"
Tests under `tests/*.cpp` are `static_assert` suites; each compiles to a `.o`.
```bash
make                                   # C++20 (default), one .o per test
make CXX=clang++                       # clang
make CXX=clang++ CXX_STANDARD=17       # C++17 path (variable-form API)
make clean
cmake -B build && cmake --build build && ctest --test-dir build
```
Flags are `-O2 -pedantic -Wall -Wextra -Werror -Wconversion` — keep every
change warning-clean. A PCH of the umbrella header (`make pch`, done
automatically) compiles the grammar + tables ONCE; TUs start from the baked
result. gcc uses `include/ctxml.hpp.gch`; clang uses `ctxml.pch` (`-include-pch`).

## Layout
- `include/ctxml.hpp` — umbrella (includes the pieces below); public API.
- `include/ctxml/grammar.hpp` — the XML grammar as a **lark grammar string** (data, parsed by ctlark); relies on ctlark's contextual lexing.
- `include/ctxml/bind.hpp` — lowers the lark tree into document types; enforces well-formedness the grammar can't (see below).
- `include/ctxml/types.hpp` — `element` / `text` node types, `kind` enum, accessors.
- `include/ctxml/views.hpp` — `node_view` / `attribute_view` (uniform runtime views for `operator[]`, iteration).
- `include/ctxml/serialize.hpp` — `serialize()` back to minified XML.
- `include/ctlark/`, `include/ctll/` + `ctlark.hpp`, `ctll.hpp` — **vendored** sublibraries (see Vendoring).
- `tests/` (`document.cpp`, `cxx17.cpp`), `examples/` (`config`, `introspection`, `iteration`, `wellformed`), `single-header/ctxml.hpp`, `ctxml.cppm` (module, `import std`).

## Public API (all `template <fixed_string input>`)
- `ctxml::is_valid<input>` — `bool`, never a compile error.
- `ctxml::parse<input>()` — returns the root `element`; invalid XML fails the build with a message naming the query to run.
- `ctxml::error_info<input>()` / `error_message<input>()` — syntax failure location + expected tokens (rendered caret).
- `ctxml::bind_error<input>()` — why a document that PARSES is ill-formed: `bind_reason::{mismatched_tag, duplicate_attribute, bad_reference}` (defined in `bind.hpp`), plus `.where`.
- `ctxml::serialize(...)`, `ctxml::for_each_child`, `ctxml::for_each_attribute`, `attributes(...)`.
- `ctxml::debug::{traced_parse, parse_runtime, dump_tokens, dump_grammar}` — ctlark toolbox with the XML grammar baked in.
- Diagnostics macros: `CTLARK_VERBOSE_ERRORS`, `CTLARK_DEBUG`, `CTLARK_CONSTEXPR_ASSERT`.

## Conventions
- C++17/C++20 split via `CTLL_CNTTP_COMPILER_CHECK`: C++20 takes string-literal
  NTTPs; C++17 takes a `const auto&` to a `constexpr ctll::fixed_string`
  variable with linkage. Test both — `cxx17.cpp` guards the C++17 form.
- Constexpr/Earley parsing needs HUGE budgets (Makefile sets them; CMake
  attaches via `CTXML_CONSTEXPR_LIMITS`, opt out `-DCTXML_CONSTEXPR_LIMITS=OFF`):
  - gcc: `-fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024`
  - clang: `-fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048`
  Hitting the compiler's own step cap is a distinct failure from the library's
  queryable overflow/depth errors.
- CMake toggles: `CTXML_PCH`, `CTXML_BUILD_TESTS`, `CTXML_BUILD_EXAMPLES`, `CTXML_CXX_STANDARD` (default 20), `CTXML_MODULE`.

## GOTCHAS
- **Vendored, do NOT edit `include/ctlark` or `include/ctll` here.**
  `compile-time-lark` is the SOURCE OF TRUTH; these are byte-identical copies.
  Edit the core there, then sync with `compile-time-lark/tools/sync-vendor.sh`,
  verify with `diff -rq`, and regenerate the single-header.
- **single-header** — `make single-header` (needs `quom`); prepends `LICENSE`,
  amalgamates `include/ctxml.hpp` into `single-header/ctxml.hpp`.
- **Grammar tables via Tablewright** — the only generated table left is
  ctlark's own `include/ctlark/lark.hpp` (the grammar-of-grammars). Regenerate
  after editing `include/ctlark/lark.gram` with `make regrammar` (needs the
  `tablewright` tool + `python3` + `lark`). ctxml's own XML grammar is a plain
  data string in `grammar.hpp` — no codegen step.
- **Attribution** — CTLL is Hana Dusíková's (via `notre`, from CTRE); the Lark
  grammar language is the lark-parser project's; ctxml's LL(1)-table lineage
  traces to Tablewright/Desatomat. Preserve `NOTICE` and `LICENSE` (Apache-2.0
  w/ LLVM Exceptions).
