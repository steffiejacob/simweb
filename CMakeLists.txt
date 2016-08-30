# 
# webget/CMakeLists.txt --
#

cmake_minimum_required(VERSION 2.6)
project(webget C) 
include(GNUInstallDirs)

add_executable(webget webget.c)

# compiler flags passed as arguments
add_definitions(-Wall)

find_library(CURL curl)

target_link_libraries(webget ${CURL} pthread)

install(TARGETS webget DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES webget.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT doc)

