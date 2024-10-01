cmake_minimum_required(VERSION 3.1)
project (auto_invoice C CXX)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)
set (VERSION_PATCH 0)

include_directories(src)
include_directories(${GNUCASH_PREFIX}/include)
include_directories(${GNUCASH_BUILD}/libgnucash/engine)
include_directories(${GNUCASH_BUILD}/libgnucash/core-utils)
include_directories(${GNUCASH_BUILD}/build/common)
include_directories(${GNUCASH_PREFIX}/include/guile/3.0)
include_directories(${GNUCASH_PREFIX}/guile/3.0)

link_directories(${GNUCASH_PREFIX}/lib)
link_directories(${GNUCASH_PREFIX}/lib/gnucash)

FIND_PACKAGE(PkgConfig REQUIRED)
PKG_CHECK_MODULES(GLIB REQUIRED glib-2.0)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})
include_directories("/usr/include/guile/3.0")

file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")

set(CMAKE_CXX_STANDARD 17)

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} -lgnc-engine -lgnc-app-utils -lgnc-core-utils -lgnc-report -lgnc-module -lguile-3.0 ${GLIB_LIBRARIES} -lstdc++fs)

# set(CMAKE_C_FLAGS "-O3 -Werror -Wall -Wextra -flto -march=native")
set(CMAKE_CXX_FLAGS "-Wl,-rpath -Wl,${GNUCASH_PREFIX}/lib:${GNUCASH_PREFIX}/lib/gnucash")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath -Wl,${GNUCASH_PREFIX}/lib:${GNUCASH_PREFIX}/lib/gnucash")
