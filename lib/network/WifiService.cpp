#include "WifiService.hpp"

#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "web_page.hpp"
#include "logo1.hpp"  // Use original logo
#include "logo2.hpp"

#include <algorithm>

static const char* TAG = "WifiService";

#define NVS_NS   "wifi"
#define NVS_SSID "ssid"
#define NVS_PASS "pass"

static std::string urldecode(const std::string& s)
{
    std::string out;
    char ch;
    int val;

    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            sscanf(s.substr(i+1, 2).c_str(), "%x", &val);
            ch = (char)val;
            out += ch;
            i += 2;
        } else out += s[i];
    }
    return out;
}

// ============================================================================
// HTTP Portal handlers
// ============================================================================
esp_err_t portal_GET_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Portal GET handler called");
    
    auto* self = (WifiService*)req->user_ctx;
    
    if (!self) {
        ESP_LOGE(TAG, "Portal GET: invalid user context");
        httpd_resp_sendstr(req, "Error: server error");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    auto nets = self->scanNetworks();
    ESP_LOGI(TAG, "WiFi scan found %d networks", nets.size());

    std::string list;
    list.reserve(2500);  // Larger buffer for 10 networks

    // Limit to first 10 networks
    size_t max_nets = (nets.size() > 10) ? 10 : nets.size();
    
    for (size_t i = 0; i < max_nets; i++) {
        auto& n = nets[i];
        
        // Skip if list is getting too large
        if (list.size() > 2400) {
            ESP_LOGW(TAG, "WiFi list too large, truncating at %d networks", i);
            break;
        }
        
        int quality = (n.rssi <= -100) ? 0 : (n.rssi >= -50 ? 100 : 2 * (n.rssi + 100));
        std::string bar_color = (quality > 60) ? "#48bb78" :
                                (quality > 30) ? "#ecc94b" : "#f56565";

        list += "<div class='wifi-item' onclick=\"sel('" + n.ssid + "')\">";
        list += "<span class='ssid-text'>" + n.ssid + "</span>";
        list += "<div class='rssi-box'>" + std::to_string(n.rssi) + " dBm";
        list += "<div class='bar-bg'><div class='bar-fg' style='width:" + std::to_string(quality);
        list += "%; background:" + bar_color + ";'></div></div>";
        list += "</div></div>";
    }

    ESP_LOGI(TAG, "Sending chunked response...");
    httpd_resp_set_type(req, "text/html");
    
    // Split page and send in chunks to minimize memory usage
    std::string page_str = PAGE_HTML;
    
    // Find positions of replacements
    size_t pos_wifi = page_str.find("%WIFI_LIST%");
    size_t pos_logo1 = page_str.find("%LOGO1%");
    size_t pos_logo2 = page_str.find("%LOGO2%");
    
    // 1. Send: khung (từ đầu đến LOGO1)
    ESP_LOGI(TAG, "1. Sending: frame (before LOGO1)");
    httpd_resp_send_chunk(req, page_str.c_str(), pos_logo1);
    
    // 2. Send LOGO1
    ESP_LOGI(TAG, "2. Sending: LOGO1 (%zu bytes)", strlen(LOGO1_DATA));
    httpd_resp_send_chunk(req, LOGO1_DATA, strlen(LOGO1_DATA));
    
    // 3. Send: từ sau LOGO1 đến LOGO2
    size_t after_logo1 = pos_logo1 + 7;  // Skip "%LOGO1%"
    size_t between_len = pos_logo2 - after_logo1;
    
    ESP_LOGI(TAG, "3. Sending: separator (between LOGO1 and LOGO2)");
    httpd_resp_send_chunk(req, page_str.c_str() + after_logo1, between_len);
    
    // 4. Send LOGO2
    ESP_LOGI(TAG, "4. Sending: LOGO2 (%zu bytes)", strlen(LOGO2_DATA));
    httpd_resp_send_chunk(req, LOGO2_DATA, strlen(LOGO2_DATA));
    
    // 5. Send: từ sau LOGO2 đến WiFi list
    size_t after_logo2 = pos_logo2 + 7;  // Skip "%LOGO2%"
    size_t to_wifi = pos_wifi - after_logo2;
    
    ESP_LOGI(TAG, "5. Sending: header (before WiFi list)");
    httpd_resp_send_chunk(req, page_str.c_str() + after_logo2, to_wifi);
    
    // 6. Send WiFi list
    ESP_LOGI(TAG, "6. Sending: WiFi list (%zu bytes)", list.size());
    httpd_resp_send_chunk(req, list.c_str(), list.size());
    
    // 7. Send: nút (từ sau list đến cuối)
    size_t after_wifi = pos_wifi + 11;  // Skip "%WIFI_LIST%"
    size_t buttons_len = page_str.size() - after_wifi;
    
    ESP_LOGI(TAG, "7. Sending: buttons/form (after WiFi list)");
    httpd_resp_send_chunk(req, page_str.c_str() + after_wifi, buttons_len);
    
    // End chunked response
    httpd_resp_send_chunk(req, NULL, 0);
    
    ESP_LOGI(TAG, "Portal GET handler completed successfully");
    return ESP_OK;
}

esp_err_t portal_POST_handler(httpd_req_t* req)
{
    auto* self = (WifiService*)req->user_ctx;

    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf)-1);
    if (len <= 0) {
        ESP_LOGW(TAG, "Portal POST: no data received");
        httpd_resp_sendstr(req, "Error: no data");
        return ESP_FAIL;
    }
    buf[len] = 0;

    std::string body(buf);
    
    // Validate form fields exist
    auto pos_ssid = body.find("ssid=");
    auto pos_pass = body.find("&pass=");

    if (pos_ssid == std::string::npos || pos_pass == std::string::npos) {
        ESP_LOGW(TAG, "Portal POST: missing ssid or pass fields");
        httpd_resp_sendstr(req, "Error: missing fields");
        return ESP_FAIL;
    }

    // Safely extract SSID and PASS
    size_t ssid_start = pos_ssid + 5;
    size_t ssid_len = pos_pass - ssid_start;
    if (ssid_len == 0 || ssid_len > 255) {
        ESP_LOGW(TAG, "Portal POST: invalid SSID length");
        httpd_resp_sendstr(req, "Error: invalid SSID");
        return ESP_FAIL;
    }
    
    std::string ssid = body.substr(ssid_start, ssid_len);
    
    size_t pass_start = pos_pass + 6;
    std::string pass = body.substr(pass_start);
    
    // Limit password length
    if (pass.size() > 255) {
        pass = pass.substr(0, 255);
    }

    ssid = urldecode(ssid);
    pass = urldecode(pass);

    if (ssid.empty()) {
        ESP_LOGW(TAG, "Portal POST: SSID is empty after decode");
        httpd_resp_sendstr(req, "Error: SSID cannot be empty");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Portal received SSID=%s PASS=%s", ssid.c_str(), pass.c_str());

    self->connectWithCredentials(ssid.c_str(), pass.c_str());

    httpd_resp_sendstr(req, "OK, rebooting WiFi...");
    return ESP_OK;
}

// ============================================================================
// INIT
// ============================================================================
void WifiService::init()
{
    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable power save to reduce beacon timeout disconnects during streaming
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    // Reduce WiFi log spam (channel switch, beacon timeout details)
    esp_log_level_set("wifi", ESP_LOG_WARN);

    registerEvents();
    loadCredentials();
}

// ============================================================================
// AUTO CONNECT
// ============================================================================
bool WifiService::autoConnect()
{
    if (!auto_connect_enabled) return false;

    if (sta_ssid.empty() || sta_pass.empty()) {
        ESP_LOGW(TAG, "No credentials → opening portal");
        startCaptivePortal();
        return false;
    }

    startSTA();
    return true;
}

// ============================================================================
// START STA
// ============================================================================
void WifiService::startSTA()
{
    wifi_config_t cfg = {};
    strncpy((char*)cfg.sta.ssid, sta_ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
    strncpy((char*)cfg.sta.password, sta_pass.c_str(), sizeof(cfg.sta.password) - 1);

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode STA: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect WiFi: %s", esp_err_to_name(ret));
        return;
    }

    if (status_cb) status_cb(1); // CONNECTING
}

// ============================================================================
// START CAPTIVE PORTAL
// ============================================================================
void WifiService::startCaptivePortal(const std::string& ap_ssid, uint8_t ap_num_connections)
{
    if (portal_running) return;

    // Stop any existing portal first to avoid conflicts
    stopCaptivePortal();

    ESP_LOGI(TAG, "Starting Captive Portal: SSID=%s max_conn=%d",
             ap_ssid.c_str(), ap_num_connections);

    wifi_config_t cfg = {};
    strncpy((char*)cfg.ap.ssid, ap_ssid.c_str(), sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len       = ap_ssid.size();
    cfg.ap.authmode       = WIFI_AUTH_OPEN;
    cfg.ap.max_connection = ap_num_connections;

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode APSTA: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return;
    }

    // HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 20480;  // Increased to 20KB for complex string operations
    config.max_uri_handlers = 8;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.lru_purge_enable = true;  // Enable LRU purge to free up connections
    config.backlog_conn = 2;  // Limit backlog to reduce memory usage
    config.max_open_sockets = 4;  // Limit concurrent connections

    ret = httpd_start(&http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        http_server = nullptr;
        return;
    }

    // Register handlers only after successful server start
    httpd_uri_t get = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = portal_GET_handler,
        .user_ctx = this,
    };
    ret = httpd_register_uri_handler(http_server, &get);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register GET handler: %s", esp_err_to_name(ret));
    }

    httpd_uri_t post = {
        .uri      = "/connect",
        .method   = HTTP_POST,
        .handler  = portal_POST_handler,
        .user_ctx = this,
    };
    ret = httpd_register_uri_handler(http_server, &post);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register POST handler: %s", esp_err_to_name(ret));
    }

    portal_running = true;
    ESP_LOGI(TAG, "Captive Portal started successfully");
}

// ============================================================================
// STOP PORTAL
// ============================================================================
void WifiService::stopCaptivePortal()
{
    if (http_server == nullptr && !portal_running) return;

    ESP_LOGI(TAG, "Stopping Captive Portal");

    if (http_server) {
        esp_err_t ret = httpd_stop(http_server);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "httpd_stop returned: %s", esp_err_to_name(ret));
        }
        http_server = nullptr;
    }

    portal_running = false;
    ESP_LOGI(TAG, "Captive Portal stopped");
}

// ============================================================================
// DISCONNECT
// ============================================================================
void WifiService::disconnect()
{
    ESP_LOGW(TAG, "WiFi Disconnect");
    esp_wifi_disconnect();
    connected = false;
    if (status_cb) status_cb(0);
}

// ============================================================================
// SCAN NETWORKS
// ============================================================================
std::vector<WifiInfo> WifiService::scanNetworks()
{
    ESP_LOGI(TAG, "Starting WiFi network scan");
    
    wifi_scan_config_t cfg = {};
    cfg.show_hidden = false;
    cfg.scan_time.active.min = 100;  // Faster scan
    cfg.scan_time.active.max = 300;

    esp_err_t ret = esp_wifi_scan_start(&cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %s", esp_err_to_name(ret));
        return std::vector<WifiInfo>();  // Return empty vector
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        return std::vector<WifiInfo>();
    }

    // Limit max APs to prevent memory issues
    if (ap_count > 20) {
        ESP_LOGW(TAG, "Limiting scan results from %d to 20 APs", ap_count);
        ap_count = 20;
    }

    ESP_LOGI(TAG, "Found %d access points", ap_count);

    if (ap_count == 0) {
        return std::vector<WifiInfo>();
    }

    std::vector<wifi_ap_record_t> records(ap_count);
    ret = esp_wifi_scan_get_ap_records(&ap_count, records.data());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
        return std::vector<WifiInfo>();
    }

    std::vector<WifiInfo> out;
    out.reserve(ap_count);  // Pre-allocate
    
    for (auto& r : records) {
        if (r.ssid[0] == '\0') continue;
        WifiInfo info;
        info.ssid = (char*)r.ssid;
        info.rssi = r.rssi;
        out.push_back(info);
    }

    std::sort(out.begin(), out.end(), [](auto& a, auto& b){ return a.rssi > b.rssi; });
    
    ESP_LOGI(TAG, "WiFi scan completed, returning %d networks", out.size());
    return out;
}

// ============================================================================
// CREDENTIALS
// ============================================================================
void WifiService::connectWithCredentials(const char* ssid, const char* pass)
{
    saveCredentials(ssid, pass);
    startSTA();
}

void WifiService::loadCredentials()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        sta_ssid.clear();
        sta_pass.clear();
        return;
    }

    // SSID
    size_t len = 0;
    if (nvs_get_str(h, NVS_SSID, nullptr, &len) == ESP_OK && len > 1) {
        std::string buf(len, 0);
        nvs_get_str(h, NVS_SSID, buf.data(), &len);
        sta_ssid = buf.c_str();
    }

    // PASS
    len = 0;
    if (nvs_get_str(h, NVS_PASS, nullptr, &len) == ESP_OK && len > 1) {
        std::string buf(len, 0);
        nvs_get_str(h, NVS_PASS, buf.data(), &len);
        sta_pass = buf.c_str();
    }

    nvs_close(h);

    ESP_LOGI(TAG, "Credentials loaded: SSID=%s PASS=%s",
             sta_ssid.c_str(), sta_pass.empty() ? "(empty)" : "****");
}

void WifiService::saveCredentials(const char* ssid, const char* pass)
{
    if (!ssid || !pass) {
        ESP_LOGE(TAG, "Invalid credentials: ssid or pass is NULL");
        return;
    }

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_str(h, NVS_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set SSID: %s", esp_err_to_name(ret));
        nvs_close(h);
        return;
    }

    ret = nvs_set_str(h, NVS_PASS, pass);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PASS: %s", esp_err_to_name(ret));
        nvs_close(h);
        return;
    }

    ret = nvs_commit(h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(h);

    sta_ssid = ssid;
    sta_pass = pass;

    ESP_LOGI(TAG, "Credentials saved: %s / ****", ssid);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================
void WifiService::registerEvents()
{
    esp_err_t ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &WifiService::wifiEventHandlerStatic, this, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &WifiService::ipEventHandlerStatic, this, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Event handlers registered successfully");
}

void WifiService::wifiEventHandlerStatic(void* arg, esp_event_base_t base,
                                         int32_t id, void* data)
{
    ((WifiService*)arg)->wifiEventHandler(base, id, data);
}

void WifiService::ipEventHandlerStatic(void* arg, esp_event_base_t base,
                                       int32_t id, void* data)
{
    ((WifiService*)arg)->ipEventHandler(base, id, data);
}

void WifiService::wifiEventHandler(esp_event_base_t base, int32_t id, void* data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "STA start");
        break;

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "STA connected to AP");
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "STA disconnected from AP");
        connected = false;
        
        if (status_cb) {
            status_cb(0);  // DISCONNECTED
        }

        // If never connected successfully, open portal (first boot failure)
        if (!has_connected_once) {
            ESP_LOGW(TAG, "First connection failed, opening portal");
            auto_connect_enabled = false;  // Prevent retry loop
            startCaptivePortal();
        }
        // If was connected before, try to reconnect (disconnected during operation)
        else if (auto_connect_enabled) {
            ESP_LOGW(TAG, "Connection lost, attempting reconnect");
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "Auto-connect disabled, starting portal");
            startCaptivePortal();
        }
        break;

    default:
        ESP_LOGD(TAG, "WiFi event %d", id);
        break;
    }
}

void WifiService::ipEventHandler(esp_event_base_t base, int32_t id, void* data)
{
    if (id != IP_EVENT_STA_GOT_IP) return;

    ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP received");
    
    connected = true;
    has_connected_once = true;  // Mark that WiFi connected successfully at least once
    
    if (status_cb) {
        status_cb(2);  // GOT_IP
    }
    
    ESP_LOGI(TAG, "IP event handler completed");
}

std::string WifiService::getIp() const
{
    if (!connected) return "";

    esp_netif_ip_info_t ip;
    esp_netif_t* n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK) {
        char buf[48];  // Increased buffer size
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
        return buf;
    }
    return "";
}
