#ifndef CTXML__GRAMMAR__HPP
#define CTXML__GRAMMAR__HPP

#include "../ctlark.hpp"

// The grammar layer: the ctxml XML subset written in lark's grammar
// language and parsed by ctlark. This grammar only tokenizes at all
// because ctlark's lexer is CONTEXTUAL, like lark's: TEXT is a
// candidate only where character data is expected, so it cannot
// swallow attribute syntax inside a tag, and whitespace-only runs are
// %ignore'd between attributes but arrive as TEXT tokens (dropped
// later if blank) inside content.
//
// Terminals carry the spec load: tag-open tokens glue '<' to the name
// (ASCII name characters plus every byte above 0x7F, so UTF-8 names
// work), close tags are one token, entity references are validated
// lexically (an undefined entity or a raw & is a lex error), comments
// enforce the no "--" rule by construction, CDATA ends at the first
// ]]>, and processing instructions (the XML declaration included) are
// _-prefixed so they vanish from trees. DOCTYPE has no terminal at
// all: rejected, by design.
//
// What the grammar cannot say - a close tag must match its open tag,
// attribute names must be unique, character references must be valid
// code points - the binder (bind.hpp) checks, and is_valid includes.

namespace ctxml::detail {

inline constexpr ctll::fixed_string xml_grammar = R"x(
?start: (_COMMENT | _PI)* element (_COMMENT | _PI)*

element: OPEN attr* "/>"
       | OPEN attr* ">" (element | TEXT | CDATA | _COMMENT | _PI)* CLOSE

attr: NAME "=" (DQVAL | SQVAL)

OPEN: /<[A-Za-z_:\x80-\xff][A-Za-z0-9_:.\x80-\xff\-]*/
CLOSE: /<\/[A-Za-z_:\x80-\xff][A-Za-z0-9_:.\x80-\xff\-]*[ \x09\x0a\x0d]*>/
NAME: /[A-Za-z_:\x80-\xff][A-Za-z0-9_:.\x80-\xff\-]*/
DQVAL: /"([^<"&]|&(lt|gt|amp|apos|quot|#[0-9]+|#x[0-9a-fA-F]+);)*"/
SQVAL: /'([^<'&]|&(lt|gt|amp|apos|quot|#[0-9]+|#x[0-9a-fA-F]+);)*'/
TEXT: /([^<&]|&(lt|gt|amp|apos|quot|#[0-9]+|#x[0-9a-fA-F]+);)+/
CDATA: /<!\[CDATA\[([^\]]|\]+[^\]>])*\]+\]>/
_COMMENT: /<!--([^-]|-[^-])*-->/
_PI: /<\?([^?]|\?+[^?>])*\?+>/

%ignore /[ \x09\x0a\x0d]+/
)x";

inline constexpr ctll::fixed_string xml_start = "start";

static_assert(ctlark::grammar_valid<xml_grammar>,
              "ctxml: internal error - the XML grammar failed to compile");

} // namespace ctxml::detail

#endif
