# CMakeLists.txt for tests

# Set minimum CMake version (adjust if needed)
cmake_minimum_required (VERSION 3.8)

# Add the test executable
add_executable(ZeroCopyRpcTests  "ConcurrentDictionaryTests.cpp" 
"SharedMemoryServerTests.cpp" "IDPoolTests.cpp" "ConcurrentBugTests.cpp")

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

 message("CMAKE_BINARY_DIR:        ${CMAKE_BINARY_DIR}/bin/ZeroCopyRpc.dll")
 message("Target output directory: ${CMAKE_BINARY_DIR}/tests")
 file(GLOB DLL_FILES "${CMAKE_BINARY_DIR}/bin/ZeroCopyRpc.*")

foreach(DLL ${DLL_FILES})
    add_custom_command(TARGET ZeroCopyRpcTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL}" "${CMAKE_BINARY_DIR}/tests"
    )
endforeach()
