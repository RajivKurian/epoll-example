cmake_minimum_required (VERSION 2.6)
project (epoll-example)

#############################
## Compiler flags.
#############################
list( APPEND CMAKE_CXX_FLAGS "-pthread -std=c++0x -march=native -O2 -g -Wall ${CMAKE_CXX_FLAGS} ")


##########
# Src
##########
SET (ring_buffer_SRC ring_buffer.hpp)

SET (net_utils_LIB net_utils)
SET (net_utils_SRC net_utils.cpp)

SET (epoll_SRC epoll.cpp)
SET (epoll_EXECUTABLE epoll)


ADD_LIBRARY(${net_utils_LIB} SHARED ${net_utils_SRC})

ADD_EXECUTABLE(${epoll_EXECUTABLE} ${epoll_SRC})

TARGET_LINK_LIBRARIES(${epoll_EXECUTABLE} ${net_utils_LIB})