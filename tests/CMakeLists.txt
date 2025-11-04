# Always show failing output and keep tests short by default
set(CTEST_OUTPUT_ON_FAILURE ON)
set_property(GLOBAL PROPERTY TIMEOUT 30)

# Helper(s) shared by subfolders
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AddCliTest)
include(doctest)

# Split by test “type” for clean labels & easy CI targeting
add_subdirectory(cli)
add_subdirectory(unit)
add_subdirectory(e2e)
