idf_component_register(
    SRCS "codex_usage.cpp" "codex_usage_parse.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server esp_hw_support freertos json
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
