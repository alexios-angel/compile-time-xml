// Walking a document generically: every node carries its kind, children
// and attributes are iterable, so a recursive if-constexpr visitor can
// pretty-print (or transform) any document - the traversal is resolved
// at compile time, only the printing runs.
//
// Build: make introspection

#include <ctxml.hpp>
#include <iostream>
#include <string>

template <typename Node> void print(Node node, int indent = 0) {
	const std::string pad(static_cast<size_t>(indent) * 2, ' ');
	if constexpr (Node::type == ctxml::kind::text) {
		std::cout << pad << '"' << Node::view() << '"' << "\n";
	} else {
		std::cout << pad << '<' << Node::name();
		ctxml::for_each_attribute(node, [](auto name, auto value) {
			std::cout << ' ' << name.view() << "=\"" << value.view() << '"';
		});
		if constexpr (Node::child_count() == 0) {
			std::cout << "/>\n";
		} else {
			std::cout << ">\n";
			ctxml::for_each_child(node, [&](auto child) { print(child, indent + 1); });
			std::cout << pad << "</" << Node::name() << ">\n";
		}
	}
}

constexpr auto doc = ctxml::parse<R"(
<feed version="1.1">
	<entry id="1" starred="yes">
		<title>Compile-time everything</title>
		<summary>types &amp; templates</summary>
	</entry>
	<entry id="2">
		<title>Parsers as tables</title>
	</entry>
	<generator/>
</feed>)">();

int main() {
	print(doc);

	// the same document, re-serialized to minified form at compile time
	constexpr auto minified = ctxml::serialize(doc);
	std::cout << "\nminified (" << minified.size() << " bytes):\n" << minified << "\n";
}
