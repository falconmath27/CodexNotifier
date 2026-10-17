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

