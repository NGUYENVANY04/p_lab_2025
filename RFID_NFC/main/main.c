#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "rfid_reader.h"
#include "keypad.h"
#include "lcd_i2c.h"
#include "http_client.h"

static const char *TAG = "RFID_NFC";


typedef struct {
    int id;
    const char *name;
    int price;
} MenuItem;

static const MenuItem menu[10] = {
    {1,"Pho Bo", 35000},
    {2,"Com Tam", 30000},
    {3,"Banh Mi", 20000},
    {4,"Bun Cha", 40000},
    {5,"Hu Tieu", 35000},
    {6,"Mi Quang", 38000},
    {7,"Xoi Ga", 25000},
    {8,"Cha Gio", 15000},
    {9,"Goi Cuon", 18000},
    {10,"Ca Phe", 22000}
};
char* CreatePassword(){
    char password[5];
    lcd_clear();
    lcd_printf(1, 0, "NHAP MAT KHAU:");
    int index = 0;
    while (1)
    {
        char key;
        if (keypad_get_key(&key)) {
            if (key >= '0' && key <= '9' && index < 4) {
                password[index++] = key;
                lcd_printf(6+index - 1, 1, "*");
            } else if (key == '#' && index == 4) {
                password[4] = '\0';
                ESP_LOGI(TAG, "Password entered: %s", password);
                return strdup(password);
            } else if (key == '*') { 
                if (index > 0) {
                    index--;
                    lcd_printf(6+index, 1, " ");
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
}
// Hàm xác thực thẻ và mật khẩu, trả về card_id nếu thành công
char* VerifyCardAndPassword() {
    char* id_card;
    
    // Bước 1: Quét thẻ
    lcd_clear();
    lcd_printf(0, 0, "DUA THE LAI GAN");
    while (1) {
        id_card = rfid_read_card();
        if (id_card != NULL) {
            ESP_LOGI(TAG, "Card ID: %s", id_card);
            lcd_clear();
            lcd_printf(0, 0, "DANG KIEM TRA...");
            vTaskDelay(500 / portTICK_PERIOD_MS);
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Bước 2: Kiểm tra thẻ có trong DB không
    if (!http_verify_card(id_card)) {
        // Thẻ không hợp lệ
        lcd_clear();
        lcd_printf(0, 0, "THE KHONG HOP LE");
        lcd_printf(0, 1, "CHUA DANG KY!");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "✗ THẺ KHÔNG HỢP LỆ!");
        ESP_LOGE(TAG, "Card ID: %s", id_card);
        ESP_LOGE(TAG, "========================================");
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        return NULL;
    }
    
    // Bước 3: Thẻ hợp lệ, yêu cầu nhập mật khẩu
    lcd_clear();
    lcd_printf(0, 0, "THE HOP LE");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    char* password = CreatePassword();
    
    // Bước 4: Xác thực mật khẩu
    lcd_clear();
    lcd_printf(0, 0, "DANG XAC THUC...");
    
    if (http_verify_password(id_card, password)) {
        // Mật khẩu đúng
        lcd_clear();
        lcd_printf(0, 0, "XAC THUC THANH");
        lcd_printf(0, 1, "CONG!");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        free(password);
        
        // Trả về card_id để sử dụng cho thanh toán
        return strdup(id_card);
    } else {
        // Mật khẩu sai
        lcd_clear();
        lcd_printf(0, 0, "SAI MAT KHAU!");
        lcd_printf(0, 1, "THU LAI");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        free(password);
        return NULL;
    }
}

void CreateCard(){
    char* id_card;
    lcd_clear();
    lcd_printf(0, 0, "DUA THE LAI GAN");
    while (1)
    {
        id_card = rfid_read_card();
        if (id_card != NULL) {
            ESP_LOGI(TAG, "Card ID: %s", id_card);
            lcd_clear();
            lcd_printf(0, 0, "DA DOC THE");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            break;
        } else {
            ESP_LOGD(TAG, "No card detected");
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    char* password = CreatePassword();
    bool result = http_send_card_data(id_card, password);
    
    // Phải free password sau khi dùng xong
    free(password);
    
    if (result == true){ 
        ESP_LOGI(TAG, "Card data sent successfully");
        lcd_clear();
        lcd_printf(1, 0, "THEM THE THANH");
        lcd_printf(6, 1, "CONG");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    } else{
        // Thẻ đã tồn tại hoặc lỗi khác
        ESP_LOGW(TAG, "Failed to create card");
    }
}


int SelectProduct() {
    lcd_clear();
    lcd_printf(0, 0, "CHON SAN PHAM");
    lcd_printf(0, 1, "ID: ");
    
    char product_id[3] = {0};  
    int index = 0;
    
    while (1) {
        char key;
        if (keypad_get_key(&key)) {
            if (key >= '1' && key <= '9' && index < 2) {
                
                product_id[index++] = key;
                product_id[index] = '\0';
                lcd_printf(4 + index - 1, 1, "%c", key);
                
                if (index == 1) {
                    ESP_LOGI(TAG, "Press # to confirm or continue entering");
                }
            } else if (key == '0' && index == 1 && product_id[0] == '1') {
 
                product_id[index++] = key;
                product_id[index] = '\0';
                lcd_printf(4 + index - 1, 1, "%c", key);
            } else if (key == '#' && index > 0) {
                
                int selected_id = atoi(product_id);
                
                if (selected_id >= 1 && selected_id <= 10) {

                    const MenuItem *item = &menu[selected_id - 1];
                    lcd_clear();
                    lcd_printf(0, 0, "%s", item->name);
                    lcd_printf(0, 1, "Gia: %d VND", item->price);
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    
                    ESP_LOGI(TAG, "Selected product ID: %d - %s - %d VND", 
                             item->id, item->name, item->price);
                    return selected_id;
                } else {
                    lcd_clear();
                    lcd_printf(0, 0, "ID KHONG HOP LE");
                    lcd_printf(0, 1, "CHON SAN PHAM");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    
                    lcd_clear();
                    lcd_printf(0, 0, "CHON SAN PHAM");
                    lcd_printf(0, 1, "ID: ");
                    index = 0;
                    product_id[0] = '\0';
                }
            } else if (key == '*') {
                if (index > 0) {
                    index--;
                    product_id[index] = '\0';
                    lcd_printf(4 + index, 1, " ");
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting RFID NFC application...");

    // Khởi tạo RFID reader
    rfid_init();
    keypad_init();

    // Khởi tạo LCD
    lcd_init();
    lcd_clear();
    lcd_printf(0, 0, "Connecting WiFi");
    http_client_init();  // Kết nối WiFi
    lcd_clear();
    lcd_printf(0, 0, "WiFi Connected");

    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_clear();

    while (1) {
        char key;
        lcd_printf(0, 0, "1 MENU ORDER");
        lcd_printf(0, 1, "2 CREATE CARD");
        if (keypad_get_key(&key)) {
            if (key == '1') {
                // Bước 1: Chọn sản phẩm
                int product_id = SelectProduct();
                const MenuItem *item = &menu[product_id - 1];
                ESP_LOGI(TAG, "You selected product ID: %d", product_id);
                
                // Bước 2: Xác nhận order
                lcd_clear();
                lcd_printf(2, 0, "XAC NHAN MON");
                lcd_printf(2, 1, "1 YES   2 NO");
                
                while (1) {
                    char confirm_key;
                    if (keypad_get_key(&confirm_key)) {
                        if (confirm_key == '1') {
                           
                            char* card_id = VerifyCardAndPassword();
                            if (card_id != NULL) {
                               
                                lcd_clear();
                                int balance = http_check_balance(card_id);
                                
                                if (balance < 0) {
                                    lcd_clear();
                                    lcd_printf(0, 0, "LOI KET NOI");
                                    lcd_printf(0, 1, "SERVER");
                                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                                } else if (balance < item->price) {
                                   
                                    ESP_LOGE(TAG, "========================================");
                                    ESP_LOGE(TAG, "✗ SỐ DƯ KHÔNG ĐỦ!");
                                    ESP_LOGE(TAG, "Balance: %d VND", balance);
                                    ESP_LOGE(TAG, "Required: %d VND", item->price);
                                    ESP_LOGE(TAG, "========================================");
                                    
                                    lcd_clear();
                                    lcd_printf(0, 0, "SO DU KHONG DU");
                                    vTaskDelay(3000 / portTICK_PERIOD_MS);
                                } else {
                                    
                                    lcd_clear();
                                 
                                    if (http_process_payment(card_id, item->price)) {
                                        
                                        ESP_LOGI(TAG, "Payment completed successfully");
                                        vTaskDelay(2000 / portTICK_PERIOD_MS);
                                        
                                        lcd_clear();
                                        lcd_printf(3, 0, "THANH TOAN");
                                        lcd_printf(3, 1, "THANH CONG");
                                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                                    }
                                }
                                free(card_id);
                            }
                            break;
                        } else if (confirm_key == '2') {
                            lcd_clear();
                            lcd_printf(1, 0, "ORDER CANCELLED");
                            vTaskDelay(2000 / portTICK_PERIOD_MS);
                            break;
                        }
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                lcd_clear();
            } else if (key == '2') {
                CreateCard();
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                lcd_clear();
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    //  CreateCard();
}