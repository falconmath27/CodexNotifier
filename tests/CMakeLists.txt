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

