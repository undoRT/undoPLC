cmake_minimum_required(VERSION 3.16)
project(undoPLC_test VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
#  Include directories
# ============================================================================
set(UNDOPLC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
set(BOOST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/boost_1_91_0)
set(CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/undoCore/include)

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
#  Test executable
#
#  Unlike the other undoPLC tests this one links the real undoPLC target
#  (defined by the top level CMakeLists.txt) instead of importing a prebuilt
#  archive, because it exercises the UndoMasterTaskBase::onCycleEnd() hook and
#  therefore needs the library symbols. undoDiag itself is header only.
# ============================================================================
add_executable(test_undoDiag
    main.cpp
)

target_include_directories(test_undoDiag PRIVATE
    ${UNDOPLC_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
    ${CORE_INCLUDE_DIR}
)

target_compile_options(test_undoDiag PRIVATE
    ${UNDOPLC_COMPILE_FLAGS}
)

target_link_libraries(test_undoDiag
    undoPLC
    pthread
    rt
)

# ============================================================================
#  CTest integration
# ============================================================================
enable_testing()
add_test(NAME test_undoDiag COMMAND test_undoDiag)
