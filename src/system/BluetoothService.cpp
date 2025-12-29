#include "BluetoothService.hpp"
#include <string>
#include <cstring>

// NimBLE core
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#undef min
#undef max

static const char* BLE_TAG = "NimBLE_SVC";

// Khởi tạo static members
BluetoothService::ConfigData BluetoothService::temp_cfg;
BluetoothService::OnConfigComplete BluetoothService::config_cb = nullptr;
std::string BluetoothService::s_adv_name = "PTalk";

// ============================================================================
// KHỞI TẠO UUID THỦ CÔNG (Sửa lỗi expected primary-expression)
// ============================================================================
static const ble_uuid16_t g_svc_uuid      = { {BLE_UUID_TYPE_16}, BluetoothService::SVC_UUID_CONFIG };
static const ble_uuid16_t g_chr_name_uuid = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_DEVICE_NAME };
static const ble_uuid16_t g_chr_vol_uuid  = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_VOLUME };
static const ble_uuid16_t g_chr_bri_uuid  = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_BRIGHTNESS };
static const ble_uuid16_t g_chr_ssid_uuid = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_WIFI_SSID };
static const ble_uuid16_t g_chr_pass_uuid = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_WIFI_PASS };
static const ble_uuid16_t g_chr_ver_uuid  = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_APP_VERSION };
static const ble_uuid16_t g_chr_info_uuid = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_BUILD_INFO };
static const ble_uuid16_t g_chr_save_uuid = { {BLE_UUID_TYPE_16}, BluetoothService::CHR_UUID_SAVE_CMD };

// ============================================================================
// GATT ACCESS CALLBACK
// ============================================================================
int BluetoothService::gatt_svr_access(uint16_t conn_handle, uint16_t attr_handle, 
                                      struct ble_gatt_access_ctxt *ctxt, void *arg) 
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (uuid16) {
            case CHR_UUID_APP_VERSION:
                return os_mbuf_append(ctxt->om, app_meta::APP_VERSION, strlen(app_meta::APP_VERSION));
            
            case CHR_UUID_BUILD_INFO: {
                std::string info = std::string(app_meta::DEVICE_MODEL) + " (" + app_meta::BUILD_DATE + ")";
                return os_mbuf_append(ctxt->om, info.c_str(), info.length());
            }

            case CHR_UUID_DEVICE_NAME:
                return os_mbuf_append(ctxt->om, temp_cfg.device_name.c_str(), temp_cfg.device_name.length());

            case CHR_UUID_VOLUME:
                return os_mbuf_append(ctxt->om, &temp_cfg.volume, 1);

            case CHR_UUID_BRIGHTNESS:
                return os_mbuf_append(ctxt->om, &temp_cfg.brightness, 1);

            default: return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        switch (uuid16) {
            case CHR_UUID_DEVICE_NAME:
                temp_cfg.device_name.assign((char*)ctxt->om->om_data, ctxt->om->om_len);
                break;
            case CHR_UUID_VOLUME:
                temp_cfg.volume = ctxt->om->om_data[0];
                break;
            case CHR_UUID_BRIGHTNESS:
                temp_cfg.brightness = ctxt->om->om_data[0];
                break;
            case CHR_UUID_WIFI_SSID:
                temp_cfg.ssid.assign((char*)ctxt->om->om_data, ctxt->om->om_len);
                break;
            case CHR_UUID_WIFI_PASS:
                temp_cfg.pass.assign((char*)ctxt->om->om_data, ctxt->om->om_len);
                break;
            case CHR_UUID_SAVE_CMD:
                if (ctxt->om->om_len > 0 && ctxt->om->om_data[0] == 1) {
                    if (config_cb) config_cb(temp_cfg);
                }
                break;
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// ============================================================================
// GATT TABLE (Sử dụng con trỏ đến các biến tĩnh g_...)
// ============================================================================
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (const ble_uuid_t *)&g_svc_uuid,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = (const ble_uuid_t *)&g_chr_name_uuid, .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {.uuid = (const ble_uuid_t *)&g_chr_vol_uuid,  .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {.uuid = (const ble_uuid_t *)&g_chr_bri_uuid,  .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
            {.uuid = (const ble_uuid_t *)&g_chr_ssid_uuid, .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_WRITE},
            {.uuid = (const ble_uuid_t *)&g_chr_pass_uuid, .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_WRITE},
            {.uuid = (const ble_uuid_t *)&g_chr_ver_uuid,  .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = (const ble_uuid_t *)&g_chr_info_uuid, .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = (const ble_uuid_t *)&g_chr_save_uuid, .access_cb = BluetoothService::gatt_svr_access, .flags = BLE_GATT_CHR_F_WRITE},
            {0}
        },
    },
    {0}
};

// ============================================================================
// VÒNG ĐỜI
// ============================================================================
BluetoothService::BluetoothService() = default;
BluetoothService::~BluetoothService() { stop(); }

void BluetoothService::on_stack_sync() {
    ble_svc_gap_device_name_set(s_adv_name.c_str());
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*)s_adv_name.c_str();
    fields.name_len = (uint8_t)s_adv_name.length();
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    ESP_LOGI(BLE_TAG, "Advertising Started");
}

bool BluetoothService::init(const std::string& adv_name) {
    s_adv_name = adv_name;
    nimble_port_init();
    ble_hs_cfg.sync_cb = BluetoothService::on_stack_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);
    return true;
}

void BluetoothService::start() {
    if (started) return;
    started = true;
    xTaskCreatePinnedToCore([](void* arg){ nimble_port_run(); }, "nimble", 4096, NULL, 5, NULL, 0);
}

void BluetoothService::stop() {
    if (!started) return;
    nimble_port_stop();
    nimble_port_deinit();
    started = false;
}