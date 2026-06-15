cmake_minimum_required(VERSION 3.16)
project(undoPLC VERSION 0.1.0 LANGUAGES CXX)

set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option for building tests (default OFF)
option(BUILD_TESTS "Build tests" OFF)

# Include directories
set(UNDOPLC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../include)
set(BOOST_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../third_party)

# ============================================================================
#  Compiler flags (conditional for cross-platform support)
# ============================================================================
if(MSVC)
    # Visual Studio / MSVC on Windows
    set(UNDOPLC_COMPILE_FLAGS
        /W4           # Warning level 4 (similar to -Wall)
        /utf-8        # UTF-8 support for source files
    )
    # Suppress warnings about "unsafe" CRT functions (fopen, strcpy, etc.)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC / Clang / MinGW on Linux/macOS/Windows
    set(UNDOPLC_COMPILE_FLAGS
        -Wall         # Enable all warnings
        -Wextra       # Enable extra warnings
        -Wpedantic    # Strict ISO C++ compliance
    )
endif()

# ============================================================================
#  Static library (core compiler logic)
# ============================================================================
add_library(undoPLC_lib STATIC
    ../../src/undoSystem.cpp
    main.cpp
)

target_include_directories(undoPLC_lib PUBLIC
    ${UNDOPLC_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
)

target_compile_options(undoPLC_lib PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

# ============================================================================
#  CLI executable (main compiler driver)
# ============================================================================
add_executable(undoPLC main.cpp)

target_link_libraries(undoPLC PRIVATE undoPLC_lib)

target_include_directories(undoPLC PRIVATE
    ${UNDOPLC_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
)

target_compile_options(undoPLC PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

# ============================================================================
#  Tests (optional, enabled with -DBUILD_TESTS=ON)
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    # Add test directory if you have unit tests
    # add_subdirectory(tests)
endif()

# ============================================================================
#  Installation
# ============================================================================
install(TARGETS undoPLC RUNTIME DESTINATION bin)
install(TARGETS undoPLC_lib ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)

# ============================================================================
#  CPack - Package generation for distribution
# ============================================================================
include(CPack)

set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "undoPLC")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Structured Text to C++ Compiler")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VENDOR "undoRT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# ============================================================================
#  Print configuration summary
# ============================================================================
message(STATUS "")
message(STATUS "undoPLC Configuration:")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build tests:       ${BUILD_TESTS}")
message(STATUS "  Install prefix:    ${CMAKE_INSTALL_PREFIX}")
if(DOXYGEN_FOUND)
    message(STATUS "  Documentation:     Enabled")
else()
    message(STATUS "  Documentation:     Disabled (Doxygen not found)")
endif()
message(STATUS "")