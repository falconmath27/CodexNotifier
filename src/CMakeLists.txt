add_library(dimepenny_core STATIC
    core/application_info.cpp
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
)

target_compile_features(dimepenny_core PUBLIC cxx_std_20)

dimepenny_enable_warnings(dimepenny_core)

