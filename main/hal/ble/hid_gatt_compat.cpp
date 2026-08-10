#include <cstddef>
#include <cstdint>
#include <atomic>

#include <esp_gatt_defs.h>
#include <esp_gatts_api.h>
#include <esp_hidd.h>
#include <esp_hidd_gatts.h>
#include <esp_log.h>

namespace {

constexpr const char* Tag = "CodexMicro-GATT";

std::atomic<uint16_t> HidInputHandle = 0;
std::atomic<uint16_t> HidInputCccHandle = 0;
std::atomic<uint16_t> HidConnectionId = 0;
std::atomic<esp_gatt_if_t> HidGattIf = ESP_GATT_IF_NONE;
std::atomic_bool HidConnected = false;
std::atomic_bool HidNotificationsEnabled = false;
uint16_t PendingInputIndex = 0;
uint16_t PendingInputCccIndex = 0;

bool hasUuid16(const esp_attr_desc_t& attribute, uint16_t expected)
{
    if (attribute.uuid_length != ESP_UUID_LEN_16 || attribute.uuid_p == nullptr) {
        return false;
    }
    const uint16_t actual =
        static_cast<uint16_t>(attribute.uuid_p[0]) | (static_cast<uint16_t>(attribute.uuid_p[1]) << 8U);
    return actual == expected;
}

bool isWritableCharacteristic(const esp_gatts_attr_db_t& declaration)
{
    const esp_attr_desc_t& attribute = declaration.att_desc;
    if (!hasUuid16(attribute, ESP_GATT_UUID_CHAR_DECLARE) || attribute.value == nullptr || attribute.length < 1) {
        return false;
    }
    const uint8_t properties = attribute.value[0];
    return (properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) != 0;
}

bool isNotifyingCharacteristic(const esp_gatts_attr_db_t& declaration)
{
    const esp_attr_desc_t& attribute = declaration.att_desc;
    if (!hasUuid16(attribute, ESP_GATT_UUID_CHAR_DECLARE) || attribute.value == nullptr || attribute.length < 1) {
        return false;
    }
    return (attribute.value[0] & ESP_GATT_CHAR_PROP_BIT_NOTIFY) != 0;
}

bool isHidServiceTable(const esp_gatts_attr_db_t* database, uint16_t count)
{
    if (database == nullptr || count == 0) {
        return false;
    }
    const esp_attr_desc_t& service = database[0].att_desc;
    if (!hasUuid16(service, ESP_GATT_UUID_PRI_SERVICE) || service.value == nullptr || service.length < 2) {
        return false;
    }
    const uint16_t uuid = static_cast<uint16_t>(service.value[0]) | (static_cast<uint16_t>(service.value[1]) << 8U);
    return uuid == ESP_GATT_UUID_HID_SVC;
}

}  // namespace

extern "C" esp_err_t __real_esp_ble_gatts_create_attr_tab(const esp_gatts_attr_db_t* gatts_attr_db,
                                                          esp_gatt_if_t gatts_if, uint16_t max_nb_attr,
                                                          uint8_t srvc_inst_id);

extern "C" void codex_micro_hid_gatt_compat_link_anchor()
{
}

extern "C" void codex_micro_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                                  esp_ble_gatts_cb_param_t* param)
{
    if (param != nullptr) {
        if (event == ESP_GATTS_CREAT_ATTR_TAB_EVT && gatts_if == HidGattIf.load() && PendingInputIndex > 0 &&
            param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle > PendingInputCccIndex) {
            HidInputHandle.store(param->add_attr_tab.handles[PendingInputIndex]);
            HidInputCccHandle.store(param->add_attr_tab.handles[PendingInputCccIndex]);
            ESP_LOGI(Tag, "captured HID input handle=%u ccc=%u", static_cast<unsigned>(HidInputHandle.load()),
                     static_cast<unsigned>(HidInputCccHandle.load()));
        } else if (event == ESP_GATTS_CONNECT_EVT && gatts_if == HidGattIf.load()) {
            HidConnectionId.store(param->connect.conn_id);
            HidConnected.store(true);
            HidNotificationsEnabled.store(false);
        } else if (event == ESP_GATTS_DISCONNECT_EVT && gatts_if == HidGattIf.load()) {
            HidConnected.store(false);
            HidNotificationsEnabled.store(false);
        } else if (event == ESP_GATTS_WRITE_EVT && gatts_if == HidGattIf.load() &&
                   param->write.handle == HidInputCccHandle.load() && param->write.len >= 2 &&
                   param->write.value != nullptr) {
            const uint16_t ccc = static_cast<uint16_t>(param->write.value[0]) |
                                 (static_cast<uint16_t>(param->write.value[1]) << 8U);
            HidNotificationsEnabled.store((ccc & 0x0001U) != 0);
            ESP_LOGI(Tag, "Windows HID notification subscription=%u", static_cast<unsigned>(ccc));
        }
    }

    esp_hidd_gatts_event_handler(event, gatts_if, param);
}

extern "C" esp_err_t codex_micro_hid_input_set(esp_hidd_dev_t* device, size_t map_index, size_t report_id,
                                                 uint8_t* data, size_t length)
{
    const esp_err_t normal = esp_hidd_dev_input_set(device, map_index, report_id, data, length);
    if (normal == ESP_OK || !HidConnected.load() || HidGattIf.load() == ESP_GATT_IF_NONE ||
        HidInputHandle.load() == 0) {
        return normal;
    }

    // Windows persists the BLE HID subscription for bonded devices, while the
    // ESP-IDF HID helper clears its in-memory CCC flag on every device reboot.
    // The host still expects notifications, so bypass only that stale local
    // guard and send through the exact Input Report characteristic it created.
    const esp_err_t fallback = esp_ble_gatts_send_indicate(HidGattIf.load(), HidConnectionId.load(),
                                                            HidInputHandle.load(), length, data, false);
    if (fallback == ESP_OK) {
        ESP_LOGI(Tag, "sent HID input using bonded-Windows notification fallback (ccc=%d)",
                 HidNotificationsEnabled.load() ? 1 : 0);
        return ESP_OK;
    }
    ESP_LOGE(Tag, "Windows HID notification fallback failed: %s", esp_err_to_name(fallback));
    return normal;
}

extern "C" esp_err_t __wrap_esp_ble_gatts_create_attr_tab(const esp_gatts_attr_db_t* gatts_attr_db,
                                                          esp_gatt_if_t gatts_if, uint16_t max_nb_attr,
                                                          uint8_t srvc_inst_id)
{
    // node-hid passes the non-zero Report ID in its 64-byte SetReport buffer.
    // macOS forwards all 64 bytes to the BLE Output Report characteristic. The
    // ESP-IDF HID helper sizes that characteristic from the 63-byte report body,
    // so the ATT server rejects the otherwise valid write with error 0x0D
    // (Invalid Attribute Value Length). Keep the HID descriptor unchanged and
    // widen only the writable HID Report characteristic by the Report ID byte.
    if (gatts_attr_db != nullptr) {
        auto* mutable_db = const_cast<esp_gatts_attr_db_t*>(gatts_attr_db);
        const bool hid_service = isHidServiceTable(gatts_attr_db, max_nb_attr);
        for (uint16_t index = 1; index < max_nb_attr; ++index) {
            esp_attr_desc_t& attribute = mutable_db[index].att_desc;
            if (hid_service && hasUuid16(attribute, ESP_GATT_UUID_HID_REPORT) &&
                isNotifyingCharacteristic(mutable_db[index - 1]) && index + 1 < max_nb_attr &&
                hasUuid16(mutable_db[index + 1].att_desc, ESP_GATT_UUID_CHAR_CLIENT_CONFIG)) {
                HidGattIf.store(gatts_if);
                PendingInputIndex = index;
                PendingInputCccIndex = index + 1;
            }
            if (hasUuid16(attribute, ESP_GATT_UUID_HID_REPORT) && isWritableCharacteristic(mutable_db[index - 1]) &&
                attribute.max_length == 63) {
                attribute.max_length = 64;
                ESP_LOGI(Tag, "expanded HID output report characteristic to 64 bytes");
            }
        }
    }

    return __real_esp_ble_gatts_create_attr_tab(gatts_attr_db, gatts_if, max_nb_attr, srvc_inst_id);
}
