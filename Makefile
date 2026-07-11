.PHONY: default all clean grammar regrammar single-header single-header/ctxml.hpp

default: all

CXX_STANDARD := 20

PYTHON := python3

# LL1q parser generator: https://github.com/alexios-angel/Tablewright
# (needs python3 with the lark package)
TABLEWRIGHT := tablewright

override CXXFLAGS := $(CXXFLAGS) -std=c++$(CXX_STANDARD) -Iinclude -O3 -pedantic -Wall -Wextra -Werror -Wconversion

TESTS := $(wildcard tests/*.cpp)
OBJECTS := $(TESTS:%.cpp=%.o)
DEPENDENCY_FILES := $(OBJECTS:%.o=%.d)

all: $(OBJECTS)

$(OBJECTS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(OBJECTS) $(DEPENDENCY_FILES)

grammar: include/ctxml/xml.hpp

regrammar:
	@rm -f include/ctxml/xml.hpp
	@$(MAKE) grammar

include/ctxml/xml.hpp: include/ctxml/xml.gram
	@echo "LL1q $<"
	@$(TABLEWRIGHT) --ll --q --input=include/ctxml/xml.gram --output=include/ctxml/ --generator=cpp_ctll_v2 --cfg:fname=xml.hpp --cfg:namespace=ctxml --cfg:guard=CTXML__XML__HPP --cfg:grammar_name=xml

# needs python3 with the quom package
single-header: single-header/ctxml.hpp

single-header/ctxml.hpp:
	$(PYTHON) -m quom include/ctxml.hpp ctxml.hpp.tmp
	echo "/*" > single-header/ctxml.hpp
	cat LICENSE >> single-header/ctxml.hpp
	echo "*/" >> single-header/ctxml.hpp
	cat ctxml.hpp.tmp >> single-header/ctxml.hpp
	rm ctxml.hpp.tmp
