cmake_minimum_required(VERSION 3.16)
project(undoPLC_test VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
#  Include directories
# ============================================================================
set(UNDOPLC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../include)
set(BOOST_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../third_party/boost_1_91_0)

# ============================================================================
#  Compiler flags
# ============================================================================
set(UNDOPLC_COMPILE_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -pthread
)

# ============================================================================
#  Import the undoPLC library target
# ============================================================================
add_library(undoPLC STATIC IMPORTED)

# Find the library file
find_library(UNDOPLC_LIB_PATH
    NAMES undoPLC
    PATHS ${CMAKE_SOURCE_DIR}/../../src
    NO_DEFAULT_PATH
)

if(NOT UNDOPLC_LIB_PATH)
    message(FATAL_ERROR "undoPLC library not found at ${CMAKE_SOURCE_DIR}/../../src")
endif()

set_target_properties(undoPLC PROPERTIES
    IMPORTED_LOCATION ${UNDOPLC_LIB_PATH}
    INTERFACE_INCLUDE_DIRECTORIES ${UNDOPLC_INCLUDE_DIR}
)

message(STATUS "Imported undoPLC library: ${UNDOPLC_LIB_PATH}")

# ============================================================================
#  Test executable
# ============================================================================
add_executable(test_undoPLC
    main.cpp
)

target_include_directories(test_undoPLC PRIVATE
    ${UNDOPLC_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
)

target_compile_options(test_undoPLC PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

target_link_libraries(test_undoPLC
    undoPLC
    pthread
    rt
)

# ============================================================================
#  CTest integration
# ============================================================================
enable_testing()
add_test(NAME test_undoPLC COMMAND test_undoPLC)