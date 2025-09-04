

add_executable(
        configctl
        main.cpp
        ${CMAKE_SOURCE_DIR}/Src/Utils/config_secure.cpp
)

target_include_directories(
        configctl PRIVATE ${CMAKE_SOURCE_DIR}/Src
)

target_link_libraries(
        configctl PRIVATE media_kit
)

if(ENABLE_OPENSSL)
    target_compile_definitions(configctl PRIVATE -DENABLE_OPENSSL)
endif()

install(
        TARGETS configctl
        DESTINATION bin
)