
cmake_minimum_required(VERSION 3.15)

project(CampusBuggyDispatch LANGUAGES C)

add_executable(buggy_dispatch
    main.c
    Output/outputwriter.c
    GraphRouting/graph.c
    Dispatch/dispatch.c
    lib/cJSON.c
)

target_include_directories(buggy_dispatch PRIVATE
    GraphRouting
    Dispatch
    Output
    lib
)

enable_testing()

add_executable(test_all
    tests/test_all.c
    GraphRouting/graph.c
    Dispatch/dispatch.c
    lib/cJSON.c
)

target_include_directories(test_all PRIVATE
    GraphRouting
    Dispatch
    Output
    lib
)

add_test(NAME integration_tests COMMAND test_all)

set_tests_properties(integration_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
