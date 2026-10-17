add_executable(dimepenny_app WIN32
    main.cpp
)

set_target_properties(dimepenny_app PROPERTIES
    OUTPUT_NAME "DimePenny"
)

target_link_libraries(dimepenny_app
    PRIVATE
        DimePenny::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Widgets
)

dimepenny_enable_warnings(dimepenny_app)

install(TARGETS dimepenny_app
    RUNTIME DESTINATION bin
)

