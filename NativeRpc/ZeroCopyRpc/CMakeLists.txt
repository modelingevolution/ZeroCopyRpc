# CMakeLists.txt : CMake project for ZeroCopyRpc, includes source and defines project-specific logic.
#

# Add the shared library target
add_library(ZeroCopyRpc SHARED
    "SharedMemoryServer.cpp" "SharedMemoryServer.h"
    "IDPool.hpp" "TypeDefs.h"
    "Messages.h" "Random.h"
    "SharedMemoryClient.h" "SharedMemoryClient.cpp"
    "ProcessUtils.cpp" "Random.cpp"
 "TypeDefs.cpp" "ThreadSpin.h" "ThreadSpin.cpp" "PeriodicTimer.h" "PeriodicTimer.cpp" "NamedSemaphore.h" "NamedSemaphore.cpp" "CrossPlatform.h" "CrossPlatform.cpp" "ZeroCopyRpcException.h" "ZeroCopyRpcException.cpp")
target_compile_definitions(ZeroCopyRpc PRIVATE BUILD_DLL)

target_include_directories(ZeroCopyRpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Set C++ standard
if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET ZeroCopyRpc PROPERTY CXX_STANDARD 20)
endif()

# Set Boost root directory
set(BOOST_ROOT "../external/boost")

# Include the Boost directory
include_directories("${BOOST_ROOT}")

# Find Boost libraries (set required components)
find_package(Boost REQUIRED COMPONENTS filesystem system)

# Link Boost libraries
target_link_libraries(ZeroCopyRpc Boost::filesystem Boost::system)

# Create an executable that uses the shared library
add_executable(ZeroCopyRpcExecutable
    "Program.cpp" 
)

# Link the executable to the shared library
target_link_libraries(ZeroCopyRpcExecutable PRIVATE ZeroCopyRpc)

# Set output directories for the shared library
set_target_properties(ZeroCopyRpc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Set output directory for the executable
set_target_properties(ZeroCopyRpcExecutable PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
