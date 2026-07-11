> **Attribution:** this library is built on the CTLL compile-time LL(1)
> parser from [CTRE](https://github.com/hanickadot/compile-time-regular-expressions)
> by Hana Dusíková, via the [notre](https://github.com/alexios-angel/notre)
> fork, and follows the architecture of its siblings
> [compile-time-json](https://github.com/alexios-angel/compile-time-json) and
> [compile-time-json5](https://github.com/alexios-angel/compile-time-json5).
> Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# ctxml — compile-time XML

XML parsed while your code compiles. The document is a *type*: malformed
— or **ill-formed** — XML is a compile error, lookups are resolved at
compile time, and every accessor is `constexpr` — usable in
`static_assert`, as template arguments, or at runtime with zero parsing
cost.

```c++
#include <ctxml.hpp>

constexpr auto doc = ctxml::parse<R"(<?xml version="1.0"?>
<server host="example.com" port="8080">
    <!-- routes -->
    <path>/api</path>
    <motd>a &lt;b&gt; &#x1F600;</motd>
    <raw><![CDATA[literal <markup> & stuff]]></raw>
</server>)">();

static_assert(doc.name() == "server");
static_assert(doc.attribute<"port">() == "8080");
static_assert(doc.get<"path">().text() == "/api");
static_assert(doc.get<"raw">().text() == "literal <markup> & stuff");

// well-formedness is a compile-time property:
static_assert(!ctxml::is_valid<"<a></b>">);           // mismatched tags
static_assert(!ctxml::is_valid<"<a x='1' x='2'/>">);  // duplicate attribute
static_assert(!ctxml::is_valid<"<a><!-- x -- y --></a>">); // "--" in comment
```

## What is supported

A well-defined subset of XML 1.0, aimed at data documents:

* one root element; nested elements with attributes (`"` or `'`
  quoted) and self-closing tags
* mixed text content; `text()` concatenates an element's direct text
* the five predefined entities (`&lt; &gt; &amp; &apos; &quot;`) and
  decimal/hexadecimal character references, decoded to UTF-8 at parse
  time
* `<!-- comments -->` (the spec's no-`--` rule enforced),
  `<![CDATA[ ... ]]>` sections (taken literally), and processing
  instructions / the XML declaration (skipped)
* names take the ASCII name characters (`A-Za-z0-9_:.-`) plus every
  byte above `0x7F`, so UTF-8 tag and attribute names work
* whitespace-only text between elements is dropped from the document

**Well-formedness checks beyond the grammar**, both compile errors: a
close tag must match its open tag, and attribute names must be unique
within an element.

Not supported, by design (also compile errors): `<!DOCTYPE ...>` and
DTDs, custom entities, namespace *semantics* (colons in names are just
name characters), and encodings other than UTF-8/ASCII.

## API

```c++
// validity as a bool (never a compile error):
template <ctll::fixed_string input> constexpr bool ctxml::is_valid;

// the parsed root element; invalid XML fails the build:
template <ctll::fixed_string input> constexpr auto ctxml::parse();
```

`parse` returns an `element`; its children are `element`s and `text`
nodes:

| Type | Accessors |
|------|-----------|
| `element<name, attrs, children...>` | `name()`, `attribute<"key">()`, `has_attribute<"key">()`, `attribute_count()`, positional `attribute_name<I>()` / `attribute_value<I>()`, `get<"tag">()` (first matching child element), `contains<"tag">()`, `count<"tag">()`, `child<I>()`, `child_count()`, `empty()`, `text()` |
| `text<chars...>` | `view()`, `c_str()` (null-terminated), `size()`, `empty()`, `==` with `std::string_view` |

Every type carries `static constexpr ctxml::kind type` for
introspection (`kind::element`, `kind::text`), and two free functions
iterate at compile time:

```c++
ctxml::for_each_child(doc, [](auto child) { /* each has its own type */ });
ctxml::for_each_attribute(doc, [](auto name, auto value) { ... });

// render any element back to minified XML, in static storage:
static_assert(ctxml::serialize(ctxml::parse<"<a  x = '1' >hi<b/></a>">())
    == R"(<a x="1">hi<b/></a>)");
```

`serialize` re-escapes text (`& < >`) and attribute values (`& < "`),
emits childless elements self-closed, and the result is
null-terminated.

## C++17

With a pre-C++20 compiler, inputs and keys are `constexpr
ctll::fixed_string` variables with linkage instead of string literals:

```c++
static constexpr auto text = ctll::fixed_string{R"(<cfg port="8080"/>)"};
static constexpr ctll::fixed_string port_key = "port";

constexpr auto doc = ctxml::parse<text>();
static_assert(doc.template attribute<port_key>() == std::string_view{"8080"});
```

## How it works

The same architecture as CTRE: an XML grammar
([`xml.gram`](include/ctxml/xml.gram)) is compiled by
[Tablewright](https://github.com/alexios-angel/Tablewright) into an
LL(1) parse table of `rule()` overloads
([`xml.hpp`](include/ctxml/xml.hpp)), which CTLL — the compile-time LL
parser from CTRE — walks character by character. Semantic actions
([`actions.hpp`](include/ctxml/actions.hpp)) build the document on a
type stack: names and text accumulate as they are read, attributes pair
up as they complete, and closing a tag collects the children back to
its open marker — where the tag-match and attribute-uniqueness checks
turn well-formedness violations into compile errors.

Regenerate the table after editing the grammar with `make regrammar`.

## Building and integrating

Header-only. Pick whichever fits your project:

**CMake, as a subdirectory or via FetchContent:**

```cmake
add_subdirectory(compile-time-xml)   # or FetchContent_MakeAvailable(ctxml)
target_link_libraries(your-target PRIVATE ctxml::ctxml)
```

**CMake, installed** (`cmake -B build && cmake --install build`):

```cmake
find_package(ctxml 0.1 REQUIRED)
target_link_libraries(your-target PRIVATE ctxml::ctxml)
```

The install also ships a `pkg-config` file (`ctxml.pc`). Tests and
examples build only when ctxml is the top-level project
(`CTXML_BUILD_TESTS`, `CTXML_BUILD_EXAMPLES`); `CTXML_CXX_STANDARD`
selects the advertised standard (default 20). CPack can produce
TGZ/ZIP archives (plus DEB/RPM where the tooling exists), and
`-DCTXML_MODULE=ON` builds `ctxml.cppm` as a named C++ module
(experimental; needs CMake 3.30+, a modules-capable toolchain and
`import std`).

**No build system:** add `include/` to your include path, or copy the
amalgamated [`single-header/ctxml.hpp`](single-header/ctxml.hpp)
(regenerate with `make single-header`, which needs the
[quom](https://pypi.org/project/quom/) tool).

Requires C++17 (C++20 for the string-literal API). Runnable demos live
in [`examples/`](examples/).

Run the tests (compilation is the test — the suite is `static_assert`s):

```bash
make CXX=clang++                       # C++20
make CXX=clang++ CXX_STANDARD=17
# or through CMake/CTest:
cmake -B build && cmake --build build && ctest --test-dir build
```

## License

Apache License 2.0 with LLVM Exceptions (see [LICENSE](LICENSE)).
The CTLL parser is Hana Dusíková's work, via notre; see
[NOTICE](NOTICE).
