add_executable(apitest apitest.cxx)

target_link_libraries(apitest
    PRIVATE
        ${PROJECT_NAME}
)

set_target_properties(apitest PROPERTIES
    FOLDER "Examples"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

configure_file(expected expected COPYONLY)
configure_file(runtest runtest COPYONLY)
