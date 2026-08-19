/*
 * SPDX-License-Identifier: MIT
 */
#include "usage_link.h"

#include <hal/utils/settings/settings.h>

#include <cJSON.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace usage_link {

namespace {

constexpr const char* Tag                = "UsageLink";
constexpr const char* SettingsNs         = "usagelink";
constexpr uint32_t PollIntervalMs        = 30000;
constexpr uint32_t RetryIntervalMs       = 10000;
constexpr uint32_t HttpTimeoutMs         = 5000;
constexpr std::size_t ResponseLimit      = 2048;
constexpr uint16_t DiscoveryPort         = 8788;
constexpr uint32_t DiscoveryTimeoutMs    = 1500;
constexpr const char* DiscoveryRequest   = "STOPWATCH_DISCOVER_V1";
constexpr const char* DiscoveryResponse  = "STOPWATCH_COMPANION_V1";

struct ActionRequest {
    char target[12];
    char action[16];
};

}  // namespace

class UsageLinkImpl {
public:
    void begin();
    Snapshot snapshot();
    bool requestAction(const char* target, const char* action);
    void setWifiConfig(const char* ssid, const char* password);
    void setHostConfig(const char* host, uint16_t port);
    void clearConfig();
    bool copyCompanionHost(char* host, std::size_t capacity) const;
    bool configured() const
    {
        return _configured;
    }

private:
    static void taskEntry(void* context);
    static void wifiEventHandler(void* context, esp_event_base_t base, int32_t id, void* data);

    void run();
    bool pollUsage();
    bool discoverCompanion();
    bool postAction(const ActionRequest& request);
    bool httpRequest(const char* path, const char* postBody, char* response, std::size_t responseCapacity);
    void parseUsageJson(const char* json);
    static void parseMeter(const cJSON* node, Meter& meter);

    Snapshot _state;
    SemaphoreHandle_t _state_mutex = nullptr;
    QueueHandle_t _action_queue    = nullptr;
    bool _configured               = false;
    char _ssid[33]                 = {};
    char _password[65]             = {};
    char _host[40]                 = {};
    uint16_t _port                 = 8787;
    volatile bool _got_ip          = false;
};

namespace {
UsageLinkImpl g_impl;
}

/* --------------------------------- config --------------------------------- */

void UsageLinkImpl::setWifiConfig(const char* ssid, const char* password)
{
    Settings settings(SettingsNs, true);
    settings.SetString("ssid", ssid == nullptr ? "" : ssid);
    settings.SetString("pass", password == nullptr ? "" : password);
}

void UsageLinkImpl::setHostConfig(const char* host, uint16_t port)
{
    Settings settings(SettingsNs, true);
    settings.SetString("host", host == nullptr ? "" : host);
    settings.SetInt("port", static_cast<int32_t>(port));
}

void UsageLinkImpl::clearConfig()
{
    Settings settings(SettingsNs, true);
    settings.EraseAll();
}

/* --------------------------------- startup -------------------------------- */

void UsageLinkImpl::begin()
{
    _state_mutex  = xSemaphoreCreateMutex();
    _action_queue = xQueueCreate(8, sizeof(ActionRequest));
    if (_state_mutex == nullptr || _action_queue == nullptr) {
        ESP_LOGE(Tag, "allocation failed; usage link disabled");
        return;
    }

    {
        Settings settings(SettingsNs, false);
        const std::string ssid = settings.GetString("ssid");
        const std::string pass = settings.GetString("pass");
        const std::string host = settings.GetString("host");
        const int32_t port     = settings.GetInt("port", 8787);
        if (ssid.empty() || host.empty()) {
            ESP_LOGI(Tag, "not configured; run 'debug wifi <ssid> <pass>' and 'debug host <ip>'");
            return;
        }
        std::snprintf(_ssid, sizeof(_ssid), "%s", ssid.c_str());
        std::snprintf(_password, sizeof(_password), "%s", pass.c_str());
        std::snprintf(_host, sizeof(_host), "%s", host.c_str());
        _port = static_cast<uint16_t>(port <= 0 || port > 65535 ? 8787 : port);
    }
    _configured = true;
    {
        // snapshot() readers see the configured flag immediately.
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _state.configured = true;
        xSemaphoreGive(_state_mutex);
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(Tag, "netif init failed: %d", static_cast<int>(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(Tag, "event loop failed: %d", static_cast<int>(err));
        return;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err                            = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        ESP_LOGE(Tag, "wifi init failed: %d", static_cast<int>(err));
        return;
    }
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &UsageLinkImpl::wifiEventHandler, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &UsageLinkImpl::wifiEventHandler, this, nullptr);

    wifi_config_t wifi_config = {};
    // memcpy with clamped length: the zero-initialized fields stay terminated
    // and -Werror=format-truncation has nothing to complain about.
    std::memcpy(wifi_config.sta.ssid, _ssid, std::min(std::strlen(_ssid), sizeof(wifi_config.sta.ssid) - 1));
    std::memcpy(wifi_config.sta.password, _password,
                std::min(std::strlen(_password), sizeof(wifi_config.sta.password) - 1));
    wifi_config.sta.scan_method       = WIFI_FAST_SCAN;
    wifi_config.sta.failure_retry_cnt = 3;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    // Modem power save keeps the shared 2.4GHz radio friendly to BLE HID.
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(Tag, "wifi start failed: %d", static_cast<int>(err));
        return;
    }

    // CPU0 with the radio stacks; LVGL rendering stays alone on CPU1.
    const BaseType_t created =
        xTaskCreatePinnedToCore(&UsageLinkImpl::taskEntry, "usage_link", 6144, this, 3, nullptr, 0);
    if (created != pdPASS) {
        ESP_LOGE(Tag, "worker task creation failed");
        return;
    }
    ESP_LOGI(Tag, "started; host=%s:%u ssid=%s", _host, static_cast<unsigned>(_port), _ssid);
}

void UsageLinkImpl::wifiEventHandler(void* context, esp_event_base_t base, int32_t id, void* data)
{
    auto* self = static_cast<UsageLinkImpl*>(context);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->_got_ip = false;
        // Unconditional reconnect keeps the link resilient to AP restarts.
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        self->_got_ip = true;
    }
}

/* ---------------------------------- state ---------------------------------- */

Snapshot UsageLinkImpl::snapshot()
{
    Snapshot copy;
    if (_state_mutex == nullptr) {
        return copy;
    }
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    copy = _state;
    xSemaphoreGive(_state_mutex);
    copy.wifiConnected = _got_ip;
    return copy;
}

bool UsageLinkImpl::requestAction(const char* target, const char* action)
{
    if (!_configured || _action_queue == nullptr || target == nullptr || action == nullptr) {
        return false;
    }
    ActionRequest request = {};
    std::snprintf(request.target, sizeof(request.target), "%s", target);
    std::snprintf(request.action, sizeof(request.action), "%s", action);
    return xQueueSend(_action_queue, &request, 0) == pdTRUE;
}

bool UsageLinkImpl::copyCompanionHost(char* host, std::size_t capacity) const
{
    if (!_configured || !_got_ip || host == nullptr || capacity == 0 || _state_mutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    const bool available = _host[0] != '\0';
    if (available) {
        std::snprintf(host, capacity, "%s", _host);
    }
    xSemaphoreGive(_state_mutex);
    return available;
}

/* --------------------------------- worker ---------------------------------- */

void UsageLinkImpl::taskEntry(void* context)
{
    static_cast<UsageLinkImpl*>(context)->run();
}

void UsageLinkImpl::run()
{
    uint32_t next_poll_ms = 0;
    while (true) {
        ActionRequest request = {};
        // Wake immediately for control actions; otherwise tick every 250ms.
        if (xQueueReceive(_action_queue, &request, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (_got_ip) {
                postAction(request);
            }
        }
        const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
        if (now < next_poll_ms) {
            continue;
        }
        if (!_got_ip) {
            next_poll_ms = now + 1000;
            continue;
        }
        const bool ok = pollUsage();
        next_poll_ms  = now + (ok ? PollIntervalMs : RetryIntervalMs);
    }
}

bool UsageLinkImpl::pollUsage()
{
    static char response[ResponseLimit];
    if (!httpRequest("/usage", nullptr, response, sizeof(response))) {
        if (discoverCompanion() && httpRequest("/usage", nullptr, response, sizeof(response))) {
            parseUsageJson(response);
            return true;
        }
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _state.linkOk = false;
        xSemaphoreGive(_state_mutex);
        return false;
    }
    parseUsageJson(response);
    return true;
}

bool UsageLinkImpl::discoverCompanion()
{
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGW(Tag, "discovery socket creation failed: errno=%d", errno);
        return false;
    }

    const int broadcast_enabled = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enabled, sizeof(broadcast_enabled));
    timeval timeout = {
        .tv_sec  = static_cast<time_t>(DiscoveryTimeoutMs / 1000),
        .tv_usec = static_cast<suseconds_t>((DiscoveryTimeoutMs % 1000) * 1000),
    };
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in destination     = {};
    destination.sin_family      = AF_INET;
    destination.sin_port        = htons(DiscoveryPort);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const auto send_discovery = [&](uint32_t address) {
        destination.sin_addr.s_addr = address;
        return sendto(socket_fd, DiscoveryRequest, std::strlen(DiscoveryRequest), 0,
                      reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
    };

    bool sent_to_current_subnet   = false;
    esp_netif_t* station          = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info   = {};
    if (station != nullptr && esp_netif_get_ip_info(station, &ip_info) == ESP_OK) {
        const uint32_t directed_broadcast = ip_info.ip.addr | ~ip_info.netmask.addr;
        send_discovery(directed_broadcast);
        sent_to_current_subnet = true;
    }
    if (!sent_to_current_subnet) {
        send_discovery(htonl(INADDR_BROADCAST));
    }

    char response[64]       = {};
    sockaddr_in source      = {};
    socklen_t source_length = sizeof(source);
    const int received = recvfrom(socket_fd, response, sizeof(response) - 1, 0,
                                  reinterpret_cast<sockaddr*>(&source), &source_length);
    close(socket_fd);
    if (received <= 0) {
        ESP_LOGW(Tag, "companion discovery timed out");
        return false;
    }
    response[received] = '\0';

    unsigned discovered_port = 0;
    char protocol[40]         = {};
    if (std::sscanf(response, "%39s %u", protocol, &discovered_port) != 2 ||
        std::strcmp(protocol, DiscoveryResponse) != 0 || discovered_port == 0 || discovered_port > 65535) {
        ESP_LOGW(Tag, "ignored invalid discovery response: %s", response);
        return false;
    }

    char discovered_host[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &source.sin_addr, discovered_host, sizeof(discovered_host)) == nullptr) {
        return false;
    }

    bool changed = false;
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    if (std::strcmp(_host, discovered_host) != 0 || _port != discovered_port) {
        std::snprintf(_host, sizeof(_host), "%s", discovered_host);
        _port   = static_cast<uint16_t>(discovered_port);
        changed = true;
    }
    xSemaphoreGive(_state_mutex);

    if (changed) {
        setHostConfig(discovered_host, static_cast<uint16_t>(discovered_port));
    }
    ESP_LOGI(Tag, "companion discovered at %s:%u%s", discovered_host, discovered_port,
             changed ? " and saved" : "");
    return true;
}

bool UsageLinkImpl::postAction(const ActionRequest& request)
{
    char body[64] = {};
    std::snprintf(body, sizeof(body), "{\"target\":\"%s\",\"action\":\"%s\"}", request.target, request.action);
    char response[128];
    const bool ok = httpRequest("/control", body, response, sizeof(response));
    ESP_LOGI(Tag, "action %s/%s -> %s", request.target, request.action, ok ? "ok" : "failed");
    return ok;
}

bool UsageLinkImpl::httpRequest(const char* path, const char* postBody, char* response, std::size_t responseCapacity)
{
    char url[96] = {};
    std::snprintf(url, sizeof(url), "http://%s:%u%s", _host, static_cast<unsigned>(_port), path);

    esp_http_client_config_t config = {};
    config.url                      = url;
    config.timeout_ms               = HttpTimeoutMs;
    config.method                   = postBody == nullptr ? HTTP_METHOD_GET : HTTP_METHOD_POST;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    bool ok = false;
    do {
        if (postBody != nullptr) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
        }
        if (esp_http_client_open(client, postBody == nullptr ? 0 : static_cast<int>(std::strlen(postBody))) != ESP_OK) {
            break;
        }
        if (postBody != nullptr) {
            const int body_length = static_cast<int>(std::strlen(postBody));
            if (esp_http_client_write(client, postBody, body_length) != body_length) {
                break;
            }
        }
        if (esp_http_client_fetch_headers(client) < 0) {
            break;
        }
        const int status = esp_http_client_get_status_code(client);
        const int read   = esp_http_client_read_response(client, response, static_cast<int>(responseCapacity) - 1);
        if (read < 0 || status != 200) {
            break;
        }
        response[read] = '\0';
        ok             = true;
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* --------------------------------- parsing --------------------------------- */

void UsageLinkImpl::parseMeter(const cJSON* node, Meter& meter)
{
    meter = Meter{};
    if (node == nullptr) {
        return;
    }
    const cJSON* pct   = cJSON_GetObjectItemCaseSensitive(node, "pct");
    const cJSON* reset = cJSON_GetObjectItemCaseSensitive(node, "reset");
    if (!cJSON_IsNumber(pct)) {
        return;
    }
    double value = pct->valuedouble;
    if (value < 0.0) {
        value = 0.0;
    }
    if (value > 100.0) {
        value = 100.0;
    }
    meter.valid   = true;
    meter.percent = static_cast<uint8_t>(value + 0.5);
    if (cJSON_IsString(reset) && reset->valuestring != nullptr) {
        std::snprintf(meter.reset, sizeof(meter.reset), "%s", reset->valuestring);
    }
}

void UsageLinkImpl::parseUsageJson(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr) {
        ESP_LOGW(Tag, "usage JSON parse failed");
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        _state.linkOk = false;
        xSemaphoreGive(_state_mutex);
        return;
    }

    Meter claude_session, claude_week, codex_session, codex_week;
    const cJSON* claude = cJSON_GetObjectItemCaseSensitive(root, "claude");
    const cJSON* codex  = cJSON_GetObjectItemCaseSensitive(root, "codex");
    if (claude != nullptr) {
        parseMeter(cJSON_GetObjectItemCaseSensitive(claude, "session"), claude_session);
        parseMeter(cJSON_GetObjectItemCaseSensitive(claude, "week"), claude_week);
    }
    if (codex != nullptr) {
        parseMeter(cJSON_GetObjectItemCaseSensitive(codex, "session"), codex_session);
        parseMeter(cJSON_GetObjectItemCaseSensitive(codex, "week"), codex_week);
    }
    cJSON_Delete(root);

    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    _state.linkOk        = true;
    _state.lastUpdateMs  = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    _state.claudeSession = claude_session;
    _state.claudeWeek    = claude_week;
    _state.codexSession  = codex_session;
    _state.codexWeek     = codex_week;
    xSemaphoreGive(_state_mutex);
}

/* -------------------------------- facade ----------------------------------- */

void UsageLink::begin()
{
    g_impl.begin();
}

Snapshot UsageLink::snapshot()
{
    return g_impl.snapshot();
}

bool UsageLink::requestAction(const char* target, const char* action)
{
    return g_impl.requestAction(target, action);
}

void UsageLink::setWifiConfig(const char* ssid, const char* password)
{
    g_impl.setWifiConfig(ssid, password);
}

void UsageLink::setHostConfig(const char* host, uint16_t port)
{
    g_impl.setHostConfig(host, port);
}

void UsageLink::clearConfig()
{
    g_impl.clearConfig();
}

bool UsageLink::configured() const
{
    return g_impl.configured();
}

bool UsageLink::copyCompanionHost(char* host, std::size_t capacity) const
{
    return g_impl.copyCompanionHost(host, capacity);
}

UsageLink& GetUsageLink()
{
    static UsageLink instance;
    return instance;
}

}  // namespace usage_link
