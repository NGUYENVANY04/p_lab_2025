#include "drv_rc522.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

void app_main(void)
{
    rc522_init();
    rc522_loop_task();
}
