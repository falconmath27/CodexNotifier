add_executable(dimepenny_bridge
    main.cpp
)

set_target_properties(dimepenny_bridge PROPERTIES
    OUTPUT_NAME "DimePennyBridge"
)

target_link_libraries(dimepenny_bridge
    PRIVATE
        DimePenny::Core
        Qt6::Network
)

dimepenny_enable_warnings(dimepenny_bridge)

install(TARGETS dimepenny_bridge
    RUNTIME DESTINATION bin
)

add_executable(dimepenny_interactive interactive_main.cpp)
set_target_properties(dimepenny_interactive PROPERTIES OUTPUT_NAME "DimePennyInteractive")
target_link_libraries(dimepenny_interactive PRIVATE DimePenny::Core)
dimepenny_enable_warnings(dimepenny_interactive)
install(TARGETS dimepenny_interactive RUNTIME DESTINATION bin)
install(FILES ${PROJECT_SOURCE_DIR}/tools/Start-DimePennyCodex.ps1 DESTINATION bin)

