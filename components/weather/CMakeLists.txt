idf_component_register(
    SRCS "weather.cpp" "weather_parse.cpp" "regions.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_client mbedtls freertos json
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
