add_executable(bddrand bddrand.cxx)

target_link_libraries(bddrand
    PRIVATE
        ${PROJECT_NAME}
)

set_target_properties(bddrand PROPERTIES
    FOLDER "Examples"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

configure_file(expected expected COPYONLY)
configure_file(runtest runtest COPYONLY)
