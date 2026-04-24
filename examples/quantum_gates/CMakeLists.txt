add_executable(quantum_gates quantum_gates.cpp)

target_link_libraries(quantum_gates
    PRIVATE
        ${PROJECT_NAME}
        nlohmann_json::nlohmann_json
)

set_target_properties(quantum_gates PROPERTIES FOLDER "Examples")
configure_file(h.mosf   h.mosf   COPYONLY)
configure_file(ccx.mosf ccx.mosf COPYONLY)
configure_file(runtest runtest COPYONLY)