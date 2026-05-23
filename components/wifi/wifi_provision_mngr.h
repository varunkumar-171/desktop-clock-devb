#ifndef WIFI_PROVISIONING_MGR_H
#define WIFI_PROVISIONING_MGR_H

#include <freertos/event_groups.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CONNECTED_BIT      BIT0

extern EventGroupHandle_t wifi_event_group;

/**
 * @brief Initializes NVS, network stack, and starts the Wi-Fi provisioning
 * process over SoftAP. This function blocks until Wi-Fi is connected.
 */
void initialize_wifi_provisioning(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_PROVISIONING_MGR_H