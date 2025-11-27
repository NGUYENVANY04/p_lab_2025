#ifndef RFID_READER_H
#define RFID_READER_H

#include <stdint.h>
#include <stddef.h>

// Định nghĩa chân cho RFID module
#define RFID_SS_PIN  5   // Chân SS (Slave Select)
#define RFID_RST_PIN 17  // Chân RST (Reset)

// Function prototypes cho đọc thẻ RFID
void rfid_init(void);
const char* rfid_read_card(void);

#endif // RFID_READER_H