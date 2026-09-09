add_executable(evddtest evddtest.cpp)

target_link_libraries(evddtest
    PRIVATE
        ${PROJECT_NAME}
)

set_target_properties(evddtest PROPERTIES
    FOLDER "Tests"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

configure_file(expected expected COPYONLY)
configure_file(runtest runtest COPYONLY)
