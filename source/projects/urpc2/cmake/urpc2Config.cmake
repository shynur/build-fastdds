include(CMakeFindDependencyMacro)
find_dependency(Threads)
find_dependency(fastcdr 2)
find_dependency(fastdds 3.4)

include(${CMAKE_CURRENT_LIST_DIR}/urpc2Targets.cmake)
