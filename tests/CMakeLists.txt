include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(unit_tests basic_tests.cpp advanced_tests.cpp)

target_link_libraries(unit_tests
        PRIVATE
        event_system
        GTest::gtest_main
)
target_compile_features(unit_tests PRIVATE cxx_std_23)
include(GoogleTest)
gtest_discover_tests(unit_tests)