#include <ctxml.hpp>
#include <string_view>

// C++17: inputs and keys are fixed_string variables with linkage
static constexpr auto doc_text = ctll::fixed_string{R"(<cfg port="8080"><host>x</host></cfg>)"};
static constexpr auto bad_text = ctll::fixed_string{"<a></b>"};
static constexpr ctll::fixed_string port_key = "port";
static constexpr ctll::fixed_string host_tag = "host";

static_assert(ctxml::is_valid<doc_text>);
static_assert(!ctxml::is_valid<bad_text>);

constexpr auto doc = ctxml::parse<doc_text>();
static_assert(doc.template attribute<port_key>() == std::string_view{"8080"});
static_assert(doc.template get<host_tag>().text() == std::string_view{"x"});

void empty_symbol() { }

// operator[] needs no C++20: ordinary strings and integer indexes work as-is

static_assert(doc[0].name() == std::string_view{"host"});
static_assert(doc["host"].text() == std::string_view{"x"});

// iteration: uniform views, range-for, constexpr
static_assert([] {
	size_t n = 0;
	for (const auto & node : doc) {
		n += node.name().size() + node.text().size();
	}
	return n;
}() == 4 + 1);
static_assert(ctxml::attributes(doc).size() == 1);
