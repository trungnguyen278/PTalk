#include "WifiService.hpp"
#include "web_page.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "sdkconfig.h"

#include <string>
#include <vector>
#include <cstring>

// Logos (data URLs)
#include "../../src/assets/logos/logo1.hpp"
#include "../../src/assets/logos/logo2.hpp"

static const char *TAG = "WifiService";

// Small helpers
static int rssiToPercent(int rssi)
{
    if (rssi <= -100)
        return 0;
    if (rssi >= -50)
        return 100;
    return 2 * (rssi + 100);
}
// --------------------------------------------------------------------------------
// Struct to hold WiFi connection parameters for async connect
struct WifiConnParams
{
    WifiService *svc;
    std::string ssid;
    std::string pass;
};
// --------------------------------------------------------------------------------
// HTTP helpers & templates
// We'll use PAGE_HTML from web_page.hpp and replace placeholders
// --------------------------------------------------------------------------------

static std::string makeWifiListHtml(const std::vector<WifiInfo> &list)
{
    std::string out;
    for (const auto &w : list)
    {
        int pct = rssiToPercent(w.rssi);
        const char *color = (pct > 66) ? "#48bb78" : (pct > 33) ? "#ed8936"
                                                                : "#e53e3e";
        char buf[512];
        // escape single quotes in SSID for onclick handler
        std::string esc = w.ssid;
        for (auto &c : esc)
            if (c == '\'')
                c = ' ';
        snprintf(buf, sizeof(buf),
                 "<div class='wifi-item' onclick=\"sel('%s')\">"
                 "<div class='ssid-text'>%s</div>"
                 "<div class='rssi-box'><div class='bar-bg'><div class='bar-fg' style='width:%d%%;background:%s'></div></div><div>%ddBm</div></div>"
                 "</div>",
                 esc.c_str(), w.ssid.c_str(), pct, color, w.rssi);
        out += buf;
    }
    if (out.empty())
        out = "<div style='padding:12px;color:#718096'>Không tìm thấy mạng WiFi</div>";
    return out;
}

// --------------------------------------------------------------------------------
// NVS helpers (store SSID/PASS in namespace "storage")
// --------------------------------------------------------------------------------
static bool nvs_set_string(const char *ns, const char *key, const std::string &v)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
    if (e != ESP_OK)
        return false;
    e = nvs_set_str(h, key, v.c_str());
    if (e == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

static bool nvs_get_string(const char *ns, const char *key, std::string &out)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
    if (e != ESP_OK)
        return false;
    size_t required = 0;
    e = nvs_get_str(h, key, nullptr, &required);
    if (e != ESP_OK)
    {
        nvs_close(h);
        return false;
    }
    out.resize(required);
    e = nvs_get_str(h, key, out.data(), &required);
    nvs_close(h);
    if (e != ESP_OK)
        return false;
    // remove trailing null if present
    if (!out.empty() && out.back() == '\0')
        out.resize(out.size() - 1);
    return true;
}

// --------------------------------------------------------------------------------
// HTTP server handlers
// --------------------------------------------------------------------------------

static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET %s", req->uri);

    HandlerContext *ctx = (HandlerContext *)req->user_ctx;
    if (!ctx)
    {
        ESP_LOGE(TAG, "No handler context");
        return ESP_FAIL;
    }
    if (httpd_resp_set_type(req, "text/html; charset=utf-8") != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set response type");
        return ESP_FAIL;
    }

    // HEAD + LOGO (logo load bằng URL nội bộ)
    if (httpd_resp_send_chunk(req, PAGE_HTML_HEAD, strlen(PAGE_HTML_HEAD)) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send HTML head");
        return ESP_FAIL;
    }

    // Trước danh sách WiFi
    if (httpd_resp_send_chunk(req, PAGE_HTML_BEFORE_LIST, strlen(PAGE_HTML_BEFORE_LIST)) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send HTML before list");
        return ESP_FAIL;
    }

    // WiFi list (dynamic)
    std::string list = makeWifiListHtml(ctx->svc->getCachedNetworks());
    if (httpd_resp_send_chunk(req, list.c_str(), list.size()) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send WiFi list");
        return ESP_FAIL;
    }

    // Footer
    if (httpd_resp_send_chunk(req, PAGE_HTML_FOOTER, strlen(PAGE_HTML_FOOTER)) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send HTML footer");
        return ESP_FAIL;
    }

    // End response
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP POST %s", req->uri);
    HandlerContext *ctx = (HandlerContext *)req->user_ctx;
    if (!ctx)
        return ESP_FAIL;

    // 1. Read POST data
    int len = req->content_len;
    std::string body;
    body.resize(len);
    int ret = httpd_req_recv(req, reinterpret_cast<char *>(body.data()), len);
    if (ret <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    // 2. Parse field helper (giữ nguyên logic của bạn)
    auto get_field = [&](const std::string &key) -> std::string
    {
        std::string needle = key + "=";
        size_t i = body.find(needle);
        if (i == std::string::npos)
            return "";
        i += needle.size();
        size_t j = body.find('&', i);
        std::string val = body.substr(i, (j == std::string::npos) ? std::string::npos : (j - i));
        std::string dec;
        for (size_t k = 0; k < val.size(); ++k)
        {
            if (val[k] == '+')
                dec.push_back(' ');
            else if (val[k] == '%' && k + 2 < val.size())
            {
                char hex[3] = {val[k + 1], val[k + 2], 0};
                dec.push_back((char)strtol(hex, nullptr, 16));
                k += 2;
            }
            else
                dec.push_back(val[k]);
        }
        return dec;
    };

    std::string ssid = get_field("ssid");
    std::string pass = get_field("pass");

    if (ssid.empty())
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty SSID");
        return ESP_FAIL;
    }

    // 3. GỬI PHẢN HỒI TRƯỚC (Quan trọng nhất để tránh Deadlock)
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);

    // 4. Tạo một Task riêng để xử lý kết nối (Deferred Execution)
    WifiConnParams *params = new WifiConnParams{ctx->svc, ssid, pass};

    xTaskCreate([](void *arg)
                {
                    WifiConnParams *p = (WifiConnParams *)arg;
                    vTaskDelay(pdMS_TO_TICKS(500)); // Đợi 0.5s để server gửi xong gói tin HTTP cuối cùng

                    ESP_LOGI("WifiTask", "Executing connection switch...");
                    p->svc->connectWithCredentials(p->ssid.c_str(), p->pass.c_str());

                    delete p;          // Giải phóng bộ nhớ struct
                    vTaskDelete(NULL); // Tự xóa task
                },
                "wifi_conn_task", 4096, params, 5, NULL);

    return ESP_OK;
}

/**
 * Debug handler for any unhandled POST requests
 */
static esp_err_t any_post_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "UNHANDLED POST %s", req->uri);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No handler");
    return ESP_OK;
}

static esp_err_t logo1_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png"); // ✅ PNG
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    return httpd_resp_send(
        req,
        (const char *)LOGO1_PNG,
        LOGO1_PNG_LEN);
}

static esp_err_t logo2_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png"); // ✅ PNG
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    return httpd_resp_send(
        req,
        (const char *)LOGO2_PNG,
        LOGO2_PNG_LEN);
}
// --------------------------------------------------------------------------------
// WifiService implementation
// --------------------------------------------------------------------------------

void WifiService::init()
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Xóa dòng esp_netif_init() dư thừa ở đây
    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    registerEvents();
    ESP_LOGI(TAG, "WifiService initialized");
}

bool WifiService::autoConnect()
{
    loadCredentials();
    if (sta_ssid.empty())
    {
        ESP_LOGW(TAG, "autoConnect: No saved credentials found");
        return false;
    }
    ESP_LOGI(TAG, "autoConnect: Attempting to connect with saved credentials (SSID: %s, Pass: %s)",
             sta_ssid.c_str(), sta_pass.empty() ? "<empty>" : "<set>");
    startSTA();
    return true;
}

void WifiService::startCaptivePortal(const std::string &ap_ssid, const uint8_t ap_num_connections, bool stop_wifi_first)
{
    if (portal_running)
        return;

    // Stop WiFi first if requested
    if (stop_wifi_first)
    {
        esp_wifi_stop();
        wifi_started = false;
        ESP_LOGI(TAG, "WiFi stopped before starting portal");
    }

    ap_only_mode = true;

    wifi_config_t ap_config = {};
    strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), ap_ssid.c_str(), sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = ap_ssid.size();
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = ap_num_connections;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start simple HTTP server
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = 80;

    http_cfg.stack_size = 8192;

    if (httpd_start(&http_server, &http_cfg) == ESP_OK)
    {

        // GET /
        httpd_uri_t root_get = {};
        root_get.uri = "/";
        root_get.method = HTTP_GET;
        root_get.handler = root_get_handler;
        root_get.user_ctx = &http_ctx;
        httpd_register_uri_handler(http_server, &root_get);

        // POST /connect
        httpd_uri_t connect_post = {};
        connect_post.uri = "/connect";
        connect_post.method = HTTP_POST;
        connect_post.handler = connect_post_handler;
        connect_post.user_ctx = &http_ctx;
        httpd_register_uri_handler(http_server, &connect_post);

        // Any other POST
        httpd_uri_t any_post = {};
        any_post.uri = "/*";
        any_post.method = HTTP_POST;
        any_post.handler = any_post_handler;
        httpd_register_uri_handler(http_server, &any_post);

        // GET /logo1.jpg
        httpd_uri_t logo1_get = {};
        logo1_get.uri = "/logo1.jpg";
        logo1_get.method = HTTP_GET;
        logo1_get.handler = logo1_get_handler;
        httpd_register_uri_handler(http_server, &logo1_get);

        // GET /logo2.jpg
        httpd_uri_t logo2_get = {};
        logo2_get.uri = "/logo2.jpg";
        logo2_get.method = HTTP_GET;
        logo2_get.handler = logo2_get_handler;
        httpd_register_uri_handler(http_server, &logo2_get);
    }

    portal_running = true;
    ESP_LOGI(TAG, "Captive portal started (AP: %s)", ap_ssid.c_str());
}

void WifiService::stopCaptivePortal()
{
    if (!portal_running)
        return;

    ESP_LOGI(TAG, "Manual stop captive portal");
    if (http_server)
    {
        httpd_stop(http_server);
        http_server = nullptr;
    }

    portal_running = false;
    ap_only_mode = false;
    cached_networks.clear();

    // Thử kết nối lại STA nếu có credentials cũ
    loadCredentials();
    if (!sta_ssid.empty())
    {
        esp_wifi_stop(); // Stop trước khi startSTA để reset mode
        vTaskDelay(pdMS_TO_TICKS(50));
        startSTA();
    }
}

void WifiService::disconnect()
{   
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_started = false;
    connected = false;
    if (status_cb)
        status_cb(0);
    ESP_LOGI(TAG, "WiFi disconnected");
}

void WifiService::disableAutoConnect()
{
    auto_connect_enabled = false;
}

std::string WifiService::getIp() const
{
    if (!sta_netif)
        return std::string();
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(sta_netif, &info) == ESP_OK)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
        return std::string(buf);
    }
    return std::string();
}

void WifiService::connectWithCredentials(const char *ssid, const char *pass)
{
    if (!ssid)
        return;

    ESP_LOGI(TAG, "connectWithCredentials: %s", ssid);

    // Lưu credentials trước
    sta_ssid = ssid;
    sta_pass = pass ? pass : "";
    saveCredentials(sta_ssid.c_str(), sta_pass.c_str());

    // Nếu portal đang chạy, dừng nó trước khi chuyển sang mode STA
    if (portal_running)
    {
        ESP_LOGI(TAG, "Stopping portal before STA connection");

        // Dừng HTTP server
        if (http_server)
        {
            httpd_stop(http_server);
            http_server = nullptr;
        }
        portal_running = false;
        ap_only_mode = false;

        // Reset WiFi driver để đảm bảo sạch sẽ (Clean state)
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    startSTA();
}

std::vector<WifiInfo> WifiService::scanNetworks()
{
    std::vector<WifiInfo> out;

    // Block scanning if portal or AP-only mode is active
    if (ap_only_mode || portal_running)
    {
        ESP_LOGW(TAG, "Scan blocked: portal/AP active");
        return {};
    }

    if (!wifi_started)
    {
        ESP_LOGW(TAG, "Scan blocked: wifi not started");
        return {};
    }

    wifi_scan_config_t cfg = {};
    cfg.ssid = nullptr;
    cfg.bssid = nullptr;
    cfg.channel = 0;
    cfg.show_hidden = true;

    esp_err_t e = esp_wifi_scan_start(&cfg, true); // blocking
    if (e != ESP_OK)
    {
        ESP_LOGW(TAG, "scan start failed: %d", e);
        return out;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0)
        return out;

    std::vector<wifi_ap_record_t> records(ap_num);
    esp_wifi_scan_get_ap_records(&ap_num, records.data());

    for (uint16_t i = 0; i < ap_num; i++)
    {
        WifiInfo wi;
        wi.ssid = reinterpret_cast<const char *>(records[i].ssid);
        wi.rssi = records[i].rssi;
        out.push_back(wi);
    }

    return out;
}

void WifiService::scanAndCache()
{
    cached_networks = scanNetworks();
    ESP_LOGI(TAG, "Scanned and cached %d networks", cached_networks.size());
}

void WifiService::ensureStaStarted()
{
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)
    {
        ESP_LOGI(TAG, "Switching WiFi to STA for scan");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }

    if (!wifi_started)
    {
        ESP_LOGI(TAG, "Starting WiFi for scan");
        ESP_ERROR_CHECK(esp_wifi_start());
        wifi_started = true;
    }
}

// --------------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------------
void WifiService::loadCredentials()
{
    std::string s, p;
    if (nvs_get_string("storage", "ssid", s))
    {
        sta_ssid = s;
    }
    if (nvs_get_string("storage", "pass", p))
    {
        sta_pass = p;
    }
    ESP_LOGI(TAG, "loadCredentials: Loaded SSID: %s, Pass: %s",
             sta_ssid.c_str(), sta_pass.empty() ? "<empty>" : "<set>");
}

void WifiService::saveCredentials(const char *ssid, const char *pass)
{
    nvs_set_string("storage", "ssid", ssid ? ssid : "");
    nvs_set_string("storage", "pass", pass ? pass : "");
}

void WifiService::startSTA()
{
    if (ap_only_mode)
    {
        ESP_LOGI(TAG, "AP-only mode enabled; ignoring STA start request");
        return;
    }

    ESP_LOGI(TAG, "startSTA: Configuring WiFi STA mode (SSID: %s, Pass: %s)",
             sta_ssid.c_str(), sta_pass.empty() ? "<empty>" : "<set>");

    wifi_config_t cfg = {};
    strncpy(reinterpret_cast<char *>(cfg.sta.ssid), sta_ssid.c_str(), sizeof(cfg.sta.ssid) - 1);
    strncpy(reinterpret_cast<char *>(cfg.sta.password), sta_pass.c_str(), sizeof(cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_started = true;
    ESP_LOGI(TAG, "WiFi STA started. Connecting to SSID: %s (password: %s)",
             sta_ssid.c_str(), sta_pass.empty() ? "<empty>" : "<set>");
    esp_wifi_connect();

    if (status_cb)
        status_cb(1); // CONNECTING
}

void WifiService::registerEvents()
{
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiService::wifiEventHandlerStatic,
                                                        this,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiService::ipEventHandlerStatic,
                                                        this,
                                                        &instance_got_ip));
}

// Static event trampoline
void WifiService::wifiEventHandlerStatic(void *arg, esp_event_base_t base,
                                         int32_t id, void *data)
{
    WifiService *s = static_cast<WifiService *>(arg);
    if (s)
        s->wifiEventHandler(base, id, data);
}

void WifiService::ipEventHandlerStatic(void *arg, esp_event_base_t base,
                                       int32_t id, void *data)
{
    WifiService *s = static_cast<WifiService *>(arg);
    if (s)
        s->ipEventHandler(base, id, data);
}

void WifiService::wifiEventHandler(esp_event_base_t base, int32_t id, void *data)
{
    if (ap_only_mode)
        return;

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        connected = false;
        if (status_cb)
            status_cb(0);
        if (auto_connect_enabled && !sta_ssid.empty())
        {
            esp_wifi_connect();
        }
        break;
    default:
        break;
    }
}

void WifiService::ipEventHandler(esp_event_base_t base, int32_t id, void *event)
{
    if (ap_only_mode)
        return;

    if (id == IP_EVENT_STA_GOT_IP)
    {
        connected = true;
        has_connected_once = true;
        if (status_cb)
            status_cb(2);
        if (portal_running)
        {
            // Stop portal if running
            stopCaptivePortal();
        }
        ESP_LOGI(TAG, "Got IP - WiFi connected");
    }
}
