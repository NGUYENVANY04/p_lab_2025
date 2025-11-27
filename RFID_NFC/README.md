# RFID_NFC ESP-IDF Project

This is a basic ESP-IDF project for RFID and NFC development on ESP32.

## Prerequisites

- ESP-IDF installed and configured. Follow the [official installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).

## Building and Flashing

1. Open the project in VS Code with ESP-IDF extension installed (optional but recommended).

2. Set up the ESP-IDF environment by running `idf.py set-target esp32` (or your target).

3. Configure the project: `idf.py menuconfig` (optional for basic setup).

4. Build the project: `idf.py build`

5. Flash to ESP32: `idf.py flash`

6. Monitor: `idf.py monitor`

## Notes

- This is a starter project. Add RFID/NFC libraries and code as needed.
- Ensure your ESP32 board is connected and the correct COM port is selected.