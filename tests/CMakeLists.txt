add_executable(dimepenny_core_tests
    unit/core_smoke_test.cpp
)

target_link_libraries(dimepenny_core_tests
    PRIVATE
        DimePenny::Core
        Qt6::Test
)

dimepenny_enable_warnings(dimepenny_core_tests)

add_test(
    NAME dimepenny.core.smoke
    COMMAND dimepenny_core_tests
)

add_executable(dimepenny_domain_tests unit/domain_models_test.cpp)
target_link_libraries(dimepenny_domain_tests PRIVATE DimePenny::Core)
dimepenny_enable_warnings(dimepenny_domain_tests)
add_test(NAME dimepenny.domain.models COMMAND dimepenny_domain_tests)

add_executable(dimepenny_state_machine_tests unit/agent_state_machine_test.cpp)
target_link_libraries(dimepenny_state_machine_tests PRIVATE DimePenny::Core)
dimepenny_enable_warnings(dimepenny_state_machine_tests)
add_test(NAME dimepenny.domain.state_machine COMMAND dimepenny_state_machine_tests)

add_executable(dimepenny_registry_tests unit/session_registry_test.cpp)
target_link_libraries(dimepenny_registry_tests PRIVATE DimePenny::Core)
dimepenny_enable_warnings(dimepenny_registry_tests)
add_test(NAME dimepenny.domain.registry COMMAND dimepenny_registry_tests)

add_executable(dimepenny_protocol_tests unit/ipc_protocol_test.cpp)
target_link_libraries(dimepenny_protocol_tests PRIVATE DimePenny::Core)
dimepenny_enable_warnings(dimepenny_protocol_tests)
add_test(NAME dimepenny.ipc.protocol COMMAND dimepenny_protocol_tests)

add_executable(dimepenny_server_tests integration/local_server_test.cpp)
target_link_libraries(dimepenny_server_tests PRIVATE DimePenny::Core Qt6::Test)
dimepenny_enable_warnings(dimepenny_server_tests)
add_test(NAME dimepenny.ipc.server COMMAND dimepenny_server_tests)
set_tests_properties(dimepenny.ipc.server PROPERTIES TIMEOUT 45)

