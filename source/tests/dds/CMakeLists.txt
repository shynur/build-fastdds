cmake_minimum_required(VERSION 3.20)
project(hello_world_dds LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fastcdr REQUIRED)
find_package(fastdds REQUIRED)

# fastddsgen 生成的类型支持代码 (在 src/types/ 下, CI 期间生成, 不入库)
set(GENERATED_SOURCES
    src/types/HelloWorldPubSubTypes.cxx
    src/types/HelloWorldTypeObjectSupport.cxx
)

add_executable(publisher src/HelloWorldPublisher.cpp ${GENERATED_SOURCES})
target_include_directories(publisher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(publisher fastdds fastcdr)

add_executable(subscriber src/HelloWorldSubscriber.cpp ${GENERATED_SOURCES})
target_include_directories(subscriber PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(subscriber fastdds fastcdr)
