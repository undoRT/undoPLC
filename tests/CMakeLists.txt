# Test applications, built with -DBUILD_TESTS=ON and run through ctest.
#
# Every test lives in its own directory with its own CMakeLists.txt.

add_subdirectory(test_latch)
add_subdirectory(test_diag)

# TODO The test_cpu, test_log, test_log_multithread, test_plc_placehodler and
# test_task directories are not registered yet. Each of their CMakeLists.txt
# declares add_library(undoPLC STATIC IMPORTED) against a prebuilt archive in
# src/, which clashes with the real undoPLC target defined by the top level
# CMakeLists.txt, so configuring them fails with:
#   add_library cannot create imported target "undoPLC" because another target
#   with the same name already exists.
# They need to link the target directly instead of importing it. Until then
# -DBUILD_TESTS=ON only builds and runs the tests listed above.
