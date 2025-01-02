# CMakeLists.txt for tests

# Set minimum CMake version (adjust if needed)
cmake_minimum_required (VERSION 3.8)

# Add the test executable
add_executable(ZeroCopyRpcTests  "ConcurrentDictionaryTests.cpp" 
"SharedMemoryServerTests.cpp" "IDPoolTests.cpp" "ConcurrentBugTests.cpp" "CyclicBufferTests.cpp" 
"SpinWaitTests.cpp"  "../ZeroCopyRpc/StopWatch.cpp" "../ZeroCopyRpc/StopWatch.h" "ThreadSpinTests.cpp" 
"StopWatchTests.cpp" "SyncLatencyTest.cpp" )

# Link the test executable with gtest and the main library
target_link_libraries(ZeroCopyRpcTests PRIVATE gtest gtest_main ZeroCopyRpc)

# Include directories
file(GLOB ZERO_COPY_RPC_HEADERS "${CMAKE_SOURCE_DIR}/ZeroCopyRpc/*.h" "${CMAKE_SOURCE_DIR}/ZeroCopyRpc/*.hpp")
target_include_directories(ZeroCopyRpcTests PRIVATE 
    ${CMAKE_SOURCE_DIR}/ZeroCopyRpc 
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set Boost root directory
if(DEFINED ENV{SYSTEM_BOOST_ROOT})
    # Optionally, you can retrieve its value
    set(BOOST_ROOT "$ENV{SYSTEM_BOOST_ROOT}")
    message(STATUS "BOOST_ROOT was found on the system. ${BOOST_ROOT}")
else()
    set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/boost")
    message(STATUS "BOOST_ROOT was not found system-wide, will use git-submodule instead. ${BOOST_ROOT}")

endif()

# Include the Boost directory
include_directories("${BOOST_ROOT}")

# Enable testing
enable_testing()

# Register the test executable with CTest
add_test(NAME ZeroCopyRpcTests COMMAND ZeroCopyRpcTests)

# Add compiler-specific flags if needed

if(CMAKE_CXX_COMPILER MATCHES "cl.exe")
    message("Setting flags for MSVC")
    string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(ZeroCopyRpcTests PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
        $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /GL>
    )
    # Enable parallel compilation for MSVC
    target_compile_options(ZeroCopyRpcTests PRIVATE /MP)
 else()
     target_compile_options(ZeroCopyRpcTests PRIVATE
         $<$<CONFIG:Debug>:-Wall -Wextra -g -O0>
         $<$<CONFIG:Release>:-O3 -march=native>
     )
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
