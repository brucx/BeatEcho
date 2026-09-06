idf_component_register(
    SRCS "app_main.cpp" "board.cpp" "audio.cpp"
         "../../core/src/engine.cpp" "../../core/src/synth.cpp"
    INCLUDE_DIRS "." "../../core/include"
    REQUIRES esp_driver_gpio esp_driver_i2s esp_driver_rmt esp_timer esp_system esp_hw_support freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
