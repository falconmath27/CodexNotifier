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

