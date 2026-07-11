// Well-formedness as a compile-time property: is_valid answers as a
// bool without failing the build, so shipping malformed markup becomes
// impossible - the checks below run in the compiler, not in production.
//
// Build: make wellformed

#include <ctxml.hpp>
#include <iostream>

// what parses
static_assert(ctxml::is_valid<"<a/>">);
static_assert(ctxml::is_valid<"<a x='1'><b>text</b></a>">);
static_assert(ctxml::is_valid<"<r>&lt;&#65;&#x42;&gt;</r>">);
static_assert(ctxml::is_valid<"<r><![CDATA[<raw>]]></r>">);
static_assert(ctxml::is_valid<"<ns:tag-1.2 attr.name='v'/>">);
static_assert(ctxml::is_valid<"<café>ő</café>">); // utf-8 names

// what the grammar rejects
static_assert(!ctxml::is_valid<"<a>">);                    // unclosed
static_assert(!ctxml::is_valid<"<a/><b/>">);               // two roots
static_assert(!ctxml::is_valid<"<a>&nbsp;</a>">);          // undefined entity
static_assert(!ctxml::is_valid<"<a b='<'/>">);             // raw < in attribute
static_assert(!ctxml::is_valid<"<a><!-- x -- y --></a>">); // "--" inside comment
static_assert(!ctxml::is_valid<"<1a/>">);                  // names cannot start with a digit
static_assert(!ctxml::is_valid<"<!DOCTYPE html><a/>">);    // DTDs unsupported, by design

// what the SEMANTIC checks reject: these are grammatical, but not
// well-formed - caught by the actions, still at compile time
static_assert(!ctxml::is_valid<"<a></b>">);                // close tag mismatch
static_assert(!ctxml::is_valid<"<a><b></a></b>">);         // interleaved elements
static_assert(!ctxml::is_valid<"<a x='1' x='2'/>">);       // duplicate attribute

int main() {
	std::cout << "every claim in this file was proven during compilation\n";
}
