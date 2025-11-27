#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <stdint.h>
#include <stdbool.h>

// Địa chỉ I2C của LCD
#define LCD_I2C_ADDR    0x27  // Địa chỉ đã xác nhận

// Cấu hình chân I2C
#define LCD_SDA_PIN     21
#define LCD_SCL_PIN     22

// Kích thước màn hình
#define LCD_COLS        16
#define LCD_ROWS        2

// Function prototypes
void lcd_init(void);
void lcd_clear(void);
void lcd_home(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_print(const char *str);
void lcd_printf(uint8_t col, uint8_t row, const char *format, ...);
void lcd_backlight(bool on);
void lcd_display(bool on);
void lcd_scan_i2c(void);  // Scan để tìm địa chỉ LCD

#endif // LCD_I2C_H
