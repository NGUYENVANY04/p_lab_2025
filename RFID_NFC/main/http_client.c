#include "http_client.h"
#include "lcd_i2c.h"
#include <string.h>
#include <sys/param.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#define MAX_HTTP_OUTPUT_BUFFER 2048
#define WIFI_MAXIMUM_RETRY 5

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "HTTP_CLIENT";

// Event group for WiFi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static bool wifi_connected = false;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Starting WiFi connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to the AP (attempt %d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to the AP after %d attempts", WIFI_MAXIMUM_RETRY);
        }
        wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Khởi tạo WiFi
static bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // Đợi kết nối
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "WiFi CONNECTED SUCCESSFULLY!");
        ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
        ESP_LOGI(TAG, "========================================");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "WiFi CONNECTION FAILED!");
        ESP_LOGE(TAG, "SSID: %s", WIFI_SSID);
        ESP_LOGE(TAG, "Please check your WiFi credentials");
        ESP_LOGE(TAG, "========================================");
        return false;
    } else {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "UNEXPECTED WiFi EVENT!");
        ESP_LOGE(TAG, "========================================");
        return false;
    }
}

// HTTP event handler
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;
    static int output_len;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (output_len == 0 && evt->user_data) {
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            if (!esp_http_client_is_chunked_response(evt->client)) {
                int copy_len = 0;
                if (evt->user_data) {
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

// Khởi tạo HTTP client
void http_client_init(void)
{
    ESP_LOGI(TAG, "Initializing HTTP client...");
    
    // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    
    // Vòng lặp kết nối WiFi cho đến khi thành công
    int retry_count = 0;
    const int max_retries = 10;
    
    while (!wifi_connected && retry_count < max_retries) {
        retry_count++;
        ESP_LOGI(TAG, "WiFi connection attempt %d/%d", retry_count, max_retries);
        
        wifi_connected = wifi_init_sta();
        
        if (!wifi_connected) {
            ESP_LOGW(TAG, "Retrying WiFi connection in 5 seconds...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            
            // Reset các biến để thử lại
            s_retry_num = 0;
            esp_wifi_stop();
            esp_wifi_deinit();
        }
    }
    
    if (wifi_connected) {
        ESP_LOGI(TAG, "HTTP client initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize HTTP client - WiFi not connected");
    }
}

// Gửi dữ liệu thẻ RFID lên server
bool http_send_card_data(const char *card_id, const char *password)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    bool success = false;

    // Tạo JSON payload
    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "{\"id_card\":\"%s\",\"password\":\"%s\"}", 
             card_id, password);

    ESP_LOGI(TAG, "Sending card data: %s", post_data);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = "http://172.20.10.2:8000/create_account",
        .timeout_ms = 30000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // POST Request
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
        } else {
            int content_length = esp_http_client_fetch_headers(client);
            if (content_length < 0) {
                ESP_LOGE(TAG, "HTTP client fetch headers failed");
            } else {
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {
                    int status_code = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                            status_code, content_length);
                    
                    ESP_LOGI(TAG, "Response: %s", output_buffer);
                    
                    if (status_code == 200) {
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "✓ CARD DATA SENT SUCCESSFULLY!");
                        ESP_LOGI(TAG, "========================================");
                        success = true;
                    } else if (status_code == 400) {
                        // Kiểm tra response có chứa thông báo thẻ đã tồn tại
                        if (strstr(output_buffer, "already exists") != NULL || 
                            strstr(output_buffer, "đã tồn tại") != NULL ||
                            strstr(output_buffer, "duplicate") != NULL) {
                            ESP_LOGE(TAG, "========================================");
                            ESP_LOGE(TAG, "✗ LỖI: THẺ ĐÃ TỒN TẠI TRONG HỆ THỐNG!");
                            ESP_LOGE(TAG, "Card ID: %s", card_id);
                            ESP_LOGE(TAG, "========================================");
                        } else {
                            ESP_LOGW(TAG, "Bad request: %s", output_buffer);
                        }
                    } else if (status_code == 409) {
                        // HTTP 409 Conflict - thường dùng cho duplicate
                        lcd_clear();
                        lcd_printf(1, 0, "THE DA TON TAI");
                        ESP_LOGE(TAG, "========================================");
                        ESP_LOGE(TAG, "✗ LỖI: THẺ ĐÃ TỒN TẠI TRONG HỆ THỐNG!");
                        ESP_LOGE(TAG, "Card ID: %s", card_id);
                        ESP_LOGE(TAG, "Response: %s", output_buffer);
                        ESP_LOGE(TAG, "========================================");
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        return false;
                    } else {
                        ESP_LOGW(TAG, "Server returned status code: %d", status_code);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to read response");
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(100));  // Delay ngắn để WiFi ổn định
    return success;
}

// Gửi dữ liệu order lên server
bool http_send_order(const char *card_id, int item_id, int quantity, int total_price)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    bool success = false;

    // Tạo JSON payload
    char post_data[512];
    snprintf(post_data, sizeof(post_data), 
             "{\"id_card\":\"%s\",\"item_id\":%d,\"quantity\":%d,\"total_price\":%d}", 
             card_id, item_id, quantity, total_price);

    ESP_LOGI(TAG, "Sending order: %s", post_data);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = SERVER_URL "/order",
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // POST Request
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) {
            ESP_LOGE(TAG, "Write failed");
        } else {
            int content_length = esp_http_client_fetch_headers(client);
            if (content_length < 0) {
                ESP_LOGE(TAG, "HTTP client fetch headers failed");
            } else {
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {
                    int status_code = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                            status_code, content_length);
                    
                    ESP_LOGI(TAG, "Response: %s", output_buffer);
                    
                    if (status_code == 200) {
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "✓ ORDER SENT SUCCESSFULLY!");
                        ESP_LOGI(TAG, "========================================");
                        success = true;
                    } else {
                        ESP_LOGW(TAG, "Server returned status code: %d", status_code);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to read response");
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    return success;
}

// Kiểm tra thẻ có tồn tại trong DB không
bool http_verify_card(const char *card_id)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    bool card_exists = false;

    // Tạo URL với card_id
    char url[256];
    snprintf(url, sizeof(url), "%s/verify_card/%s", SERVER_URL, card_id);

    ESP_LOGI(TAG, "Verifying card: %s", card_id);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // GET Request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length >= 0) {
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP GET Status = %d", status_code);
                ESP_LOGI(TAG, "Response: %s", output_buffer);
                
                if (status_code == 200) {
                    card_exists = true;
                    ESP_LOGI(TAG, "Card exists in database");
                } else if (status_code == 404) {
                    ESP_LOGW(TAG, "Card NOT found in database");
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(100));
    return card_exists;
}

// Xác thực thẻ và mật khẩu
bool http_verify_password(const char *card_id, const char *password)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    bool password_correct = false;

    // Tạo JSON payload
    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "{\"id_card\":\"%s\",\"password\":\"%s\"}", 
             card_id, password);

    ESP_LOGI(TAG, "Verifying password for card: %s", card_id);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = SERVER_URL "/verify_password",
        .timeout_ms = 30000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // POST Request
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen >= 0) {
            int content_length = esp_http_client_fetch_headers(client);
            if (content_length >= 0) {
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {
                    int status_code = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);
                    ESP_LOGI(TAG, "Response: %s", output_buffer);
                    
                    if (status_code == 200) {
                        password_correct = true;
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "✓ PASSWORD CORRECT!");
                        ESP_LOGI(TAG, "========================================");
                    } else if (status_code == 401) {
                        ESP_LOGE(TAG, "========================================");
                        ESP_LOGE(TAG, "✗ WRONG PASSWORD!");
                        ESP_LOGE(TAG, "========================================");
                    }
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(100));
    return password_correct;
}

// Kiểm tra số dư tài khoản
int http_check_balance(const char *card_id)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return -1;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    int balance = -1;

    // Tạo URL với card_id
    char url[256];
    snprintf(url, sizeof(url), "%s/get_balance/%s", SERVER_URL, card_id);

    ESP_LOGI(TAG, "Checking balance for card: %s", card_id);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // GET Request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } else {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length >= 0) {
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "HTTP GET Status = %d", status_code);
                ESP_LOGI(TAG, "Response: %s", output_buffer);
                
                if (status_code == 200) {
                    // Parse JSON response để lấy balance
                    // Format: {"balance": 50000}
                    char *balance_str = strstr(output_buffer, "balance");
                    if (balance_str) {
                        sscanf(balance_str, "balance\":%d", &balance);
                        ESP_LOGI(TAG, "Account balance: %d VND", balance);
                    }
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(100));
    return balance;
}

// Xử lý thanh toán (trừ tiền)
bool http_process_payment(const char *card_id, int amount)
{
    if (!wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    char output_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    bool payment_success = false;

    // Tạo JSON payload
    char post_data[256];
    snprintf(post_data, sizeof(post_data), 
             "{\"id_card\":\"%s\",\"amount\":%d}", 
             card_id, amount);

    ESP_LOGI(TAG, "Processing payment: %s", post_data);

    // Cấu hình HTTP client
    esp_http_client_config_t config = {
        .url = SERVER_URL "/process_payment",
        .timeout_ms = 10000,
        .event_handler = _http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // POST Request
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        lcd_clear();
        lcd_printf(0, 0, "LOI KET NOI");
        lcd_printf(0, 1, "SERVER");
    } else {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen >= 0) {
            int content_length = esp_http_client_fetch_headers(client);
            if (content_length >= 0) {
                int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
                if (data_read >= 0) {
                    int status_code = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);
                    ESP_LOGI(TAG, "Response: %s", output_buffer);
                    
                    if (status_code == 200) {
                        payment_success = true;
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "✓ THANH TOÁN THÀNH CÔNG!");
                        ESP_LOGI(TAG, "Amount: %d VND", amount);
                        ESP_LOGI(TAG, "========================================");
                        
                    } else if (status_code == 400) {
                        // Kiểm tra lỗi số dư không đủ
                        if (strstr(output_buffer, "insufficient") != NULL || 
                            strstr(output_buffer, "không đủ") != NULL) {
                            ESP_LOGE(TAG, "========================================");
                            ESP_LOGE(TAG, "✗ SỐ DƯ KHÔNG ĐỦ!");
                            ESP_LOGE(TAG, "Required: %d VND", amount);
                            ESP_LOGE(TAG, "========================================");
                            
                            lcd_clear();
                            lcd_printf(0, 0, "SO DU KHONG DU");
                            lcd_printf(0, 1, "Yeu cau: %d", amount);
                        } else {
                            ESP_LOGW(TAG, "Payment failed: %s", output_buffer);
                            lcd_clear();
                            lcd_printf(0, 0, "THANH TOAN LOI");
                        }
                    }
                }
            }
        }
    }
    
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(100));
    return payment_success;
}

// Kiểm tra kết nối WiFi
bool http_is_connected(void)
{
    return wifi_connected;
}
