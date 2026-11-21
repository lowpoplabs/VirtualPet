idf_component_register(
    SRCS "main.c" "board_bsp.c" "axp2101.c" "pcf85063.c" "sensing.c" "foodlog.c" "qmi8658.c" "crashlog.c"
    INCLUDE_DIRS "."
)
# crashlog.c notes the cause of a crash before the usual handler runs.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
