cmake_minimum_required(VERSION 3.18)
project(webify LANGUAGES C CXX)

# Allow third_party libs with old cmake_minimum_required versions
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Third-party directory
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# -------------------------------------------------------------------
# Zlib
# -------------------------------------------------------------------
if(EXISTS ${THIRD_PARTY_DIR}/zlib/CMakeLists.txt)
    set(ZLIB_HEADERS ${THIRD_PARTY_DIR}/zlib)
    set(ZLIB_SOURCES
        ${THIRD_PARTY_DIR}/zlib/adler32.c
        ${THIRD_PARTY_DIR}/zlib/compress.c
        ${THIRD_PARTY_DIR}/zlib/crc32.c
        ${THIRD_PARTY_DIR}/zlib/deflate.c
        ${THIRD_PARTY_DIR}/zlib/gzclose.c
        ${THIRD_PARTY_DIR}/zlib/gzlib.c
        ${THIRD_PARTY_DIR}/zlib/gzread.c
        ${THIRD_PARTY_DIR}/zlib/gzwrite.c
        ${THIRD_PARTY_DIR}/zlib/inflate.c
        ${THIRD_PARTY_DIR}/zlib/infback.c
        ${THIRD_PARTY_DIR}/zlib/inftrees.c
        ${THIRD_PARTY_DIR}/zlib/inffast.c
        ${THIRD_PARTY_DIR}/zlib/trees.c
        ${THIRD_PARTY_DIR}/zlib/uncompr.c
        ${THIRD_PARTY_DIR}/zlib/zutil.c
    )
    add_library(zlib_static STATIC ${ZLIB_SOURCES})
    target_include_directories(zlib_static PUBLIC ${ZLIB_HEADERS})
    if(MSVC)
        target_compile_options(zlib_static PRIVATE /w)
    else()
        target_compile_options(zlib_static PRIVATE -w)
    endif()
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARIES zlib_static)
    message(STATUS "Using zlib from third_party")
else()
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        message(STATUS "Using system zlib")
    endif()
endif()

# -------------------------------------------------------------------
# Zopfli
# -------------------------------------------------------------------
if(EXISTS ${THIRD_PARTY_DIR}/zopfli/CMakeLists.txt)
    add_subdirectory(${THIRD_PARTY_DIR}/zopfli third_party/zopfli EXCLUDE_FROM_ALL)
    set(ZOPFLI_FOUND TRUE)
    set(ZOPFLI_LIBRARIES Zopfli::libzopfli)
    message(STATUS "Using zopfli from third_party")
else()
    find_package(Zopfli QUIET)
    if(Zopfli_FOUND)
        set(ZOPFLI_FOUND TRUE)
        set(ZOPFLI_LIBRARIES Zopfli::libzopfli)
        message(STATUS "Using system zopfli")
    endif()
endif()

# -------------------------------------------------------------------
# LibXml2
# -------------------------------------------------------------------
if(EXISTS ${THIRD_PARTY_DIR}/libxml2/CMakeLists.txt)
    set(BUILD_SHARED_LIBS OFF)
    set(LIBXML2_WITH_ICONV OFF)
    set(LIBXML2_WITH_PYTHON OFF)
    set(LIBXML2_WITH_TESTS OFF)
    set(LIBXML2_WITH_PROGRAMS OFF)
    set(LIBXML2_WITH_HTTP OFF)
    set(LIBXML2_WITH_MODULES OFF)
    set(LIBXML2_WITH_LEGACY OFF)
    set(LIBXML2_WITH_THREADS OFF)
    set(LIBXML2_WITH_C14N OFF)
    set(LIBXML2_WITH_CATALOG OFF)
    set(LIBXML2_WITH_DEBUG OFF)
    set(LIBXML2_WITH_HTML OFF)
    set(LIBXML2_WITH_PATTERN OFF)
    set(LIBXML2_WITH_PUSH OFF)
    set(LIBXML2_WITH_READER OFF)
    set(LIBXML2_WITH_RELAXNG OFF)
    set(LIBXML2_WITH_SAX1 OFF)
    set(LIBXML2_WITH_SCHEMAS OFF)
    set(LIBXML2_WITH_SCHEMATRON OFF)
    set(LIBXML2_WITH_VALID OFF)
    set(LIBXML2_WITH_XINCLUDE OFF)
    set(LIBXML2_WITH_XPATH OFF)
    set(LIBXML2_WITH_XPTR OFF)
    set(LIBXML2_WITH_REGEXPS OFF)
    set(LIBXML2_WITH_ISO8859X OFF)
    set(LIBXML2_WITH_WINPATH OFF)
    set(LIBXML2_WITH_THREAD_ALLOC OFF)
    set(LIBXML2_WITH_ICU OFF)
    add_subdirectory(${THIRD_PARTY_DIR}/libxml2 third_party/libxml2 EXCLUDE_FROM_ALL)
    set(LIBXML2_FOUND TRUE)
    set(LIBXML2_LIBRARIES LibXml2::LibXml2)
    message(STATUS "Using libxml2 from third_party")
else()
    find_package(LibXml2 QUIET)
    if(LIBXML2_FOUND)
        message(STATUS "Using system libxml2")
    endif()
endif()

# -------------------------------------------------------------------
# Brotli
# -------------------------------------------------------------------
if(EXISTS ${THIRD_PARTY_DIR}/brotli/CMakeLists.txt)
    set(BUILD_SHARED_LIBS OFF)
    set(BROTLI_BUNDLED_MODE ON)
    add_subdirectory(${THIRD_PARTY_DIR}/brotli third_party/brotli EXCLUDE_FROM_ALL)
    set(BROTLI_FOUND TRUE)
    set(BROTLI_INCLUDE_DIRS ${THIRD_PARTY_DIR}/brotli/c/include)
    set(BROTLI_LIBRARIES brotlienc)
    message(STATUS "Using brotli from third_party")
else()
    find_package(Brotli QUIET)
    if(Brotli_FOUND)
        set(BROTLI_FOUND TRUE)
        message(STATUS "Using system brotli")
    endif()
endif()

# -------------------------------------------------------------------
# Platform-specific link libraries
# -------------------------------------------------------------------
set(PLATFORM_LIBS)
if(WIN32)
    list(APPEND PLATFORM_LIBS advapi32 bcrypt)
endif()
if(UNIX)
    find_library(M_LIB m)
    if(M_LIB)
        list(APPEND PLATFORM_LIBS ${M_LIB})
    endif()
endif()

# -------------------------------------------------------------------
# Main executable
# -------------------------------------------------------------------
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
if(LIBXML2_FOUND)
    include_directories(${LIBXML2_INCLUDE_DIR})
endif()

set(SOURCES
    src/main.cpp
    src/parser.cpp
    src/font.cpp
    src/utils.cpp
    src/eot.cpp
    src/woff.cpp
    src/woff2.cpp
    src/svg.cpp
)

if(LIBXML2_FOUND)
    list(APPEND SOURCES src/svg_libxml2.cpp)
endif()

add_executable(webify ${SOURCES})

target_link_libraries(webify PRIVATE
    ${ZLIB_LIBRARIES}
    ${ZOPFLI_LIBRARIES}
    ${BROTLI_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    ${PLATFORM_LIBS}
)

target_compile_definitions(webify PRIVATE
    $<$<BOOL:${ZLIB_FOUND}>:HAVE_ZLIB>
    $<$<BOOL:${ZOPFLI_FOUND}>:HAVE_ZOPFLI>
    $<$<BOOL:${BROTLI_FOUND}>:HAVE_BROTLI>
    $<$<BOOL:${LIBXML2_FOUND}>:HAVE_LIBXML2>
    $<$<BOOL:${LIBXML2_FOUND}>:LIBXML_STATIC>
)

if(ZOPFLI_FOUND)
    target_include_directories(webify PRIVATE ${THIRD_PARTY_DIR}/zopfli/src/zopfli)
endif()
if(BROTLI_FOUND)
    target_include_directories(webify PRIVATE ${BROTLI_INCLUDE_DIRS})
endif()

install(TARGETS webify RUNTIME DESTINATION bin)
