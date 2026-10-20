add_library(dimepenny_core STATIC
    core/application_info.cpp
    ipc/protocol.cpp
    ipc/local_server.cpp
    ipc/simulated_bridge.cpp
    application/event_processor.cpp
    persistence/sqlite_store.cpp
    ${PROJECT_SOURCE_DIR}/include/dimepenny/application/event_processor.h
    ${PROJECT_SOURCE_DIR}/include/dimepenny/ipc/simulated_bridge.h
    ${PROJECT_SOURCE_DIR}/include/dimepenny/ipc/local_server.h
)

add_library(DimePenny::Core ALIAS dimepenny_core)

target_include_directories(dimepenny_core
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>"
)

target_link_libraries(dimepenny_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)

target_compile_features(dimepenny_core PUBLIC cxx_std_20)

dimepenny_enable_warnings(dimepenny_core)

