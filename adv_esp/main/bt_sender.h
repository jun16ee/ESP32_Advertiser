// bt_sender.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize BT Sender API
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t bt_sender_init(void);

/**
 * @brief Execute a burst of advertising commands
 * @param cmd_type Command type (e.g., 0xA0)
 * @param target_delay_ms Target delay in milliseconds for command execution (minimum 1s)
 * @param prep_led_ms Preparation LED time in milliseconds
 * @param target_player_mask Bitmask representing target player IDs
 * @return int Actual number of commands executed
 */
typedef struct {
    uint8_t  cmd_type;
    uint32_t delay_ms;
    uint32_t prep_led_ms;
    uint64_t target_mask;
    uint8_t data[3];
} bt_sender_config_t;
esp_err_t bt_sender_init(void);
int bt_sender_add_task(const bt_sender_config_t *config);
void bt_sender_start_check(uint32_t duration_ms);
void bt_sender_remove_task(int slot_idx);