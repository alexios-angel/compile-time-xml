.PHONY: default all clean grammar regrammar pch single-header single-header/ctxml.hpp

default: all

CXX_STANDARD := 20

PYTHON := python3

# LL1q parser generator: https://github.com/alexios-angel/Tablewright
# (needs python3 with the lark package)
TABLEWRIGHT := tablewright

# Earley at compile time needs more constexpr budget than the defaults
CXX_IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -qi clang && echo yes)
ifeq ($(CXX_IS_CLANG),yes)
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048
else
CONSTEXPR_FLAGS := -fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024
endif

override CXXFLAGS := $(CXXFLAGS) -std=c++$(CXX_STANDARD) -Iinclude $(CONSTEXPR_FLAGS) -O2 -pedantic -Wall -Wextra -Werror -Wconversion

# precompiled header: parsing the XML grammar text and compiling its
# tables happens once here instead of once per translation unit
ifeq ($(CXX_IS_CLANG),yes)
PCH := ctxml.pch
PCH_USE = -include-pch $(PCH)
else
PCH := include/ctxml.hpp.gch
PCH_USE =
endif

TESTS := $(wildcard tests/*.cpp)
OBJECTS := $(TESTS:%.cpp=%.o)
DEPENDENCY_FILES := $(OBJECTS:%.o=%.d)

all: $(OBJECTS)

$(OBJECTS): %.o: %.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(PCH_USE) -MMD -c $< -o $@

pch: $(PCH)

$(PCH): include/ctxml.hpp
	$(CXX) $(CXXFLAGS) -x c++-header $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(OBJECTS) $(DEPENDENCY_FILES) ctxml.pch include/ctxml.hpp.gch

# the only generated table left is ctlark's own grammar-of-grammars
grammar: include/ctlark/lark.hpp

regrammar:
	@rm -f include/ctlark/lark.hpp
	@$(MAKE) grammar

include/ctlark/lark.hpp: include/ctlark/lark.gram
	@echo "LL1q $<"
	@$(TABLEWRIGHT) --ll --q --input=include/ctlark/lark.gram --output=include/ctlark/ --generator=cpp_ctll_v2 --cfg:fname=lark.hpp --cfg:namespace=ctlark --cfg:guard=CTLARK__LARK__HPP --cfg:grammar_name=lark_grammar

# needs python3 with the quom package
single-header: single-header/ctxml.hpp

single-header/ctxml.hpp:
	$(PYTHON) -m quom include/ctxml.hpp ctxml.hpp.tmp
	echo "/*" > single-header/ctxml.hpp
	cat LICENSE >> single-header/ctxml.hpp
	echo "*/" >> single-header/ctxml.hpp
	cat ctxml.hpp.tmp >> single-header/ctxml.hpp
	rm ctxml.hpp.tmp
