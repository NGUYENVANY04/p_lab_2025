#ifndef DRV_RC522_H
#define DRV_RC522_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RC522_SCK 18
#define RC522_MOSI 23
#define RC522_MISO 19
#define RC522_CS 5
#define RC522_RST 27

    void rc522_init(void);
    bool rc522_read_uid(uint8_t *uid_out);
    uint8_t rc522_get_version(void);
    void rc522_loop_task(void);

#ifdef __cplusplus
}
#endif

#endif // DRV_RC522_H
