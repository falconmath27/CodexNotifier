# Explicit VERBATIM resource command also supports Windows checkout paths containing '&'.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_resources.cpp
    COMMAND Qt6::rcc --name ui -o ${CMAKE_CURRENT_BINARY_DIR}/ui_resources.cpp ${CMAKE_CURRENT_SOURCE_DIR}/ui.qrc
    DEPENDS ui.qrc qml/Activation.qml qml/Pet.qml
    VERBATIM)
add_library(dimepenny_ui STATIC activation_window.cpp activation_window.h pet_window.cpp pet_window.h
    ${CMAKE_CURRENT_BINARY_DIR}/ui_resources.cpp)
target_include_directories(dimepenny_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dimepenny_ui PUBLIC DimePenny::Core Qt6::Quick Qt6::QuickControls2)
dimepenny_enable_warnings(dimepenny_ui)

add_executable(dimepenny_app WIN32
    main.cpp
)

set_target_properties(dimepenny_app PROPERTIES
    OUTPUT_NAME "DimePenny"
)

target_link_libraries(dimepenny_app
    PRIVATE
        dimepenny_ui
        Qt6::Network
        Qt6::Sql
        Qt6::Quick
)

dimepenny_enable_warnings(dimepenny_app)

install(TARGETS dimepenny_app
    RUNTIME DESTINATION bin
)

