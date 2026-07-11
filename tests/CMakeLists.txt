# the suite is static_asserts: compiling a test IS running it
add_custom_target(ctxml-test ALL)

file(GLOB test-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# tests with a main() run; the rest are static_assert suites where
# compiling is the test
set(runtime-tests "")

foreach(source ${test-sources})
  get_filename_component(test "${source}" NAME_WE)
  if(test IN_LIST runtime-tests)
    add_executable(ctxml-test-${test} "${source}")
    target_link_libraries(ctxml-test-${test} PRIVATE ctxml::ctxml)
    if(CTXML_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
      target_precompile_headers(ctxml-test-${test} PRIVATE
        "${PROJECT_SOURCE_DIR}/include/ctxml.hpp")
    endif()
    add_dependencies(ctxml-test ctxml-test-${test})
    add_test(NAME run-${test} COMMAND ctxml-test-${test})
  else()
    add_library(ctxml-test-${test} OBJECT EXCLUDE_FROM_ALL "${source}")
    target_link_libraries(ctxml-test-${test} PRIVATE ctxml::ctxml)
    if(CTXML_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
      target_precompile_headers(ctxml-test-${test} PRIVATE
        "${PROJECT_SOURCE_DIR}/include/ctxml.hpp")
    endif()
    add_dependencies(ctxml-test ctxml-test-${test})
    add_test(NAME compile-${test}
      COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ctxml-test-${test})
  endif()
endforeach()
