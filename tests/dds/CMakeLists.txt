cmake_minimum_required(VERSION 3.20)
project(hello_world_dds LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fastcdr REQUIRED)
find_package(fastdds REQUIRED)

# 生成的类型支持代码
set(GENERATED_SOURCES
    HelloWorldPubSubTypes.cxx
    HelloWorldTypeObjectSupport.cxx
)

add_executable(publisher HelloWorldPublisher.cpp ${GENERATED_SOURCES})
target_include_directories(publisher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(publisher fastdds fastcdr)

add_executable(subscriber HelloWorldSubscriber.cpp ${GENERATED_SOURCES})
target_include_directories(subscriber PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(subscriber fastdds fastcdr)
