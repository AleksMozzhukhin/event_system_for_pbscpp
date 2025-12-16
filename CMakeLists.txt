cmake_minimum_required(VERSION 3.20)
project(event_system)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

add_library(event_system INTERFACE)

target_include_directories(event_system INTERFACE include)

target_sources(event_system INTERFACE
        include/event_system/EventSystem.hpp
        include/event_system/internal/IDispatcher.hpp
        include/event_system/internal/Dispatcher.hpp
)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)

# clang-format (auto format before build + optional check target)
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)

if(CLANG_FORMAT_EXECUTABLE)
    set(CLANG_FORMAT_SOURCES
            ${CMAKE_SOURCE_DIR}/include/event_system/EventSystem.hpp
            ${CMAKE_SOURCE_DIR}/include/event_system/internal/IDispatcher.hpp
            ${CMAKE_SOURCE_DIR}/include/event_system/internal/Dispatcher.hpp
            ${CMAKE_SOURCE_DIR}/src/main.cpp
            ${CMAKE_SOURCE_DIR}/tests/basic_tests.cpp
            ${CMAKE_SOURCE_DIR}/tests/advanced_tests.cpp
    )

    # Format in-place:
    #   cmake --build . --target clang_format
    add_custom_target(
            clang_format
            COMMAND ${CLANG_FORMAT_EXECUTABLE} -i --style=file ${CLANG_FORMAT_SOURCES}
            COMMENT "Running clang-format (in-place)"
            VERBATIM
    )

    # Check-only:
    #   cmake --build . --target clang_format_check
    add_custom_target(
            clang_format_check
            COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror --style=file ${CLANG_FORMAT_SOURCES}
            COMMENT "Running clang-format check"
            VERBATIM
    )

    add_custom_target(
            clang_format_auto ALL
            DEPENDS clang_format
    )

    add_test(
            NAME clang_format
            COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run -Werror --style=file ${CLANG_FORMAT_SOURCES}
    )
else()
    message(STATUS "clang-format not found: formatting targets are disabled")
endif()
