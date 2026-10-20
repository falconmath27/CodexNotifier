# Explicit VERBATIM resource command also supports Windows checkout paths containing '&'.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_resources.cpp
    COMMAND Qt6::rcc --name ui -o ${CMAKE_CURRENT_BINARY_DIR}/ui_resources.cpp ${CMAKE_CURRENT_SOURCE_DIR}/ui.qrc
    DEPENDS ui.qrc qml/Activation.qml qml/Pet.qml qml/PetVisual.qml
        assets/pets/idle/orange/frame_1.png assets/pets/idle/orange/frame_2.png
        assets/pets/idle/orange/frame_3.png assets/pets/idle/orange/frame_4.png
        assets/pets/idle/yellow/frame_1.png assets/pets/idle/yellow/frame_2.png
        assets/pets/idle/pink/frame_1.png assets/pets/idle/pink/frame_2.png
        assets/pets/idle/green/frame_1.png assets/pets/idle/green/frame_2.png
        assets/pets/idle/blue/frame_1.png assets/pets/idle/blue/frame_2.png
        assets/pets/idle/purple/frame_1.png assets/pets/idle/purple/frame_2.png
        assets/pets/idle/white/frame_1.png assets/pets/idle/white/frame_2.png
        assets/pets/idle/blue-green/frame_1.png assets/pets/idle/blue-green/frame_2.png
        assets/pets/idle/grey/frame_1.png assets/pets/idle/grey/frame_2.png
        assets/pets/idle/golden/frame_1.png assets/pets/idle/golden/frame_2.png
        assets/pets/working/frame_1.png assets/pets/working/frame_2.png
        assets/pets/working/frame_3.png assets/pets/working/frame_4.png
        assets/pets/approval/frame_1.png assets/pets/approval/frame_2.png
        assets/pets/approval/frame_3.png assets/pets/approval/frame_4.png
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

