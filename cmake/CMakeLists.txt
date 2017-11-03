cmake_minimum_required(VERSION 3.1)

project(LamportMutualExclusion CXX C)

set (CMAKE_CXX_STANDARD 11)
set (THREADS_PREFER_PTHREAD_FLAG ON)

find_package(MPI REQUIRED)
message(STATUS "MPI CONFIG:")
message(STATUS "	COMPILER : " ${MPI_CXX_COMPILER})
message(STATUS "	COMPILE FLAGS : " ${MPI_CXX_COMPILE_FLAGS})
message(STATUS "	INCLUDES : " ${MPI_CXX_INCLUDE_PATH})
message(STATUS "	LINK FLAGS : " ${MPI_CXX_LINK_FLAGS})
message(STATUS "	LIBRARIES : " ${MPI_CXX_LIBRARIES})
include_directories(${MPI_CXX_INCLUDE_PATH})

find_package(Threads REQUIRED)

add_executable(lamport_mutual_exclusion ../src/main.cpp ../src/lamport_timestamp_provider.cpp ../src/mpi_mutex.cpp)
target_link_libraries(lamport_mutual_exclusion ${MPI_CXX_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(lamport_mutual_exclusion PROPERTIES LINKER_LANGUAGE "CXX")

if (MPI_COMPILE_FLAGS)
	set_target_properties(lamport_mutual_exclusion PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif ()

if (MPI_LINK_FLAGS)
	set_target_properties(lamport_mutual_exclusion PROPERTIES LINK_FLAGS "${MPI_LINK_FLAGS}")
endif ()
