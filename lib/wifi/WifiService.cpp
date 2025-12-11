#include "WifiService.hpp"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "web_page.hpp"  // Chứa khung HTML
#include "logo1.hpp"     // Chứa dữ liệu ảnh 1
#include "logo2.hpp"     // Chứa dữ liệu ảnh 2

#include <string>
#include <algorithm>

static const char* TAG = "WifiService";

#define NVS_NAMESPACE "wifi"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

// Hàm helper để replace chuỗi (đỡ viết đi viết lại)
void replaceString(std::string& subject, const std::string& search, const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
        subject.replace(pos, search.length(), replace);
        pos += replace.length();
    }
}


std::string urlDecode(const std::string& src) {
    std::string ret;
    char ch;
    int ii;
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '+') {
            // '+' trong form nghĩa là khoảng trắng
            ret += ' ';
        } else if (src[i] == '%' && i + 2 < src.length()) {
            sscanf(src.substr(i+1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            ret += ch;
            i += 2;
        } else {
            ret += src[i];
        }
    }
    return ret;
}


static esp_err_t portal_get_handler(httpd_req_t *req) {
    // 1. Quét WiFi & Tạo danh sách HTML
    auto networks = WifiService::instance().getAvailableNetworks();
    std::string listHtml = "";
    
    for (auto& net : networks) {
        int quality = (net.rssi <= -100) ? 0 : (net.rssi >= -50 ? 100 : 2 * (net.rssi + 100));
        std::string color = (quality > 60) ? "#48bb78" : ((quality > 30) ? "#ecc94b" : "#f56565");

        listHtml += "<div class='wifi-item' onclick=\"sel('" + net.ssid + "')\">";
        listHtml +=   "<span class='ssid-text'>" + net.ssid + "</span>";
        listHtml +=   "<div class='rssi-box'>" + std::to_string(net.rssi) + " dBm";
        listHtml +=      "<div class='bar-bg'><div class='bar-fg' style='width:" + std::to_string(quality) + "%; background:" + color + ";'></div></div>";
        listHtml +=   "</div></div>";
    }

    // 2. Lấy khung HTML gốc
    std::string finalPage = PAGE_HTML;

    // 3. THAY THẾ CÁC PLACEHOLDER BẰNG DỮ LIỆU THẬT
    
    // Thay thế danh sách WiFi
    replaceString(finalPage, "%WIFI_LIST%", listHtml);
    
    // Thay thế Logo 1 (Lấy từ file logo1.h)
    replaceString(finalPage, "%LOGO1%", LOGO1_DATA);
    
    // Thay thế Logo 2 (Lấy từ file logo2.h)
    replaceString(finalPage, "%LOGO2%", LOGO2_DATA);

    // 4. Gửi về trình duyệt
    httpd_resp_send(req, finalPage.c_str(), finalPage.size());
    return ESP_OK;
}

static esp_err_t portal_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = 0;

    std::string body(buf);
    auto pos_ssid = body.find("ssid=");
    auto pos_pass = body.find("&pass=");
    std::string ssid = body.substr(pos_ssid+5, pos_pass-(pos_ssid+5));
    std::string pass = body.substr(pos_pass+6);

    // Decode URL-encoded
    ssid = urlDecode(ssid);
    pass = urlDecode(pass);

    WifiService::instance().connectWithCredentials(ssid.c_str(), pass.c_str());

    httpd_resp_sendstr(req, "Saved! ESP32 will connect...");
    return ESP_OK;
}

void WifiService::init() {
    // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Khởi tạo TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Khởi tạo event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Khởi tạo WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    registerEvents();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    loadCredentials();

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif  = esp_netif_create_default_wifi_ap();

}

bool WifiService::autoConnect() {
    if (!auto_connect_enabled) {
        ESP_LOGI(TAG, "AutoConnect disabled by user");
        return false;
    }

    if (sta_ssid.empty() || sta_pass.empty()) {
        ESP_LOGW(TAG, "No credentials found, starting Captive Portal");
        startCaptivePortal();
        return false;
    }

    startSTA();
    return true;
}

void WifiService::startSTA() {
    ESP_LOGI(TAG, "Starting STA mode with SSID:%s", sta_ssid.c_str());

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, sta_ssid.c_str(), sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, sta_pass.c_str(), sizeof(wifi_config.sta.password));

    // Chỉ set mode và config, không tạo lại netif
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    if (status_callback) status_callback(1); // CONNECTING
}



void WifiService::startCaptivePortal() {
    if (portal_running) return;
    ESP_LOGI(TAG, "Starting Captive Portal");

    // Dùng ap_netif đã tạo trong init()
    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "ESP32_Config");
    ap_config.ap.ssid_len = strlen("ESP32_Config");
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // AP+STA để cho phép scan
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = portal_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        httpd_uri_t uri_post = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = portal_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post);
    }

    portal_running = true;
}


void WifiService::stopCaptivePortal() {
    if (!portal_running) return;
    ESP_LOGI(TAG, "Stopping Captive Portal");
    if (server) {
        httpd_stop(server);
        server = nullptr;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL)); // hoặc STA nếu muốn chuyển ngay
    portal_running = false;
}


void WifiService::disconnect() {
    ESP_LOGI(TAG, "Disconnecting STA");
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    wifi_connected = false;
    if (status_callback) status_callback(0); // DISCONNECTED
}

void WifiService::disableAutoConnect() {
    auto_connect_enabled = false;
}

bool WifiService::isConnected() const {
    return wifi_connected;
}

std::string WifiService::getIp() const {
    if (!wifi_connected) return "";
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char buf[32];
        sprintf(buf, IPSTR, IP2STR(&ip_info.ip));
        return std::string(buf);
    }
    return "";
}

void WifiService::onStatus(std::function<void(int)> cb) {
    status_callback = cb;
}

void WifiService::loadCredentials() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No NVS namespace found, credentials empty");
        sta_ssid.clear();
        sta_pass.clear();
        return;
    }

    // Đọc SSID
    size_t ssid_len = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, NULL, &ssid_len);
    if (err == ESP_OK && ssid_len > 1) {
        char* ssid_buf = (char*)malloc(ssid_len);
        nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid_buf, &ssid_len);
        sta_ssid = ssid_buf;
        free(ssid_buf);
    } else {
        sta_ssid.clear();
    }

    // Đọc PASS
    size_t pass_len = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_PASS, NULL, &pass_len);
    if (err == ESP_OK && pass_len > 1) {
        char* pass_buf = (char*)malloc(pass_len);
        nvs_get_str(nvs_handle, NVS_KEY_PASS, pass_buf, &pass_len);
        sta_pass = pass_buf;
        free(pass_buf);
    } else {
        sta_pass.clear();
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Loaded credentials: SSID='%s', PASS='%s'",
             sta_ssid.c_str(), sta_pass.empty() ? "(empty)" : "****");
}

void WifiService::saveCredentials(const char* ssid, const char* pass) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        return;
    }

    // Lưu SSID
    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID");
    }

    // Lưu PASS
    err = nvs_set_str(nvs_handle, NVS_KEY_PASS, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save PASS");
    }

    // Commit
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS");
    }

    nvs_close(nvs_handle);

    sta_ssid = ssid;
    sta_pass = pass;

    ESP_LOGI(TAG, "Saved credentials: SSID='%s', PASS='%s'",
             sta_ssid.c_str(), sta_pass.empty() ? "(empty)" : "****");
}

void WifiService::registerEvents() {
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiService::wifi_event_handler,
                                                        this,
                                                        nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiService::ip_event_handler,
                                                        this,
                                                        nullptr));
}

void WifiService::scanNetworks(std::vector<WifiInfo>& networks) {
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;

    // Start scan (Blocking mode = true để chờ kết quả)
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
    
    // Cấp phát bộ nhớ đệm để lấy kết quả
    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * ap_num);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

    networks.clear();
    
    // Lọc và lưu vào vector
    for (int i = 0; i < ap_num; i++) {
        // Bỏ qua SSID rỗng
        if (ap_records[i].ssid[0] != 0) {
            WifiInfo info;
            info.ssid = std::string((char*)ap_records[i].ssid);
            info.rssi = ap_records[i].rssi;
            networks.push_back(info);
        }
    }
    free(ap_records);

    //sort networks by RSSI descending
    std::sort(networks.begin(), networks.end(), [](const WifiInfo& a, const WifiInfo& b) {
        return a.rssi > b.rssi;
    });
    
}

void WifiService::wifi_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data) {
    WifiService* self = static_cast<WifiService*>(arg);
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi STA connected");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            {
            wifi_event_sta_disconnected_t* disconn = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "WiFi STA disconnected, reason=%d", disconn->reason);
            self->wifi_connected = false;
            if (self->status_callback) self->status_callback(0);
            if (self->auto_connect_enabled) {
                esp_wifi_connect();
            } else {
                self->startCaptivePortal();
            }
            break;
        }
        default:
            break;
        }
    }
}

void WifiService::ip_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data) {
    WifiService* self = static_cast<WifiService*>(arg);
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        self->wifi_connected = true;
        if (self->status_callback) self->status_callback(2);
    }
}
