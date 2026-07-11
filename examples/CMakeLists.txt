file(GLOB example-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

foreach(source ${example-sources})
  get_filename_component(example "${source}" NAME_WE)
  add_executable(ctxml-example-${example} "${source}")
  target_link_libraries(ctxml-example-${example} PRIVATE ctxml::ctxml)
  add_test(NAME example-${example} COMMAND ctxml-example-${example})
endforeach()
