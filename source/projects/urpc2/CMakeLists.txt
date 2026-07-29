cmake_minimum_required(VERSION 3.23)

project(urpc2
    VERSION 0.1
    LANGUAGES CXX
)

find_package(fastcdr 2 REQUIRED)
find_package(fastdds 3.4 REQUIRED)
find_package(Threads REQUIRED)

# 获取 IDL 由 fastddsgen 生成的产物 (只取 *.cxx 和 *.hpp 文件)
set(urpc2_processor_idl src/types/processor.idl)
get_filename_component(urpc2_processor_idl_dir "${urpc2_processor_idl}" DIRECTORY)
get_filename_component(urpc2_processor_idl_base "${urpc2_processor_idl}" NAME_WE)
file(GLOB urpc2_processor_generated_sources CONFIGURE_DEPENDS
    "${urpc2_processor_idl_dir}/${urpc2_processor_idl_base}*.cxx"
)
file(GLOB urpc2_processor_generated_headers CONFIGURE_DEPENDS
    "${urpc2_processor_idl_dir}/${urpc2_processor_idl_base}*.hpp"
)

add_library(urpc2_processor_gen STATIC)
set_target_properties(urpc2_processor_gen
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
target_sources(urpc2_processor_gen
    PRIVATE
        ${urpc2_processor_generated_sources}
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            ${urpc2_processor_generated_headers}
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

add_library(urpc2 SHARED)
add_library(urpc2::urpc2 ALIAS urpc2)
target_sources(urpc2
    PRIVATE
        src/urpc2.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/urpc2.hpp
)
target_compile_features(urpc2
    PUBLIC
        cxx_std_17
)
target_link_libraries(urpc2
    PRIVATE
        urpc2_processor_gen
)

add_library(urpc2_rbk SHARED)
add_library(urpc2::urpc2_rbk ALIAS urpc2_rbk)
target_sources(urpc2_rbk
    PRIVATE
        src/urpc2_rbk.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/urpc2_rbk.hpp
            include/nlohmann/json.hpp
)
target_link_libraries(urpc2_rbk
    PUBLIC
        urpc2
)




# Apps for 示例/测试

add_executable(urpc2_simple examples/simple.cpp)
target_link_libraries(urpc2_simple urpc2)

add_executable(urpc2_mesh_node examples/mesh_node.cpp)
target_link_libraries(urpc2_mesh_node urpc2)

add_executable(urpc2_mesh_node_rbk examples/mesh_node_rbk.cpp)
target_link_libraries(urpc2_mesh_node_rbk urpc2_rbk)

add_executable(urpc2_binary_passthrough examples/binary_passthrough.cpp)
target_link_libraries(urpc2_binary_passthrough urpc2)




#--------------- install -------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(
    TARGETS
        urpc2
        urpc2_rbk
    EXPORT
        urpc2Targets
    FILE_SET
        HEADERS
)

install(
       EXPORT  urpc2Targets
  DESTINATION  ${CMAKE_INSTALL_LIBDIR}/cmake/urpc2
    NAMESPACE  urpc2::
)

install(
    FILES
        cmake/urpc2Config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/urpc2
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/urpc2ConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/urpc2ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/urpc2
)
