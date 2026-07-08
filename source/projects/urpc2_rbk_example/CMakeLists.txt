cmake_minimum_required(VERSION 3.23)

project(urpc2_rbk_example
    VERSION 0.1
    LANGUAGES CXX
)

find_package(urpc2 REQUIRED)

add_executable(urpc2_rbk_example src/urpc2_rbk_example.cpp)
target_link_libraries(urpc2_rbk_example
    PRIVATE
        urpc2::urpc2_rbk
)

include(GNUInstallDirs)
install(
    TARGETS urpc2_rbk_example
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
