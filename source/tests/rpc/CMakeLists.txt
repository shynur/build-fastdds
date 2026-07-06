cmake_minimum_required(VERSION 3.23)

project(rpc_calculator
    VERSION 1.0.0
    DESCRIPTION "Fast DDS RPC calculator client/server example"
    LANGUAGES CXX
)

find_package(fastcdr 2   REQUIRED)
find_package(fastdds 3.4 REQUIRED)
find_package(Threads REQUIRED)

# fastddsgen 生成的类型/RPC 代码, 打成一个静态库供 client 与 server 复用 (只编译一次).
add_library(calculator_gen STATIC)
target_sources(calculator_gen
    PRIVATE
        src/types/calculatorClient.cxx
        src/types/calculatorServer.cxx
        src/types/calculatorPubSubTypes.cxx
        src/types/calculatorTypeObjectSupport.cxx
    PUBLIC
        FILE_SET generated_headers TYPE HEADERS
            BASE_DIRS src
            FILES
                src/types/calculator.hpp
                src/types/calculator_details.hpp
                src/types/calculatorClient.hpp
                src/types/calculatorServer.hpp
                src/types/calculatorServerImpl.hpp
                src/types/calculatorPubSubTypes.hpp
                src/types/calculatorCdrAux.hpp
                src/types/calculatorTypeObjectSupport.hpp
)
target_include_directories(calculator_gen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(calculator_gen PUBLIC cxx_std_17)
target_link_libraries(calculator_gen PUBLIC fastdds fastcdr Threads::Threads)

add_executable(calculator_server src/CalculatorServer.cpp)
target_link_libraries(calculator_server PRIVATE calculator_gen)

add_executable(calculator_client src/CalculatorClient.cpp)
target_link_libraries(calculator_client PRIVATE calculator_gen)
