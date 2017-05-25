cmake_minimum_required(VERSION 3.6)
project(framework)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS_DEBUG "-ggdb3 -O0")

find_package(MPI REQUIRED)
include_directories(SYSTEM ${MPI_INCLUDE_PATH})

# for absolute includes
include_directories(src/)

set(SOURCE_FILES src/main.cpp src/GraphPartition.h src/representations/SimpleStaticGraph.h src/representations/SimpleStaticGraph.cpp src/Algorithm.h src/algorithms/GraphColouring.cpp src/algorithms/GraphColouring.h src/utils/BufferPool.h src/utils/MPIAsync.cpp src/utils/MPIAsync.h src/algorithms/GraphColouringAsync.cpp src/algorithms/GraphColouringAsync.h src/algorithms/shared.cpp src/algorithms/shared.h)
add_executable(framework ${SOURCE_FILES})
target_link_libraries(framework ${MPI_C_LIBRARIES})