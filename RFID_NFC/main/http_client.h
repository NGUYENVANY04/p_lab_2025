#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// Cấu hình WiFi
#define WIFI_SSID "quochoc1"
#define WIFI_PASSWORD "22222222"

// Cấu hình Server - ĐỔI IP MÁY TÍNH CỦA BẠN Ở ĐÂY
#define SERVER_URL "http://172.20.10.2:8000"

// Khởi tạo WiFi và HTTP client
void http_client_init(void);

// Gửi dữ liệu thẻ RFID lên server
bool http_send_card_data(const char *card_id, const char *password);

// Gửi dữ liệu order lên server
bool http_send_order(const char *card_id, int item_id, int quantity, int total_price);

// Kiểm tra thẻ có tồn tại trong DB không
bool http_verify_card(const char *card_id);

// Xác thực thẻ và mật khẩu
bool http_verify_password(const char *card_id, const char *password);

// Kiểm tra số dư tài khoản
int http_check_balance(const char *card_id);

// Xử lý thanh toán (trừ tiền)
bool http_process_payment(const char *card_id, int amount);

// Kiểm tra kết nối WiFi
bool http_is_connected(void);

#endif // HTTP_CLIENT_H
