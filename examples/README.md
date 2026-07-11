# Examples

Self-contained programs, each compilable against `../include` (or the
single header). Build with `make`, build and run with `make run`; they
also build and run as tests through CMake/CTest.

| File | Shows |
|------|-------|
| [`config.cpp`](config.cpp) | an XML configuration baked into the binary: compile-time requirement checks, attribute values as constants, iteration over endpoints |
| [`wellformed.cpp`](wellformed.cpp) | `is_valid` as a bool: grammar rejections and the semantic well-formedness checks (tag matching, attribute uniqueness) |
| [`introspection.cpp`](introspection.cpp) | a generic recursive visitor over any document: kind dispatch, attribute/child iteration, compile-time re-serialization |
