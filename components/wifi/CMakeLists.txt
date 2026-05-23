SET(SOURCES wifi_provision_mngr.c)

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event nvs_flash wifi_provisioning qrcode ui)
