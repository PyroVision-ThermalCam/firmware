FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)

idf_component_register(SRCS ${app_sources}
                       REQUIRES esp_http_server esp_timer esp_wifi json nvs_flash driver
)

target_add_binary_data(${COMPONENT_TARGET} "../LICENSE" TEXT)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    PYROVISION_VERSION_MAJOR=1
    PYROVISION_VERSION_MINOR=0
    PYROVISION_VERSION_BUILD=0
)
