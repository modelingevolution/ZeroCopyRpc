# CMakeList.txt : CMake project for NativeRpc, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (ZeroCopyRpc "NativeRpc.cpp" "NativeRpc.h" "IDPool.hpp"  "TypeDefs.h" "Messages.h" "Random.h" "SharedMemoryClient.h" "SharedMemoryClient.cpp" "ProcessUtils.cpp" "Random.cpp")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ZeroCopyRpc PROPERTY CXX_STANDARD 20)
endif()

# Set Boost root directory
set(BOOST_ROOT "C:/Users/rafal/Downloads/boost_1_87_0")

# Set the Boost include directory
include_directories("${BOOST_ROOT}")

# Find Boost libraries (set required components)
find_package(Boost REQUIRED COMPONENTS filesystem system)

# Link Boost libraries
target_link_libraries(ZeroCopyRpc Boost::filesystem Boost::system)