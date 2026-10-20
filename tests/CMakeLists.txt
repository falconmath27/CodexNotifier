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

add_executable(dimepenny_bridge_tests integration/simulated_bridge_test.cpp)
target_link_libraries(dimepenny_bridge_tests PRIVATE DimePenny::Core Qt6::Test)
dimepenny_enable_warnings(dimepenny_bridge_tests)
add_test(NAME dimepenny.ipc.bridge COMMAND dimepenny_bridge_tests)
set_tests_properties(dimepenny.ipc.bridge PROPERTIES TIMEOUT 30)

foreach(scenario normal duplicate reconnect malformed)
    add_test(NAME dimepenny.bridge.demo.${scenario}
        COMMAND dimepenny_bridge --demo --scenario ${scenario})
    set_tests_properties(dimepenny.bridge.demo.${scenario} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(dimepenny_event_processor_tests integration/event_processor_test.cpp)
target_link_libraries(dimepenny_event_processor_tests PRIVATE DimePenny::Core Qt6::Test)
dimepenny_enable_warnings(dimepenny_event_processor_tests)
add_test(NAME dimepenny.application.events COMMAND dimepenny_event_processor_tests)
set_tests_properties(dimepenny.application.events PROPERTIES TIMEOUT 30)

add_executable(dimepenny_activation_tests integration/activation_window_test.cpp)
add_executable(dimepenny_interactive_tests integration/interactive_test.cpp)
add_executable(dimepenny_fake_interactive_codex integration/fake_interactive_codex.cpp)
target_link_libraries(dimepenny_fake_interactive_codex PRIVATE Qt6::Core)
dimepenny_enable_warnings(dimepenny_fake_interactive_codex)
target_link_libraries(dimepenny_interactive_tests PRIVATE dimepenny_ui Qt6::Test)
dimepenny_enable_warnings(dimepenny_interactive_tests)
add_dependencies(dimepenny_interactive_tests dimepenny_interactive dimepenny_fake_interactive_codex)
add_test(NAME dimepenny.integration.interactive COMMAND dimepenny_interactive_tests)
set_tests_properties(dimepenny.integration.interactive PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=FluentWinUI3")
add_executable(dimepenny_fake_codex integration/fake_codex.cpp)
target_link_libraries(dimepenny_fake_codex PRIVATE Qt6::Core)
dimepenny_enable_warnings(dimepenny_fake_codex)
add_executable(dimepenny_codex_exec_tests integration/codex_exec_test.cpp)
target_link_libraries(dimepenny_codex_exec_tests PRIVATE DimePenny::Core Qt6::Test)
dimepenny_enable_warnings(dimepenny_codex_exec_tests)
add_dependencies(dimepenny_codex_exec_tests dimepenny_fake_codex)
add_dependencies(dimepenny_activation_tests dimepenny_fake_codex)
add_test(NAME dimepenny.integration.codex_exec COMMAND dimepenny_codex_exec_tests)
set_tests_properties(dimepenny.integration.codex_exec PROPERTIES TIMEOUT 30)
add_executable(dimepenny_persistence_tests integration/persistence_test.cpp)
target_link_libraries(dimepenny_persistence_tests PRIVATE DimePenny::Core Qt6::Test)
dimepenny_enable_warnings(dimepenny_persistence_tests)
add_test(NAME dimepenny.persistence.sqlite COMMAND dimepenny_persistence_tests)
set_tests_properties(dimepenny.persistence.sqlite PROPERTIES TIMEOUT 30)
target_link_libraries(dimepenny_activation_tests PRIVATE dimepenny_ui Qt6::Test)
dimepenny_enable_warnings(dimepenny_activation_tests)
add_test(NAME dimepenny.ui.activation COMMAND dimepenny_activation_tests)
set_tests_properties(dimepenny.ui.activation PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=FluentWinUI3")

add_executable(dimepenny_pet_tests integration/pet_window_test.cpp)
target_link_libraries(dimepenny_pet_tests PRIVATE dimepenny_ui Qt6::Test)
dimepenny_enable_warnings(dimepenny_pet_tests)
add_test(NAME dimepenny.ui.pet COMMAND dimepenny_pet_tests)
set_tests_properties(dimepenny.ui.pet PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=FluentWinUI3")

