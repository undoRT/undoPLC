cmake_minimum_required(VERSION 3.16)
project(undoPLC VERSION 0.1.0 LANGUAGES CXX)

set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option for building tests (default OFF)
option(BUILD_TESTS "Build tests" OFF)

# ============================================================================
#  Include directories (Boost from third_party)
# ============================================================================
set(UNDOPLC_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(BOOST_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/boost_1_91_0)

# ============================================================================
#  Output directories
# ============================================================================
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/src)

# ============================================================================
#  Compiler flags (Linux only)
# ============================================================================
set(UNDOPLC_COMPILE_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -pthread
)

# ============================================================================
#  Static library (core real-time infrastructure)
# ============================================================================
add_library(undoPLC STATIC
    src/undoSystem.cpp
    src/undoLog.cpp
    src/undoTasks.cpp
)

target_include_directories(undoPLC PUBLIC
    ${UNDOPLC_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
)

target_compile_options(undoPLC PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

target_link_libraries(undoPLC
    PUBLIC
    pthread
    rt
)

# ============================================================================
#  Installation
# ============================================================================
install(TARGETS undoPLC
    ARCHIVE DESTINATION src
    LIBRARY DESTINATION src
    RUNTIME DESTINATION src
)

install(DIRECTORY include/ DESTINATION include)

# ============================================================================
#  CPack - Package generation for distribution
# ============================================================================
include(CPack)

set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "undoPLC")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multi-threaded PLC infrastructure with deterministic real-time execution")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VENDOR "undoRT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# ============================================================================
#  Doxygen documentation (optional)
# ============================================================================
find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} COPYONLY)
    
    add_custom_target(docs
        ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    message(STATUS "Doxygen: Enabled (documentation will be generated in docs/)")
else()
    message(STATUS "Doxygen: Disabled (install doxygen to generate API documentation)")
endif()

# ============================================================================
#  Tests (optional)
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests)
        add_subdirectory(tests)
    endif()
endif()

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
message(STATUS "  Boost include:     ${BOOST_INCLUDE_DIR}")
message(STATUS "  Library output:    ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
if(DOXYGEN_FOUND)
    message(STATUS "  Documentation:     Enabled")
else()
    message(STATUS "  Documentation:     Disabled (Doxygen not found)")
endif()
message(STATUS "")