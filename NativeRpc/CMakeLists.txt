# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "CMake CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMake CXX Compiler ID: ${CMAKE_CXX_COMPILER_ID}")

if(CMAKE_CXX_COMPILER MATCHES "cl.exe")
    # MSVC-specific flags
    message("Setting flags for MSVC")
    add_compile_options(/wd4251 /wd4273)

    string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /RTC1 /MDd /D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /GL /DNDEBUG /MD")
else()
    # GCC/Clang flags
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project ("ZeroCopyRpc")

# Include sub-projects.
add_subdirectory ("ZeroCopyRpc")

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Google Test-specific option
add_subdirectory(external/googletest)

#enable_testing()
add_subdirectory(tests)

