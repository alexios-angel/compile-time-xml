CXX ?= c++
CXXFLAGS := -std=c++20 -I../include -O2 -Wall -Wextra -pedantic

SOURCES := $(wildcard *.cpp)
TARGETS := $(SOURCES:%.cpp=%)

all: $(TARGETS)

%: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

run: all
	@for t in $(TARGETS); do printf '== %s ==\n' "$$t"; ./$$t; echo; done

clean:
	rm -f $(TARGETS)

.PHONY: all run clean
