#include "drv_rc522.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>

#define TAG "RC522"

// ---------- REGISTERS ----------
#define CommandReg 0x01
#define CommIEnReg 0x02
#define DivIEnReg 0x03
#define CommIrqReg 0x04
#define DivIrqReg 0x05
#define ErrorReg 0x06
#define Status1Reg 0x07
#define Status2Reg 0x08
#define FIFODataReg 0x09
#define FIFOLevelReg 0x0A
#define ControlReg 0x0C
#define BitFramingReg 0x0D
#define CollReg 0x0E

#define ModeReg 0x11
#define TxModeReg 0x12
#define RxModeReg 0x13
#define TxControlReg 0x14
#define TxASKReg 0x15
#define RxThresholdReg 0x18
#define DemodReg 0x19
#define ModWidthReg 0x24
#define RFCfgReg 0x26
#define GsNReg 0x27
#define CWGsPReg 0x28
#define ModGsPReg 0x29
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegH 0x2C
#define TReloadRegL 0x2D
#define VersionReg 0x37

// ---------- PCD COMMANDS ----------
#define PCD_Idle 0x00
#define PCD_CalcCRC 0x03
#define PCD_Transceive 0x0C
#define PCD_SoftReset 0x0F

// ---------- PICC COMMANDS ----------
#define PICC_REQIDL 0x26
#define PICC_WUPA 0x52
#define PICC_ANTICOLL_CL1 0x93

static spi_device_handle_t rc522;

// ===== SPI low-level =====
static esp_err_t rc522_write_reg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = {(uint8_t)((addr << 1) & 0x7E), val};
    spi_transaction_t t = {0};
    t.length = 16;
    t.tx_buffer = tx;
    return spi_device_polling_transmit(rc522, &t);
}
static esp_err_t rc522_read_reg(uint8_t addr, uint8_t *val)
{
    uint8_t tx[2] = {(uint8_t)(((addr << 1) & 0x7E) | 0x80), 0x00};
    uint8_t rx[2] = {0};
    spi_transaction_t t = {0};
    t.length = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t ret = spi_device_polling_transmit(rc522, &t);
    if (ret == ESP_OK)
        *val = rx[1];
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

// ===== Core utils =====
static void rc522_reset_hw(void)
{
    gpio_config_t io = {.pin_bit_mask = 1ULL << RC522_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    gpio_set_level(RC522_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RC522_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RC522_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}
static void rc522_soft_reset(void)
{
    rc522_write_reg(CommandReg, PCD_SoftReset);
    vTaskDelay(pdMS_TO_TICKS(50));
}
static void rc522_antenna_on(void)
{
    uint8_t v;
    rc522_read_reg(TxControlReg, &v);
    if ((v & 0x03) != 0x03)
        rc522_set_bitmask(TxControlReg, 0x03);
}
static void rc522_init_chip(void)
{
    rc522_soft_reset();

    rc522_write_reg(TModeReg, 0x8D);
    rc522_write_reg(TPrescalerReg, 0x3E);
    rc522_write_reg(TReloadRegL, 30);
    rc522_write_reg(TReloadRegH, 0);

    rc522_write_reg(TxModeReg, 0x00);
    rc522_write_reg(RxModeReg, 0x00);
    rc522_write_reg(ModeReg, 0x3D);

    rc522_write_reg(RFCfgReg, 0x7F);
    rc522_write_reg(TxASKReg, 0x40);
    rc522_write_reg(ModWidthReg, 0x26);
    rc522_write_reg(RxThresholdReg, 0x55);
    rc522_write_reg(DemodReg, 0x4D);

    rc522_antenna_on();
    vTaskDelay(pdMS_TO_TICKS(5));
}

// ===== Transceive =====
static esp_err_t rc522_transceive(uint8_t *sendData, uint8_t sendLen,
                                  uint8_t *backData, uint8_t *backLen,
                                  uint8_t validBits)
{
    rc522_write_reg(CommIEnReg, 0x77);
    rc522_clear_bitmask(CommIrqReg, 0x80);
    rc522_set_bitmask(FIFOLevelReg, 0x80);

    for (int i = 0; i < sendLen; i++)
        rc522_write_reg(FIFODataReg, sendData[i]);
    rc522_write_reg(BitFramingReg, validBits & 0x07);

    rc522_write_reg(CommandReg, PCD_Transceive);
    rc522_set_bitmask(BitFramingReg, 0x80);

    int timeout = 15000;
    uint8_t irq;
    do
    {
        rc522_read_reg(CommIrqReg, &irq);
    } while (--timeout && !(irq & 0x30));
    rc522_clear_bitmask(BitFramingReg, 0x80);

    if (timeout == 0)
        return ESP_ERR_TIMEOUT;

    uint8_t fifoLevel;
    rc522_read_reg(FIFOLevelReg, &fifoLevel);
    *backLen = fifoLevel;
    for (int j = 0; j < fifoLevel; j++)
        rc522_read_reg(FIFODataReg, &backData[j]);

    return ESP_OK;
}

// ===== PICC primitives =====
static bool picc_request(uint8_t reqMode, uint8_t *atqa)
{
    rc522_clear_bitmask(Status2Reg, 0x08);
    rc522_write_reg(BitFramingReg, 0x07);
    rc522_write_reg(CollReg, 0x00);

    uint8_t cmd = reqMode, back[16] = {0};
    uint8_t bl = 0;
    if (rc522_transceive(&cmd, 1, back, &bl, 7) != ESP_OK || bl != 2)
        return false;
    atqa[0] = back[0];
    atqa[1] = back[1];
    return true;
}
static bool picc_anticoll_cl1(uint8_t *uid4)
{
    rc522_write_reg(BitFramingReg, 0x00);
    rc522_clear_bitmask(CollReg, 0x80);

    uint8_t tx[2] = {PICC_ANTICOLL_CL1, 0x20};
    uint8_t rx[16] = {0};
    uint8_t rl = 0;
    if (rc522_transceive(tx, 2, rx, &rl, 0) != ESP_OK || rl < 5)
        return false;
    uid4[0] = rx[0];
    uid4[1] = rx[1];
    uid4[2] = rx[2];
    uid4[3] = rx[3];
    return true;
}

// ===== SPI init =====
static void rc522_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = RC522_MOSI,
        .miso_io_num = RC522_MISO,
        .sclk_io_num = RC522_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, 0));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 400 * 1000,
        .mode = 0,
        .spics_io_num = RC522_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(VSPI_HOST, &devcfg, &rc522));
}

// -------------------- Public API --------------------
void rc522_init(void)
{
    rc522_reset_hw();
    rc522_spi_init();
    rc522_init_chip();

    uint8_t ver = 0;
    rc522_read_reg(VersionReg, &ver);
    ESP_LOGI(TAG, "MFRC522 Version: 0x%02X (0x91/0x92 expected)", ver);
}

uint8_t rc522_get_version(void)
{
    uint8_t ver = 0;
    rc522_read_reg(VersionReg, &ver);
    return ver;
}

bool rc522_read_uid(uint8_t *uid_out)
{
    uint8_t atqa[2];
    if (!picc_request(PICC_REQIDL, atqa))
        return false;

    if (!picc_anticoll_cl1(uid_out))
        return false;

    ESP_LOGI(TAG, "UID: %02X %02X %02X %02X", uid_out[0], uid_out[1], uid_out[2], uid_out[3]);
    return true;
}

void rc522_loop_task(void)
{
    ESP_LOGI(TAG, "Place card near antenna...");
    while (1)
    {
        uint8_t uid[4];
        if (rc522_read_uid(uid))
        {
            ESP_LOGI(TAG, "Card detected UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
