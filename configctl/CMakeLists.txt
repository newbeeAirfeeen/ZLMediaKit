

add_executable(
        configctl
        main.cpp
        ${CMAKE_SOURCE_DIR}/Src/Utils/config_secure.cpp
)

target_include_directories(
        configctl PRIVATE ${CMAKE_SOURCE_DIR}/Src
        PRIVATE ${CMAKE_SOURCE_DIR}/3rdpart/ZLToolKit/src
)

target_link_libraries(
        configctl PUBLIC zltoolkit
)

if(ENABLE_OPENSSL)
    target_compile_definitions(configctl PRIVATE -DENABLE_OPENSSL)
endif()

install(
        TARGETS configctl
        DESTINATION bin
)