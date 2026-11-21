if(BUILD_TESTING AND WIN32)
    add_test_executable(child_process_wait_tests
        ${PROJECT_SOURCE_DIR}/core/libs/tests/ChildProcessWait.cpp
        ${PROJECT_SOURCE_DIR}/core/libs/prx/libc/src/ChildProcesses.cpp)
    target_include_directories(child_process_wait_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/libs)
    configure_host_test_executable(child_process_wait_tests)
    foreach(mode IN ITEMS invalid live concurrent)
        add_test(NAME child_process_wait_${mode} COMMAND child_process_wait_tests ${mode})
        set_tests_properties(child_process_wait_${mode} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
