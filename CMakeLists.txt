cmake_minimum_required(VERSION 3.16)
project(ToyC LANGUAGES CXX)

add_executable(compiler
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/semantic.cpp
)
set_target_properties(compiler PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
if(MSVC)
    target_compile_options(compiler PRIVATE /W4 /permissive-)
else()
    target_compile_options(compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()
