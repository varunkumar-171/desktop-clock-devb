/**
 * @file api_manager.h
 * @brief Public interface for the Weather and Quote API module.
 * * This header defines the data structures and task entry points for 
 * fetching meteorological data and quotes via REST APIs.
 */

#ifndef API_MANAGER_H
#define API_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Guardrail Constants --- */
#define MAX_QUOTE_LEN        256
#define MAX_WEATHER_STR_LEN  16
#define MAX_DESC_LEN         64

/**
 * @brief Structure to store parsed weather metrics as formatted strings.
 * * Storing these as strings simplifies direct injection into UI labels.
 */
typedef struct {
    char temperature[MAX_WEATHER_STR_LEN];
    char humidity[MAX_WEATHER_STR_LEN];
    char wind_speed[MAX_WEATHER_STR_LEN];
    char precip[MAX_WEATHER_STR_LEN];
    char description[MAX_DESC_LEN]; 
    char aqi[8];
} weather_data_t;

/* --- Public API Functions --- */

/**
 * @brief FreeRTOS task entry point for the API polling loop.
 * * Periodically fetches weather and quote data, parses it, and 
 * triggers UI updates via lv_async_call.
 * * @param pvParameters Task parameters (typically NULL).
 */
void api_update_task(void *pvParameters);

/**
 * @brief Core network function to fetch data from a given URL.
 * * @param url The REST endpoint to target (Weatherstack, ZenQuotes, etc).
 */
void fetch_api_data(const char *url);

/**
 * @brief Computes the US EPA Air Quality Index score from PM2.5 mass.
 * * @param pm25 Particulate matter 2.5 concentration.
 * @return int AQI score (0 - 500).
 */
int calculate_aqi_score(float pm25);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // API_MANAGER_H