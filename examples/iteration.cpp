// Brackets and iteration: operator[] accepts ordinary tags and indexes,
// returning uniform views; begin/end give an element's children those
// same views (kind + name + text) out of
// static storage, so range-for and <algorithm> work; attributes(...)
// does the same for its attributes.
//
// Build: make iteration

#include <ctxml.hpp>
#include <algorithm>
#include <iostream>

constexpr auto feed = ctxml::parse<R"(<feed version="1.0" updated="2026-07-11">
	<title>release notes</title>
	<entry id="1"><title>Brackets</title></entry>
	<entry id="2"><title>Iterators</title></entry>
</feed>)">();

// --- operator[]: get (first child with the tag) and child (by position)

static_assert(feed["title"].text() == "release notes");
static_assert(feed["entry"].attribute("id") == "1");
static_assert(feed[2].attribute("id") == "2");
static_assert(feed["entry"]["title"].text() == "Brackets");

// --- iteration: children and attributes as uniform views

static_assert(std::count_if(begin(feed), end(feed),
    [](const ctxml::node_view & n) { return n.name() == "entry"; }) == 2);

static_assert(ctxml::attributes(feed).size() == 2);

// range-for in constant evaluation: a named constexpr function (gcc 10
// mishandles such loops inside a constexpr lambda)
constexpr size_t attribute_chars() noexcept {
	size_t total = 0;
	for (const auto & a : ctxml::attributes(feed)) {
		total += a.name.size() + a.value.size();
	}
	return total;
}
static_assert(attribute_chars() == (7 + 3) + (7 + 10));

int main() {
	// walk any element's children: views are plain kinds and string_views
	for (const auto & n : feed) {
		if (n.type == ctxml::kind::element) {
			std::cout << "<" << n.name() << "> " << n.text() << "\n";
		}
	}

	for (const auto & a : ctxml::attributes(feed)) {
		std::cout << "@" << a.name << " = " << a.value << "\n";
	}
}
