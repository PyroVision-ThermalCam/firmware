idf_component_register(
    SRCS "visa_server.cpp" "visa_commands.cpp"
    INCLUDE_DIRS "."
    REQUIRES lwip freertos esp_timer
)
