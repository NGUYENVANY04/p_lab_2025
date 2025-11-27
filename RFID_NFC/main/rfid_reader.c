#include "rfid_reader.h"
#include <esp_log.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "RFID_READER";

// SPI device handle
static spi_device_handle_t rc522_spi;

// ===== MFRC522 Registers =====
#define REG_COMMAND       0x01
#define REG_COM_IRQ       0x04
#define REG_ERROR         0x06
#define REG_STATUS2       0x08
#define REG_FIFO_DATA     0x09
#define REG_FIFO_LEVEL    0x0A
#define REG_BIT_FRAMING   0x0D
#define REG_COLL          0x0E
#define REG_MODE          0x11
#define REG_TX_CONTROL    0x14
#define REG_TX_ASK        0x15
#define REG_RF_CFG        0x26
#define REG_T_MODE        0x2A
#define REG_T_PRESCALER   0x2B
#define REG_T_RELOAD_H    0x2C
#define REG_T_RELOAD_L    0x2D
#define REG_VERSION       0x37

// ===== PCD Commands =====
#define CMD_IDLE          0x00
#define CMD_TRANSCEIVE    0x0C
#define CMD_SOFT_RESET    0x0F

// ===== PICC Commands =====
#define PICC_REQIDL       0x26
#define PICC_WUPA         0x52
#define PICC_ANTICOLL     0x93

// ========== SPI Low-Level Functions ==========
static esp_err_t rc522_write_reg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = {(addr << 1) & 0x7E, val};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx
    };
    return spi_device_polling_transmit(rc522_spi, &t);
}

static esp_err_t rc522_read_reg(uint8_t addr, uint8_t *val)
{
    uint8_t tx[2] = {((addr << 1) & 0x7E) | 0x80, 0x00};
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    esp_err_t ret = spi_device_polling_transmit(rc522_spi, &t);
    if (ret == ESP_OK) {
        *val = rx[1];
    }
    return ret;
}

static void rc522_set_bitmask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp;
    rc522_read_reg(reg, &tmp);
    rc522_write_reg(reg, tmp | mask);
}

static void rc522_clear_bitmask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp;
    rc522_read_reg(reg, &tmp);
    rc522_write_reg(reg, tmp & (~mask));
}

// ========== Hardware Reset ==========
static void rc522_reset_hw(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RFID_RST_PIN),
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&io);
    
    gpio_set_level(RFID_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(RFID_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(RFID_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));  // Giảm từ 50ms xuống 20ms
}

// ========== Soft Reset ==========
static void rc522_soft_reset(void)
{
    rc522_write_reg(REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(20));  // Giảm từ 50ms xuống 20ms
}

// ========== Antenna Control ==========
static void rc522_antenna_on(void)
{
    uint8_t val;
    rc522_read_reg(REG_TX_CONTROL, &val);
    if ((val & 0x03) != 0x03) {
        rc522_set_bitmask(REG_TX_CONTROL, 0x03);
    }
}

// ========== Chip Initialization ==========
static void rc522_init_chip(void)
{
    rc522_soft_reset();

    // Timer configuration
    rc522_write_reg(REG_T_MODE, 0x8D);
    rc522_write_reg(REG_T_PRESCALER, 0x3E);
    rc522_write_reg(REG_T_RELOAD_L, 30);
    rc522_write_reg(REG_T_RELOAD_H, 0);

    // TX/RX mode
    rc522_write_reg(0x12, 0x00);  // TxModeReg
    rc522_write_reg(0x13, 0x00);  // RxModeReg
    rc522_write_reg(REG_MODE, 0x3D);

    // RF configuration
    rc522_write_reg(REG_RF_CFG, 0x7F);  // Max gain
    rc522_write_reg(REG_TX_ASK, 0x40);
    rc522_write_reg(0x24, 0x26);  // ModWidthReg

    // Turn on antenna
    rc522_antenna_on();
    vTaskDelay(pdMS_TO_TICKS(2));  // Giảm từ 5ms xuống 2ms
}

// ========== Transceive Function ==========
static esp_err_t rc522_transceive(uint8_t *sendData, uint8_t sendLen,
                                   uint8_t *backData, uint8_t *backLen,
                                   uint8_t validBits)
{
    // Clear interrupt request bits
    rc522_clear_bitmask(REG_COM_IRQ, 0x80);
    rc522_set_bitmask(REG_FIFO_LEVEL, 0x80);  // Flush FIFO

    // Write data to FIFO
    for (int i = 0; i < sendLen; i++) {
        rc522_write_reg(REG_FIFO_DATA, sendData[i]);
    }
    
    rc522_write_reg(REG_BIT_FRAMING, validBits & 0x07);

    // Start transmission
    rc522_write_reg(REG_COMMAND, CMD_TRANSCEIVE);
    rc522_set_bitmask(REG_BIT_FRAMING, 0x80);

    // Wait for completion (giảm timeout để phản hồi nhanh hơn)
    int timeout = 5000;  // Giảm từ 15000
    uint8_t irq;
    do {
        rc522_read_reg(REG_COM_IRQ, &irq);
    } while (--timeout && !(irq & 0x30));
    
    rc522_clear_bitmask(REG_BIT_FRAMING, 0x80);

    if (timeout == 0) {
        ESP_LOGD(TAG, "Transceive timeout");
        return ESP_ERR_TIMEOUT;
    }

    // Read received data
    uint8_t fifoLevel;
    rc522_read_reg(REG_FIFO_LEVEL, &fifoLevel);
    *backLen = fifoLevel;
    for (int j = 0; j < fifoLevel && j < 16; j++) {
        rc522_read_reg(REG_FIFO_DATA, &backData[j]);
    }

    return ESP_OK;
}

// ========== PICC Request ==========
static bool picc_request(uint8_t reqMode, uint8_t *atqa)
{
    rc522_clear_bitmask(REG_STATUS2, 0x08);
    rc522_write_reg(REG_BIT_FRAMING, 0x07);
    rc522_write_reg(REG_COLL, 0x00);

    uint8_t cmd = reqMode;
    uint8_t back[16] = {0};
    uint8_t backLen = 0;
    
    if (rc522_transceive(&cmd, 1, back, &backLen, 7) != ESP_OK || backLen != 2) {
        return false;
    }
    
    atqa[0] = back[0];
    atqa[1] = back[1];
    return true;
}

// ========== Anti-collision ==========
static bool picc_anticoll(uint8_t *uid4)
{
    rc522_write_reg(REG_BIT_FRAMING, 0x00);
    rc522_clear_bitmask(REG_COLL, 0x80);

    uint8_t tx[2] = {PICC_ANTICOLL, 0x20};
    uint8_t rx[16] = {0};
    uint8_t rxLen = 0;
    
    if (rc522_transceive(tx, 2, rx, &rxLen, 0) != ESP_OK || rxLen < 5) {
        return false;
    }
    
    uid4[0] = rx[0];
    uid4[1] = rx[1];
    uid4[2] = rx[2];
    uid4[3] = rx[3];
    return true;
}

// ========== SPI Initialization ==========
static void rc522_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = 23,
        .miso_io_num = 19,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, 0));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4000000,  // 4MHz (tăng từ 1MHz)
        .mode = 0,
        .spics_io_num = RFID_SS_PIN,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &rc522_spi));
}

// ========== Public API ==========

void rfid_init(void)
{
    ESP_LOGI(TAG, "Initializing MFRC522...");
    
    rc522_reset_hw();
    rc522_spi_init();
    rc522_init_chip();

    uint8_t ver = 0;
    rc522_read_reg(REG_VERSION, &ver);
    ESP_LOGI(TAG, "MFRC522 Version: 0x%02X (expected 0x91 or 0x92)", ver);
    
    if (ver == 0x91 || ver == 0x92) {
        ESP_LOGI(TAG, "MFRC522 initialized successfully");
    } else if (ver == 0x00 || ver == 0xFF) {
        ESP_LOGE(TAG, "SPI communication failed! Check wiring:");
        ESP_LOGE(TAG, "  MISO=19, MOSI=23, SCK=18, SS=5, RST=17");
    } else {
        ESP_LOGW(TAG, "Unknown version, but may still work");
    }
}

const char* rfid_read_card(void)
{
    static char card_id[20];  // Buffer tĩnh để lưu chuỗi ID
    uint8_t uid[4];
    uint8_t atqa[2];
    
    // Try to detect card
    if (!picc_request(PICC_REQIDL, atqa)) {
        return NULL;  // No card
    }
    
    // Read UID
    if (!picc_anticoll(uid)) {
        ESP_LOGW(TAG, "Anti-collision failed");
        return NULL;
    }
    
    // Tạo chuỗi ID từ 4 bytes UID (định dạng hex)
    snprintf(card_id, sizeof(card_id), "%02X%02X%02X%02X", 
             uid[0], uid[1], uid[2], uid[3]);
    
    return card_id;
}
