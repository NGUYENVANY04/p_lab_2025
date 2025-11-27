#include "keypad.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG = "KEYPAD";

// Ma trận phím 4x3
static const char keypad_matrix[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

// Mảng chân hàng và cột
static const int row_pins[4] = {KEYPAD_ROW1, KEYPAD_ROW2, KEYPAD_ROW3, KEYPAD_ROW4};
static const int col_pins[3] = {KEYPAD_COL1, KEYPAD_COL2, KEYPAD_COL3};

// Khởi tạo bàn phím
void keypad_init(void)
{
    ESP_LOGI(TAG, "Initializing 4x3 keypad...");
    
    // Cấu hình các chân hàng là OUTPUT, mặc định HIGH
    for (int i = 0; i < 4; i++) {
        gpio_config_t row_cfg = {
            .pin_bit_mask = (1ULL << row_pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&row_cfg);
        gpio_set_level(row_pins[i], 1);  // Set HIGH
    }
    
    // Cấu hình các chân cột là INPUT với pull-up
    for (int i = 0; i < 3; i++) {
        gpio_config_t col_cfg = {
            .pin_bit_mask = (1ULL << col_pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&col_cfg);
    }
    
    ESP_LOGI(TAG, "Keypad initialized");
    ESP_LOGI(TAG, "  Rows: %d, %d, %d, %d", row_pins[0], row_pins[1], row_pins[2], row_pins[3]);
    ESP_LOGI(TAG, "  Cols: %d, %d, %d", col_pins[0], col_pins[1], col_pins[2]);
}

// Quét bàn phím và trả về ký tự đã nhấn (hoặc 0 nếu không có phím nào)
char keypad_scan(void)
{
    for (int row = 0; row < 4; row++) {
        // Đặt hàng hiện tại xuống LOW
        gpio_set_level(row_pins[row], 0);
        
        // Delay ngắn để ổn định
        vTaskDelay(pdMS_TO_TICKS(1));
        
        // Quét các cột
        for (int col = 0; col < 3; col++) {
            if (gpio_get_level(col_pins[col]) == 0) {
                // Phím được nhấn (cột LOW)
                char key = keypad_matrix[row][col];
                
                // Chờ phím được thả (debounce)
                while (gpio_get_level(col_pins[col]) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                
                // Đặt lại hàng về HIGH
                gpio_set_level(row_pins[row], 1);
                
                ESP_LOGI(TAG, "Key pressed: '%c'", key);
                return key;
            }
        }
        
        // Đặt lại hàng về HIGH
        gpio_set_level(row_pins[row], 1);
    }
    
    return 0;  // Không có phím nào được nhấn
}

// Lấy phím được nhấn (blocking hoặc non-blocking)
bool keypad_get_key(char *key)
{
    char pressed = keypad_scan();
    if (pressed != 0) {
        *key = pressed;
        return true;
    }
    return false;
}
