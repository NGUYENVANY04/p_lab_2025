#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>
#include <stdbool.h>

// Định nghĩa chân GPIO cho bàn phím 3x4
// Hàng (Rows) - Output
#define KEYPAD_ROW1  12
#define KEYPAD_ROW2  13
#define KEYPAD_ROW3  14
#define KEYPAD_ROW4  15

// Cột (Columns) - Input với pull-up
#define KEYPAD_COL1  25
#define KEYPAD_COL2  26
#define KEYPAD_COL3  27

// Function prototypes
void keypad_init(void);
char keypad_scan(void);
bool keypad_get_key(char *key);

#endif // KEYPAD_H
