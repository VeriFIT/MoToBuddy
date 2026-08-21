add_executable(mtbddtest mtbddtest.cpp)

target_link_libraries(mtbddtest
    PRIVATE
        ${PROJECT_NAME}
)

set_target_properties(mtbddtest PROPERTIES
    FOLDER "Examples"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

configure_file(expected expected COPYONLY)
configure_file(runtest runtest COPYONLY)
