SET(SOURCES api.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES ui esp_http_client esp_netif wifi cjson mbedtls)
