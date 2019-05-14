cmake_minimum_required (VERSION 2.8)
project(TerminalRecord)
set(TERMINAL_RECORD_MAJOR_VERSION 0)
set(TERMINAL_RECORD_MINOR_VERSION 1)
set(CMAKE_BUILD_TYPE Debug)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(YOTTADB REQUIRED)

add_executable(termrec termrec.c extract_string.c parse_args.c)

include_directories("${YOTTADB_INCLUDE_DIRS}")
target_link_libraries(termrec ${YOTTADB_LIBRARIES})

