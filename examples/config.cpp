// The classic use: an XML configuration baked into the binary at
// compile time. A typo in the XML, a mismatched tag or a missing
// attribute is a build failure, and every lookup below compiles down
// to a constant.
//
// Build: make config

#include <ctxml.hpp>
#include <iostream>

constexpr auto config = ctxml::parse<R"(<?xml version="1.0" encoding="UTF-8"?>
<service name="demo" workers="8">
	<!-- upstream endpoints, tried in order -->
	<endpoint host="a.example.com" port="443" tls="true"/>
	<endpoint host="b.example.com" port="8080" tls="false"/>
	<greeting>hello &amp; welcome</greeting>
</service>)">();

// requirements checked at build time
static_assert(config.name() == "service");
static_assert(config.count<"endpoint">() >= 1);
static_assert(config.get<"endpoint">().has_attribute<"host">());

// values usable as constants
constexpr int workers = [] {
	constexpr auto text = config.attribute<"workers">();
	int value = 0;
	for (const char c : text.view()) {
		value = value * 10 + (c - '0');
	}
	return value;
}();
int worker_slots[workers];

int main() {
	std::cout << "service:  " << config.attribute<"name">().view() << "\n";
	std::cout << "workers:  " << workers << " (slots: " << sizeof(worker_slots) / sizeof(int) << ")\n";
	std::cout << "greeting: " << config.get<"greeting">().text() << "\n";

	std::cout << "endpoints:\n";
	ctxml::for_each_child(config, [](auto child) {
		if constexpr (decltype(child)::type == ctxml::kind::element) {
			if constexpr (decltype(child)::name() == std::string_view{"endpoint"}) {
				std::cout << "  " << child.template attribute<"host">().view()
				          << ":" << child.template attribute<"port">().view()
				          << (child.template attribute<"tls">() == std::string_view{"true"} ? " (tls)" : "")
				          << "\n";
			}
		}
	});
}
