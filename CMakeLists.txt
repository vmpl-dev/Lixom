cmake_minimum_required(VERSION 3.14.0)
project (lixom C CXX ASM)

if (NOT(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64"))
    message(FATAL_ERROR "Error: The architecture ${CMAKE_SYSTEM_PROCESSOR} is not supported. lixom requires x86_64.")
endif ()

set_property(GLOBAL PROPERTY C_STANDARD 11)
set_property(GLOBAL PROPERTY CXX_STANDARD 20)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_link_options("-znoexecstack")
include_directories("libxom")
include_directories("modxom")
add_compile_options("-O3")

add_subdirectory(modxom)

add_library(xom SHARED "libxom/libxom.c")
target_link_libraries(xom PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(xom PUBLIC "-fPIC;-mrdrnd")

add_library(xom-static STATIC "libxom/libxom.c")
target_link_libraries(xom-static PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(xom-static PUBLIC "-mrdrnd")

add_library(xom_provider SHARED
    openssl-provider/src/xom_provider.c
    openssl-provider/src/xom_aes_128_ctr.c
    openssl-provider/src/xom_aes_128_gcm.c
    openssl-provider/src/xom_aes_common.c
    openssl-provider/src/aes_aesni.s
    openssl-provider/src/aes_vaes.s
    openssl-provider/src/ghash.s
    openssl-provider/src/hmac_sha256.s
    openssl-provider/src/xom_hmac_sha256.c
    openssl-provider/src/xom_subpage_pool.cpp
)
target_include_directories(xom_provider PUBLIC openssl-provider/include)
target_compile_options(xom_provider PUBLIC "-fPIE;-mssse3;-mpclmul;-mrdrnd")
target_link_libraries(xom_provider PUBLIC OpenSSL::Crypto xom)

add_executable(demo_libxom "demos/demo_libxom.c")
target_link_libraries(demo_libxom PUBLIC xom)
add_executable(demo_https "demos/demo_https.c")
target_link_libraries(demo_https PUBLIC OpenSSL::SSL curl)

install(TARGETS xom DESTINATION /usr/lib)
install(FILES libxom/xom.h DESTINATION include)
install(FILES modxom/modxom.h DESTINATION include)
install(FILES ./xom DESTINATION /usr/bin PERMISSIONS WORLD_READ WORLD_EXECUTE)
