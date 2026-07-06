cmake_minimum_required(VERSION 3.23)

project(rpc_calculator
    VERSION 1.0.0
    DESCRIPTION "Fast DDS RPC calculator client/server example"
    LANGUAGES CXX
)

find_package(fastcdr 2   REQUIRED)
find_package(fastdds 3.4 REQUIRED)
find_package(Threads REQUIRED)

add_library(calculator_gen STATIC)
target_sources(calculator_gen
    PRIVATE
        src/types/calculatorClient.cxx
        src/types/calculatorServer.cxx
        src/types/calculatorPubSubTypes.cxx
        src/types/calculatorTypeObjectSupport.cxx
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS types
        FILES calculator.hpp
              calculator_details.hpp
              calculatorClient.hpp
              calculatorServer.hpp
              calculatorServerImpl.hpp
              calculatorPubSubTypes.hpp
              calculatorCdrAux.hpp
              calculatorTypeObjectSupport.hpp
)
target_compile_features(calculator_gen PUBLIC cxx_std_17)
target_link_libraries(calculator_gen PUBLIC fastdds fastcdr Threads::Threads)

add_executable(calculator_server src/CalculatorServer.cpp)
target_link_libraries(calculator_server PRIVATE calculator_gen)

add_executable(calculator_client src/CalculatorClient.cpp)
target_link_libraries(calculator_client PRIVATE calculator_gen)
