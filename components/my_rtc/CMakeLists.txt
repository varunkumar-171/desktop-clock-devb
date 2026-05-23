SET(SOURCES my_rtc.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES lwip nvs_flash esp_netif ui)
