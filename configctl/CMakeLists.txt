
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND AND ENABLE_OPENSSL)
    message(STATUS "找到openssl库:\"${OPENSSL_INCLUDE_DIR}\",ENABLE_OPENSSL宏已打开")
    include_directories(${OPENSSL_INCLUDE_DIR})
    add_definitions(-DENABLE_OPENSSL)
endif()
add_executable(
        configctl
        main.cpp
        ${CMAKE_SOURCE_DIR}/Src/Utils/config_secure.cpp
)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(configctl PRIVATE -fPIC -fPIE)
    target_link_options(configctl PRIVATE -pie)
endif ()
target_include_directories(
        configctl PRIVATE ${CMAKE_SOURCE_DIR}/Src
        PRIVATE ${CMAKE_SOURCE_DIR}/3rdpart/ZLToolKit/src
)

target_link_libraries(
        configctl PUBLIC zltoolkit
        PUBLIC ${OPENSSL_LIBRARIES} PUBLIC pthread
)

if(ENABLE_OPENSSL)
    target_compile_definitions(configctl PRIVATE -DENABLE_OPENSSL)
endif()

install(
        TARGETS configctl
        DESTINATION bin
)