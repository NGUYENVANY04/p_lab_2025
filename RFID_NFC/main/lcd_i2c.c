#include "lcd_i2c.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <rom/ets_sys.h>

static const char *TAG = "LCD_I2C";

// I2C port
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  50000  // Giảm từ 100kHz xuống 50kHz để ổn định hơn

// LCD commands
#define LCD_CMD_CLEAR       0x01
#define LCD_CMD_HOME        0x02
#define LCD_CMD_ENTRY_MODE  0x04
#define LCD_CMD_DISPLAY_CTL 0x08
#define LCD_CMD_FUNCTION    0x20
#define LCD_CMD_SET_DDRAM   0x80

// Entry mode flags
#define LCD_ENTRY_LEFT      0x02
#define LCD_ENTRY_SHIFT_DEC 0x00

// Display control flags
#define LCD_DISPLAY_ON      0x04
#define LCD_CURSOR_OFF      0x00
#define LCD_BLINK_OFF       0x00

// Function set flags
#define LCD_4BIT_MODE       0x00
#define LCD_2LINE           0x08
#define LCD_5x8DOTS         0x00

// Backlight
#define LCD_BACKLIGHT       0x08
#define LCD_NOBACKLIGHT     0x00

// Enable bit
#define En 0x04  // Enable bit
#define Rw 0x02  // Read/Write bit
#define Rs 0x01  // Register select bit

static uint8_t _backlight = LCD_BACKLIGHT;

// Gửi byte qua I2C
static esp_err_t lcd_write_byte(uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Gửi 4 bits cao
static void lcd_write_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data = (nibble & 0xF0) | mode | _backlight;
    
    // Gửi data với Enable LOW trước
    lcd_write_byte(data & ~En);
    ets_delay_us(1);
    
    // Gửi data với Enable HIGH
    lcd_write_byte(data | En);
    ets_delay_us(1);  // Enable pulse width >= 450ns
    
    // Gửi data với Enable LOW
    lcd_write_byte(data & ~En);
    ets_delay_us(100);  // Enable cycle time >= 500ns, command execution time
}

// Gửi byte (8 bits) dưới dạng 2 nibbles
static void lcd_send_byte(uint8_t data, uint8_t mode)
{
    lcd_write_nibble(data & 0xF0, mode);        // High nibble
    lcd_write_nibble((data << 4) & 0xF0, mode); // Low nibble
}

// Gửi lệnh
static void lcd_send_cmd(uint8_t cmd)
{
    lcd_send_byte(cmd, 0);
    ets_delay_us(50);  // Hầu hết lệnh cần ~37us
}

// Gửi dữ liệu (ký tự)
static void lcd_send_data(uint8_t data)
{
    lcd_send_byte(data, Rs);
    ets_delay_us(50);  // Ghi ký tự cần ~37us
}

// Khởi tạo I2C
static void lcd_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LCD_SDA_PIN,
        .scl_io_num = LCD_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
    
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", LCD_SDA_PIN, LCD_SCL_PIN);
}

// Scan địa chỉ I2C
void lcd_scan_i2c(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address: 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "I2C scan complete");
}

// Khởi tạo LCD
void lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD %dx%d at address 0x%02X...", LCD_COLS, LCD_ROWS, LCD_I2C_ADDR);
    
    lcd_i2c_init();
    
    // Chờ LCD khởi động (>40ms sau power-on)
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Bật backlight
    lcd_write_byte(_backlight);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Khởi tạo theo chuẩn HD44780 (4-bit interface)
    // Gửi 0x03 ba lần để reset
    lcd_write_nibble(0x30, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    lcd_write_nibble(0x30, 0);
    ets_delay_us(200);
    
    lcd_write_nibble(0x30, 0);
    ets_delay_us(200);
    
    // Chuyển sang 4-bit mode
    lcd_write_nibble(0x20, 0);
    ets_delay_us(200);
    
    // Function set: 4-bit, 2 lines, 5x8 dots (0x28)
    lcd_send_cmd(0x28);
    ets_delay_us(50);
    
    // Display control: display off (0x08)
    lcd_send_cmd(0x08);
    ets_delay_us(50);
    
    // Clear display (0x01) - cần 1.52ms
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    
    // Entry mode set: increment, no shift (0x06)
    lcd_send_cmd(0x06);
    ets_delay_us(50);
    
    // Display control: display on, cursor off, blink off (0x0C)
    lcd_send_cmd(0x0C);
    ets_delay_us(50);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "LCD initialized successfully");
}

// Xóa màn hình
void lcd_clear(void)
{
    lcd_send_cmd(LCD_CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));
}

// Về vị trí home
void lcd_home(void)
{
    lcd_send_cmd(LCD_CMD_HOME);
    vTaskDelay(pdMS_TO_TICKS(2));
}

// Đặt con trỏ
void lcd_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (row >= LCD_ROWS) {
        row = LCD_ROWS - 1;
    }
    if (col >= LCD_COLS) {
        col = LCD_COLS - 1;
    }
    lcd_send_cmd(LCD_CMD_SET_DDRAM | (col + row_offsets[row]));
}

// In chuỗi
void lcd_print(const char *str)
{
    while (*str) {
        lcd_send_data(*str++);
    }
}

// In chuỗi với format (printf-style) tại vị trí xác định
void lcd_printf(uint8_t col, uint8_t row, const char *format, ...)
{
    char buffer[LCD_COLS + 1];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    lcd_set_cursor(col, row);
    lcd_print(buffer);
}

// Bật/tắt backlight
void lcd_backlight(bool on)
{
    _backlight = on ? LCD_BACKLIGHT : LCD_NOBACKLIGHT;
    lcd_write_byte(_backlight);
}

// Bật/tắt màn hình
void lcd_display(bool on)
{
    if (on) {
        lcd_send_cmd(LCD_CMD_DISPLAY_CTL | LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF);
    } else {
        lcd_send_cmd(LCD_CMD_DISPLAY_CTL);
    }
}
