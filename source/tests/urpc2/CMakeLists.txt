cmake_minimum_required(VERSION 3.23)

project(urpc2
    VERSION 0.1.0
    DESCRIPTION "A tiny RPC-over-DDS wrapper"
    LANGUAGES CXX
)

find_package(fastcdr 2 REQUIRED)
find_package(fastdds 3.4 REQUIRED)
find_package(Threads REQUIRED)

add_library(urpc2_processor_gen STATIC)
target_sources(urpc2_processor_gen
    PRIVATE
        src/types/processorClient.cxx
        src/types/processorServer.cxx
        src/types/processorPubSubTypes.cxx
        src/types/processorTypeObjectSupport.cxx
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            src/types/processor.hpp
            src/types/processor_details.hpp
            src/types/processorClient.hpp
            src/types/processorServer.hpp
            src/types/processorServerImpl.hpp
            src/types/processorPubSubTypes.hpp
            src/types/processorCdrAux.hpp
            src/types/processorTypeObjectSupport.hpp
)
target_compile_features(urpc2_processor_gen PUBLIC cxx_std_17)
target_link_libraries(urpc2_processor_gen PUBLIC fastdds fastcdr Threads::Threads)

add_library(urpc2 STATIC)
target_sources(urpc2
    PRIVATE
        src/urpc2.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/urpc2.hpp
)
target_compile_features(urpc2 PUBLIC cxx_std_17)
target_link_libraries(urpc2 PUBLIC urpc2_processor_gen)

add_executable(urpc2_simple examples/simple.cpp)
target_link_libraries(urpc2_simple PRIVATE urpc2)
