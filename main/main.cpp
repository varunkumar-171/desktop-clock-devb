/**
 * @file main.cpp
 * @brief Entry point for the Desktop Clock application.
 */

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Display & UI
#include "esp_display_panel.hpp"
#include "lvgl.h"
#include "lvgl_v8_port.h"
#include "ui.h"

// Application Modules
#include "api.h"
#include "my_rtc.h"
#include "wifi_provision_mngr.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static const char *TAG = "APP_MAIN";

/* --- Configuration --- */
#define UI_TASK_STACK_SIZE    (8 * 1024)
#define API_TASK_STACK_SIZE   (8 * 1024)
#define UI_REFRESH_PERIOD_MS  20

/* --- Function Prototypes --- */
static esp_err_t initialize_hardware_and_lvgl(Board **out_board);
static void clock_runtime_task(void *pvParameters);

/**
 * @brief Application Entry Point
 */
extern "C" void app_main()
{
    Board *board = nullptr;

    ESP_ERROR_CHECK(initialize_hardware_and_lvgl(&board));

    //starts ONLY after WiFi is connected via an event handler
    xTaskCreate(clock_runtime_task, "clock_task", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(api_update_task, "api_task", API_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    ESP_LOGI(TAG, "System initialization complete.");
    lv_disp_load_scr(ui_HomeScreen);
}

/**
 * @brief Initializes the physical board and the LVGL porting layer.
 */
static esp_err_t initialize_hardware_and_lvgl(Board **out_board)
{
    auto board = new Board();
    if (board == nullptr) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_FALSE(board->init(), ESP_FAIL, TAG, "Board init failed");
    ESP_RETURN_ON_FALSE(board->begin(), ESP_FAIL, TAG, "Board begin failed");

    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        ESP_LOGE(TAG, "LVGL port init failed");
        return ESP_FAIL;
    }

    *out_board = board;

    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    initialize_wifi_provisioning(); 
    initialize_sntp();
    set_timezone("PST8PDT");

    return ESP_OK;
}

/**
 * @brief Task responsible for local timekeeping and UI clock updates.
 */
static void clock_runtime_task(void *pvParameters)
{
    while (1) {        
        // Sync the actual UI labels
        lvgl_port_lock(-1);
        update_clock_hands();
        lvgl_port_unlock();

        vTaskDelay(pdMS_TO_TICKS(500)); // Update clock every half second
    }
}