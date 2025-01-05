# CMakeLists.txt : CMake project for ZeroCopyRpc, includes source and defines project-specific logic.
#

# Add the shared library target
add_library(ZeroCopyRpc SHARED
     "SharedMemoryServer.cpp" "SharedMemoryServer.h"
     "IDPool.hpp" "TypeDefs.h"
     "Messages.h" "Random.h"
     "SharedMemoryClient.h" "SharedMemoryClient.cpp"
     "ProcessUtils.cpp" "Random.cpp"
     "TypeDefs.cpp" "ThreadSpin.h" "ThreadSpin.cpp" 
     "PeriodicTimer.h" 
     "PeriodicTimer.cpp" 
     "NamedSemaphore.h" "NamedSemaphore.cpp" "CrossPlatform.h" 
     "CrossPlatform.cpp" "ZeroCopyRpcException.h"
     "ZeroCopyRpcException.cpp" "TcpReplicator.h" "TcpReplicator.cpp" 
     "ISharedMemoryClient.h" "TestFrame.h" "TestFrame.cpp" 
     "UdpReplicator.h" "UdpReplicator.cpp")
target_compile_definitions(ZeroCopyRpc PRIVATE BUILD_DLL)

target_include_directories(ZeroCopyRpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Set C++ standard
if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET ZeroCopyRpc PROPERTY CXX_STANDARD 20)
endif()

if(DEFINED ENV{SYSTEM_BOOST_ROOT})
    # Optionally, you can retrieve its value
    set(BOOST_ROOT "$ENV{SYSTEM_BOOST_ROOT}")
    set(Boost_INCLUDE_DIR ${BOOST_ROOT})
    message(STATUS "BOOST_ROOT was found on the system. ${BOOST_ROOT}")
else()
    set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../external/boost")
    message(STATUS "BOOST_ROOT was not found system-wide, will use git-submodule instead. ${BOOST_ROOT}")

endif()
# Set Boost root directory

# Include the Boost directory
include_directories("${BOOST_ROOT}")
link_directories("${BOOST_ROOT}/stage/lib")

set(Boost_USE_STATIC_LIBS OFF)
# Find Boost libraries (set required components)
find_package(Boost REQUIRED COMPONENTS filesystem system program_options log log_setup thread)



if(CMAKE_CXX_COMPILER MATCHES "cl.exe")
    # WINDOWS
    target_link_libraries(ZeroCopyRpc Boost::filesystem Boost::system Boost::program_options Boost::log Boost::log_setup Boost::log)
else()
    # LINUX
    target_link_libraries(ZeroCopyRpc PRIVATE 
        Boost::filesystem 
        Boost::system 
        Boost::program_options 
        Boost::log 
        Boost::log_setup 
        Boost::thread
        atomic 
        pthread)
    
    add_definitions(-DBOOST_LOG_DYN_LINK)

endif()

# Create an executable that uses the shared library
add_executable(zq
    "Program.cpp" 
)

# Link the executable to the shared library
target_link_libraries(zq PRIVATE ZeroCopyRpc Boost::filesystem Boost::system Boost::program_options Boost::log Boost::log_setup)

# Set output directories for the shared library
set_target_properties(ZeroCopyRpc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Set output directory for the executable
set_target_properties(zq PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
