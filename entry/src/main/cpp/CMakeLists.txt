cmake_minimum_required(VERSION 3.16)
project(qiscreen LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(UX "${ROOT}/third_party/UxPlay/lib")
set(PL "${ROOT}/third_party/libplist")
set(CRYPTO_BUILD "${ROOT}/.native-deps/openssl")
if(NOT EXISTS "${CRYPTO_BUILD}/libcrypto.a")
    message(FATAL_ERROR "Run python3 scripts/build_native_deps.py before building the HAP")
endif()

file(GLOB PLIST_C "${PL}/src/*.c")
add_library(plist STATIC ${PLIST_C} "${PL}/libcnary/node.c" "${PL}/libcnary/node_list.c")
target_include_directories(plist PUBLIC "${PL}/include" PRIVATE "${PL}/src" "${PL}/libcnary/include")
target_compile_definitions(plist PRIVATE PACKAGE_VERSION="2.8.0" HAVE_STRNDUP=1 HAVE_MEMMEM=1 HAVE_STRPTIME=1
    HAVE_GMTIME_R=1 HAVE_LOCALTIME_R=1 HAVE_TM_TM_GMTOFF=1 HAVE_TM_TM_ZONE=1)

add_library(crypto STATIC IMPORTED)
set_target_properties(crypto PROPERTIES IMPORTED_LOCATION "${CRYPTO_BUILD}/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CRYPTO_BUILD}/include;${ROOT}/third_party/openssl/include")
file(GLOB AIRPLAY_C "${UX}/*.c" "${UX}/playfair/*.c" "${UX}/llhttp/*.c" "${UX}/mdnsd/*.c")
add_library(airplay STATIC ${AIRPLAY_C})
target_include_directories(airplay PUBLIC "${UX}" PRIVATE "${UX}/playfair" "${UX}/llhttp" "${UX}/mdnsd")
target_compile_definitions(airplay PRIVATE PLIST_210 PLIST_230 OPENSSL_API_COMPAT=0x10101000L
    STANDALONE _REENTRANT _FILE_OFFSET_BITS=64)
target_compile_options(airplay PRIVATE -Wno-deprecated-declarations)
target_link_libraries(airplay PUBLIC crypto plist)

add_library(qiscreen SHARED napi_init.cpp receiver.cpp media.cpp)
target_compile_options(qiscreen PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
target_link_libraries(qiscreen PRIVATE airplay ace_napi.z ace_ndk.z hilog_ndk.z
    native_media_vdec native_media_acodec native_media_core native_media_codecbase native_window ohaudio)
