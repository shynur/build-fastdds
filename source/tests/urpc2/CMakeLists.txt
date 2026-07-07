cmake_minimum_required(VERSION 3.23)

project(urpc2
    VERSION 0.1
    LANGUAGES CXX
)

find_package(fastcdr 2 REQUIRED)
find_package(fastdds 3.4 REQUIRED)
find_package(Threads REQUIRED)

add_library(urpc2_processor_gen STATIC)
target_sources(urpc2_processor_gen
    PRIVATE
        # TODO: 以 IDL 文件的 basename 为前缀, cxx 为后缀, 自动获取
        src/types/processorClient.cxx
        src/types/processorServer.cxx
        src/types/processorPubSubTypes.cxx
        src/types/processorTypeObjectSupport.cxx
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            # TODO: 以 IDL 文件的 basename 为前缀, hpp 为后缀, 自动获取
            src/types/processor.hpp
            src/types/processor_details.hpp
            src/types/processorClient.hpp
            src/types/processorServer.hpp
            src/types/processorServerImpl.hpp
            src/types/processorPubSubTypes.hpp
            src/types/processorCdrAux.hpp
            src/types/processorTypeObjectSupport.hpp
)
target_compile_features(urpc2_processor_gen
  PUBLIC
    cxx_std_17
)
target_link_libraries(urpc2_processor_gen
    PUBLIC
        fastdds
        fastcdr
        Threads::Threads
)

add_library(urpc2)
target_sources(urpc2
    PRIVATE
        src/urpc2.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/urpc2.hpp
)
target_link_libraries(urpc2
    PUBLIC
        urpc2_processor_gen
)



# Apps for 示例/测试

add_executable(urpc2_simple examples/simple.cpp)
target_link_libraries(urpc2_simple urpc2)

add_executable(urpc2_mesh_node examples/mesh_node.cpp)
target_link_libraries(urpc2_mesh_node urpc2)
