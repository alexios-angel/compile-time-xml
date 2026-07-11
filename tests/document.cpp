#include <ctxml.hpp>

void empty_symbol_17() { }

// the string-literal API needs C++20 class-type template parameters;
// tests/cxx17.cpp covers the C++17 variable form
#if CTLL_CNTTP_COMPILER_CHECK

#include <string_view>
using namespace std::literals;

// --- basics
static_assert(ctxml::is_valid<"<a/>">);
static_assert(ctxml::is_valid<"<a></a>">);
static_assert(ctxml::is_valid<"<a><b/><c></c></a>">);
static_assert(ctxml::is_valid<"  <a/>  ">);

// --- THE feature: well-formedness is a compile-time property
static_assert(!ctxml::is_valid<"<a></b>">);              // mismatched tags
static_assert(!ctxml::is_valid<"<a><b></a></b>">);       // interleaved
static_assert(!ctxml::is_valid<"<a x='1' x='2'/>">);     // duplicate attribute
static_assert(!ctxml::is_valid<"<a>">);                  // unclosed
static_assert(!ctxml::is_valid<"<a/><b/>">);             // two roots
static_assert(!ctxml::is_valid<"text">);                 // no root element
static_assert(!ctxml::is_valid<"">);
static_assert(!ctxml::is_valid<"<a>&nbsp;</a>">);        // undefined entity
static_assert(!ctxml::is_valid<"<!DOCTYPE html><a/>">);  // DTDs unsupported
static_assert(!ctxml::is_valid<"<a><!-- x -- y --></a>">); // -- inside comment
static_assert(!ctxml::is_valid<"<a b='<'/>">);           // raw < in attribute

// --- a real document
constexpr auto doc = ctxml::parse<R"(
<?xml version="1.0" encoding="UTF-8"?>
<!-- server configuration -->
<server host="example.com" port='8080' secure="true">
	<path>/api</path>
	<timeout unit="s">2.5</timeout>
	<alias/>
	<alias/>
	<motd>a &lt;b&gt; &amp; &#233; &#x1F600; c</motd>
	<raw><![CDATA[literal <markup> & stuff]]></raw>
	<mixed>one<sep/>two</mixed>
</server>
<!-- trailing comment -->
)">();

static_assert(doc.name() == "server");
static_assert(doc.attribute_count() == 3);
static_assert(doc.attribute<"host">() == "example.com"sv);
static_assert(doc.attribute<"port">() == "8080"sv);   // both quote flavours
static_assert(doc.has_attribute<"secure">());
static_assert(!doc.has_attribute<"missing">());
static_assert(doc.attribute_name<0>() == "host"sv);   // positional
static_assert(doc.attribute_value<1>() == "8080"sv);

// children: whitespace-only text between elements is dropped
static_assert(doc.child_count() == 7);
static_assert(doc.contains<"path">());
static_assert(!doc.contains<"nope">());
static_assert(doc.count<"alias">() == 2);
static_assert(doc.get<"path">().text() == "/api"sv);
static_assert(doc.get<"timeout">().attribute<"unit">() == "s"sv);
static_assert(doc.get<"alias">().empty());

// entities and character references decode at parse time
static_assert(doc.get<"motd">().text() == "a <b> & \xc3\xa9 \xf0\x9f\x98\x80 c"sv);

// CDATA is literal
static_assert(doc.get<"raw">().text() == "literal <markup> & stuff"sv);

// mixed content: text() concatenates the direct text children
static_assert(doc.get<"mixed">().text() == "onetwo"sv);
static_assert(doc.get<"mixed">().child_count() == 3);
static_assert(doc.get<"mixed">().child<1>().name() == "sep"sv);
static_assert(doc.get<"mixed">().child<0>().type == ctxml::kind::text);

// --- more entity / CDATA edges
static_assert(ctxml::parse<"<a>&apos;&quot;</a>">().text() == "'\""sv);
static_assert(ctxml::parse<"<a><![CDATA[x]]y]]></a>">().text() == "x]]y"sv);
static_assert(ctxml::parse<"<a><![CDATA[]]]]><![CDATA[>]]></a>">().text() == "]]>"sv);
static_assert(ctxml::parse<"<a>x<!-- comment -->y</a>">().text() == "xy"sv);
static_assert(ctxml::parse<"<a><?php echo ?>t</a>">().text() == "t"sv);

// attribute values: entities decode, both quotes, empty values
static_assert(ctxml::parse<R"(<a t="&lt;&#65;&gt;"/>)">().attribute<"t">() == "<A>"sv);
static_assert(ctxml::parse<"<a t=''/>">().attribute<"t">().empty());
static_assert(ctxml::parse<R"(<a t='say "hi"'/>)">().attribute<"t">() == "say \"hi\""sv);

// utf-8 names work (bytes above 0x7F are name characters)
static_assert(ctxml::parse<"<café tükör='ő'/>">().name() == "café"sv);

// names may contain : . - digits (but not start with them)
static_assert(ctxml::is_valid<"<ns:tag-1.2/>">);
static_assert(!ctxml::is_valid<"<1a/>">);

// whitespace tolerance in tags
static_assert(ctxml::is_valid<"<a  x = '1'  ></a  >">);

// --- serialization
static_assert(ctxml::serialize(ctxml::parse<"<a  x = '1' >hi<b/></a>">()) == R"(<a x="1">hi<b/></a>)"sv);
static_assert(ctxml::serialize(ctxml::parse<"<m>a &lt;b&gt;</m>">()) == "<m>a &lt;b&gt;</m>"sv);
static_assert(ctxml::serialize(ctxml::parse<R"(<a q="a&quot;b"/>)">()) == R"(<a q="a&quot;b"/>)"sv);
// round-trip: parse(serialize(x)) is the same type as x
constexpr auto rt = ctxml::parse<R"(<a x="1"><b>t</b></a>)">();
static_assert(std::is_same_v<decltype(ctxml::parse<ctll::fixed_string{R"(<a x="1"><b>t</b></a>)"}>()), std::remove_const_t<decltype(rt)>>);

// --- iteration
static_assert([] {
	size_t elements = 0, texts = 0;
	ctxml::for_each_child(doc, [&](auto child) {
		if constexpr (decltype(child)::type == ctxml::kind::element) {
			++elements;
		} else {
			++texts;
		}
	});
	return elements * 10 + texts;
}() == 70);
static_assert([] {
	size_t total = 0;
	ctxml::for_each_attribute(doc, [&](auto name, auto value) {
		total += name.size() + value.size();
	});
	return total;
}() == 4 + 11 + 4 + 4 + 6 + 4);

void empty_symbol() { }

#endif

// --- operator[] and iterators (see include/ctxml/views.hpp)

#if CTLL_CNTTP_COMPILER_CHECK

namespace bracket_tests {


constexpr auto cfg = ctxml::parse<
    R"(<service name="demo"><endpoint host="a" tls="true"/><endpoint host="b"/><motd>hi</motd></service>)">();

// [] is get (first child element with the tag) or child (by position),
// with the tag or index carried in the argument's type
static_assert(cfg["endpoint"].attribute("host") == "a"sv);
static_assert(cfg["motd"].text() == "hi"sv);
static_assert(cfg[2].name() == "motd"sv);
static_assert(cfg[0].has_attribute("tls"));
static_assert(cfg.attribute("name") == "demo"sv);
static_assert(cfg.has_attribute("name") && !cfg.has_attribute("motd"));
static_assert(cfg.contains("endpoint"));
static_assert(!cfg.contains("missing"));
static_assert(cfg["missing"][0]["still-missing"].name().empty());

// name TYPES work as [] arguments in any standard
static_assert([] {
	size_t found = 0;
	ctxml::for_each_child(cfg, [&](auto child) {
		if constexpr (decltype(child)::type == ctxml::kind::element) {
			if (cfg[child.name()].name() == child.name()) {
				++found;
			}
		}
	});
	return found;
}() == 3);

// begin/end yield uniform views from static storage: range-for works,
// in constexpr evaluation included
static_assert([] {
	size_t elements = 0;
	size_t texts = 0;
	for (const auto & n : cfg) {
		if (n.type == ctxml::kind::element) {
			++elements;
		} else {
			++texts;
		}
	}
	return elements * 10 + texts;
}() == 30);

static_assert([] {
	for (const auto & n : cfg) {
		if (n.name() == "motd") {
			return n.text() == "hi"; // elements view their direct text
		}
	}
	return false;
}());

// mixed content: text children view their content, with no name
// (gcc 10 wants this loop in a named function rather than a constexpr lambda)
constexpr auto mixed = ctxml::parse<"<a>x<b/>y</a>">();
constexpr size_t mixed_text_chars() {
	size_t text_chars = 0;
	for (const auto & n : mixed) {
		if (n.type == ctxml::kind::text) {
			text_chars += n.text().size();
		}
	}
	return text_chars;
}
static_assert(mixed_text_chars() == 2);

// attributes as an iterable array of name/value views
// (gcc 10 wants this loop in a named function rather than a constexpr lambda)
constexpr size_t endpoint_attribute_chars() {
	size_t chars = 0;
	for (const auto & a : ctxml::attributes(cfg["endpoint"])) {
		chars += a.name.size() + a.value.size();
	}
	return chars;
}
static_assert(endpoint_attribute_chars() == (4 + 1) + (3 + 4));
static_assert(ctxml::attributes(cfg).size() == 1);

// childless elements iterate zero times
static_assert(ctxml::begin(ctxml::parse<"<a/>">()) == ctxml::end(ctxml::parse<"<a/>">()));

} // namespace bracket_tests

// --- diagnostics: error_info, error_message, bind_error, debug tools

// valid documents report nothing
static_assert(ctxml::error_info<"<a><b/></a>">().ok());
static_assert(ctxml::error_message<"<a><b/></a>">() == ""sv);
static_assert(ctxml::bind_error<"<a><b/></a>">().ok());

// an unclosed element: kind, offset, line, column, expected tokens
constexpr auto unclosed = ctxml::error_info<"<a><b></b>">();
static_assert(unclosed.kind == ctlark::error_kind::parse);
static_assert(unclosed.position == 10 && unclosed.line == 1 && unclosed.column == 11);
static_assert(ctxml::error_message<"<a><b></b>">() ==
              "ctlark: syntax error at line 1, column 11: unexpected end of input\n"
              "  <a><b></b>\n"
              "            ^\n"
              "expected: _COMMENT, _PI, OPEN, TEXT, CDATA, CLOSE"sv);

// well-formedness failures name the rule and the offending token
constexpr auto mismatched = ctxml::bind_error<"<a><b></c></a>">();
static_assert(mismatched.reason == ctxml::bind_reason::mismatched_tag);
static_assert(mismatched.where == "</c>"sv);
constexpr auto dup_attr = ctxml::bind_error<R"(<a x="1" x="2"/>)">();
static_assert(dup_attr.reason == ctxml::bind_reason::duplicate_attribute);
static_assert(dup_attr.where == "x"sv);
constexpr auto bad_ref = ctxml::bind_error<"<a>&#x0;</a>">();
static_assert(bad_ref.reason == ctxml::bind_reason::bad_reference);
static_assert(bad_ref.where == "&#x0;"sv);

// the ctlark debugging toolbox with the XML grammar baked in
static_assert(ctxml::debug::dump_tokens<"<a x=\"1\">hi</a>">() ==
              "OPEN '<a' @0..2\n"
              "NAME 'x' @3..4\n"
              "EQUAL '=' @4..5\n"
              "DQVAL '\"1\"' @5..8\n"
              "MORETHAN '>' @8..9\n"
              "TEXT 'hi' @9..11\n"
              "CLOSE '</a>' @11..15\n"sv);
constexpr auto traced = ctxml::debug::traced_parse<"<a></b>">();
static_assert(traced.ok); // the SYNTAX parses; the binder rejects it
static_assert(traced.log.events > 0);
static_assert(ctxml::debug::dump_grammar().find("terminal OPEN") != std::string_view::npos);

#endif
