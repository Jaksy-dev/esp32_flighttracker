#ifndef MEMORY_OPTIMIZATIONS_H
#define MEMORY_OPTIMIZATIONS_H

// This should all be done in sdkconfig.h

// https://docs.espressif.com/projects/esp-matter/en/latest/esp32/optimizations.html

#define CONFIG_ENABLE_CHIP_SHELL false

#define CONFIG_BT_ENABLED false

// what is this?
#define CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT 2
#define  CONFIG_ESP_MATTER_MAX_DEVICE_TYPE_COUNT 2

#define  CONFIG_EVENT_LOGGING_CRIT_BUFFER_SIZE 256
#define CONFIG_EVENT_LOGGING_INFO_BUFFER_SIZE 256
#define CONFIG_EVENT_LOGGING_DEBUG_BUFFER_SIZE 256
#define CONFIG_MAX_EVENT_QUEUE_SIZE 20
#define CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE 16
#define CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE 2048

// TODO: Move more things to flash?

#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 3072
#define CONFIG_ESP_TIMER_TASK_STACK_SIZE 2048
#define CONFIG_CHIP_TASK_STACK_SIZE 6144

// https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/performance/ram-usage.html

// Reduce WiFi performance to gain memory
#define CONFIG_ESP_WIFI_IRAM_OPT false

#endif