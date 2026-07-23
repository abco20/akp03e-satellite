cmake_minimum_required(VERSION 3.20)
project(akp03e_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(akp03e_host_tests
    test_main.cpp
    ../components/akp03e_protocol/akp03e_protocol.cpp
    ../components/companion_protocol/companion_protocol.cpp
    ../components/image_transform/image_transform.cpp
    ../components/akp03e_tx_sequence/akp03e_tx_sequence.cpp
    ../components/akp03e_usb_descriptor/akp03e_usb_descriptor.cpp
)

target_include_directories(akp03e_host_tests PRIVATE
    ../components/akp03e_protocol/include
    ../components/companion_protocol/include
    ../components/image_transform/include
    ../components/akp03e_tx_sequence/include
    ../components/akp03e_usb_descriptor/include
)

target_compile_options(akp03e_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME akp03e_host_tests COMMAND akp03e_host_tests)
