idf_component_register(
    SRCS "audio.c" "led.c" "M5PaperColor.c" "board.c" "buttons.c" "calendar.c" "data.c" "render.c"
         "storage.c" "network.c" "compression.c" "setup.c" "sdcard.c" "fonts.c" "anniversary.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_rmt esp_timer esp_wifi
             esp_netif esp_event esp_http_client esp_http_server mbedtls nvs_flash
             cjson esp_hw_support esp_rom lwip fatfs sdmmc esp_driver_sdspi vfs
    EMBED_FILES "generated/font14.bin" "generated/font18.bin" "generated/font24.bin"
                "generated/font48.bin" "generated/font64.bin" "generated/font88.bin" "generated/lunar.bin" "generated/terms.bin"
                "generated/font18m.bin" "generated/font24m.bin"
                "generated/icons20.bin" "generated/icons30.bin" "generated/icons80.bin"
                "web/e6.wasm"
    EMBED_TXTFILES "generated/holidays.json" "web/index.html" "web/app.js"
)
