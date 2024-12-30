# CMakeLists.txt for tests

# Set minimum CMake version (adjust if needed)
cmake_minimum_required(VERSION 3.12)

# Add the test executable
add_executable(ZeroCopyRpcTests  "ConcurrentDictionaryTests.cpp")

# Set C++17 as the standard for the test target
#target_compile_features(ZeroCopyRpcTests PRIVATE cxx_std_17)

#set_target_properties(ZeroCopyRpcTests PROPERTIES
#    CXX_STANDARD 17
#    CXX_STANDARD_REQUIRED ON
#    CXX_EXTENSIONS OFF
#)

# Link the test executable with gtest and the main library
target_link_libraries(ZeroCopyRpcTests PRIVATE gtest gtest_main ZeroCopyRpc)

# Include directories
file(GLOB ZERO_COPY_RPC_HEADERS "${CMAKE_SOURCE_DIR}/ZeroCopyRpc/*.h" "${CMAKE_SOURCE_DIR}/ZeroCopyRpc/*.hpp")
target_include_directories(ZeroCopyRpcTests PRIVATE 
    ${CMAKE_SOURCE_DIR}/ZeroCopyRpc 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable testing
enable_testing()

# Register the test executable with CTest
add_test(NAME ZeroCopyRpcTests COMMAND ZeroCopyRpcTests)

# Add compiler-specific flags if needed
if(MSVC)
    target_compile_options(ZeroCopyRpcTests PRIVATE /W4)
else()
    target_compile_options(ZeroCopyRpcTests PRIVATE -Wall -Wextra)
endif()