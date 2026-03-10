#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bt_sender.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "UART_SIMPLE";

#define UART_PORT_NUM      UART_NUM_0
#define BUF_SIZE           1024
#define TXD_PIN            UART_PIN_NO_CHANGE
#define RXD_PIN            UART_PIN_NO_CHANGE

static char packet_buf[128]; // Buffer to reconstruct UART string
static int packet_idx = 0;   // Current index in packet_buf

static QueueHandle_t uart0_queue; // FreeRTOS Queue for UART events

// Delayed task to trigger the Bluetooth CHECK scan asynchronously
void check_sequence_task(void *pvParameter) {
    // Wait for 600ms before starting scan to let the broadcasted CHECK command reach receivers
    vTaskDelay(pdMS_TO_TICKS(600)); 
    bt_sender_start_check(2000); // Scan for 2 seconds
    vTaskDelete(NULL);
}

// Parse individual characters received from UART
void process_byte(uint8_t c, int64_t t_wake, int64_t t_read_done) {
    if (c == '\n') {
        // End of line reached, parse the CSV format string
        packet_buf[packet_idx] = '\0';       
        int cmd_in = 0;
        unsigned long delay_us = 0;
        unsigned long prep_led_us = 0;
        unsigned long long target_mask = 0;
        int in_data[3];

        int args = sscanf(packet_buf, "%d,%lu,%lu,%llx,%d,%d,%d", 
                          &cmd_in, &delay_us, &prep_led_us, &target_mask, 
                          &in_data[0], &in_data[1], &in_data[2]);

        if (args == 7) {
            // Reply ACK to PC script immediately
            char ack_msg[64];
            snprintf(ack_msg, sizeof(ack_msg), "ACK:OK\n");
            uart_write_bytes(UART_PORT_NUM, ack_msg, strlen(ack_msg));
            
            // Build and queue the Bluetooth broadcast task
            bt_sender_config_t burst_cfg = {
                .cmd_type = (uint8_t)cmd_in,
                .delay_ms = delay_us,
                .prep_led_ms = prep_led_us,
                .target_mask = (uint64_t)target_mask,
                .data[0]=(uint8_t)in_data[0],
                .data[1]=(uint8_t)in_data[1],
                .data[2]=(uint8_t)in_data[2]
            };
            bt_sender_add_task(&burst_cfg);
            
            // If the command is CANCEL (0x06), remove the targeted task locally as well
            if ((cmd_in & 0x0F) == 0x06) {
                int target_cmd_id = in_data[0];
                bt_sender_remove_task(target_cmd_id);
            }
            // If the command is CHECK (0x07), start the asynchronous scan sequence
            if ((cmd_in & 0x0F) == 0x07) {
                xTaskCreate(check_sequence_task, "chk_seq", 4096, NULL, 10, NULL);
            }
        } else {
            // Invalid format
            uart_write_bytes(UART_PORT_NUM, "NAK:ParseError\n", 15);
        }
        packet_idx = 0; // Reset buffer for next packet
    } 
    else if (c == '\r') {
        // Ignore carriage return
    }
    else if (packet_idx < sizeof(packet_buf) - 1) {
        // Append character to buffer
        packet_buf[packet_idx++] = (char)c;
    } 
    else {
        // Buffer overflow protection
        packet_idx = 0;
        uart_write_bytes(UART_PORT_NUM, "NAK:Overflow\n", 13);
    }
}

// Main RTOS Task to handle UART Hardware Events (Interrupts)
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE);

    for(;;) {
        // Wait for an event from the UART driver
        if(xQueueReceive(uart0_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            int64_t t_wake = esp_timer_get_time();
            
            switch(event.type) {
                case UART_DATA: // Normal data received
                    uart_read_bytes(UART_PORT_NUM, dtmp, event.size, 0);
                    int64_t t_read_done = esp_timer_get_time();
                    
                    // Process byte by byte
                    for (int i = 0; i < event.size; i++) {
                        process_byte(dtmp[i], t_wake, t_read_done);
                    }
                    break;
                case UART_FIFO_OVF: // Hardware FIFO overflow
                    ESP_LOGW(TAG, "hw fifo overflow");
                    uart_flush_input(UART_PORT_NUM);
                    xQueueReset(uart0_queue);
                    break;
                case UART_BUFFER_FULL: // Software Ring Buffer full
                    ESP_LOGW(TAG, "ring buffer full");
                    uart_flush_input(UART_PORT_NUM);
                    xQueueReset(uart0_queue);
                    break;
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

// Application Entry Point
void app_main(void)
{
    // 1. Initialize Bluetooth Sender Component
    if (bt_sender_init() != ESP_OK) return;
    
    // 2. Configure UART Parameters
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 3. Install UART Driver and set pins
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0));

    // 4. Start UART Event Handling Task
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);

    ESP_LOGI(TAG, "UART Listening...");

    // Keep the main task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}