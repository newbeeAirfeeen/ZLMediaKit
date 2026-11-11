# streamproxy NetWorkTool protobuf 3.20.3, upstream libprotobuf-lite source list.
add_library(wlw_protobuf_lite STATIC
    src/google/protobuf/any_lite.cc
    src/google/protobuf/arena.cc
    src/google/protobuf/arenastring.cc
    src/google/protobuf/arenaz_sampler.cc
    src/google/protobuf/extension_set.cc
    src/google/protobuf/generated_enum_util.cc
    src/google/protobuf/generated_message_tctable_lite.cc
    src/google/protobuf/generated_message_util.cc
    src/google/protobuf/implicit_weak_message.cc
    src/google/protobuf/inlined_string_field.cc
    src/google/protobuf/io/coded_stream.cc
    src/google/protobuf/io/io_win32.cc
    src/google/protobuf/io/strtod.cc
    src/google/protobuf/io/zero_copy_stream.cc
    src/google/protobuf/io/zero_copy_stream_impl.cc
    src/google/protobuf/io/zero_copy_stream_impl_lite.cc
    src/google/protobuf/map.cc
    src/google/protobuf/message_lite.cc
    src/google/protobuf/parse_context.cc
    src/google/protobuf/repeated_field.cc
    src/google/protobuf/repeated_ptr_field.cc
    src/google/protobuf/stubs/bytestream.cc
    src/google/protobuf/stubs/common.cc
    src/google/protobuf/stubs/int128.cc
    src/google/protobuf/stubs/status.cc
    src/google/protobuf/stubs/statusor.cc
    src/google/protobuf/stubs/stringpiece.cc
    src/google/protobuf/stubs/stringprintf.cc
    src/google/protobuf/stubs/structurally_valid.cc
    src/google/protobuf/stubs/strutil.cc
    src/google/protobuf/stubs/time.cc
    src/google/protobuf/wire_format_lite.cc
)
set_target_properties(wlw_protobuf_lite PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(wlw_protobuf_lite BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(wlw_protobuf_lite PRIVATE Threads::Threads)
if(ENABLE_CXX_API)
  install(TARGETS wlw_protobuf_lite ARCHIVE DESTINATION ${INSTALL_PATH_LIB})
  install(DIRECTORY src/google DESTINATION ${INSTALL_PATH_INCLUDE}
          FILES_MATCHING PATTERN "*.h" PATTERN "*.inc")
endif()
