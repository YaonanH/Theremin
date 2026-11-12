#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"
#include "theremin_config.h"
#include "web_monitor.h"
#include "calibration_web.h"

_Static_assert(sizeof(WIFI_AP_SSID) > 1 && sizeof(WIFI_AP_SSID) <= 33,
               "WIFI_AP_SSID must contain 1-32 bytes");
_Static_assert(sizeof(WIFI_AP_PASSWORD) >= 9 && sizeof(WIFI_AP_PASSWORD) <= 64,
               "WIFI_AP_PASSWORD must contain 8-63 bytes");

static const char *TAG = "web_monitor";
static QueueHandle_t s_status;
static httpd_handle_t s_server;

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
extern const unsigned char css_start[] asm("_binary_dashboard_css_start");
extern const unsigned char css_end[] asm("_binary_dashboard_css_end");
extern const unsigned char js_start[] asm("_binary_dashboard_js_start");
extern const unsigned char js_end[] asm("_binary_dashboard_js_end");

typedef struct {
    const unsigned char *start;
    const unsigned char *end;
    const char *content_type;
} web_asset_t;

static const web_asset_t s_assets[] = {
    {index_start, index_end, "text/html; charset=utf-8"},
    {css_start, css_end, "text/css; charset=utf-8"},
    {js_start, js_end, "text/javascript; charset=utf-8"},
};

static void response_headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self'; "
        "connect-src 'self'; img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'");
}

static esp_err_t serve_asset(httpd_req_t *request)
{
    const web_asset_t *asset = request->user_ctx;
    response_headers(request);
    httpd_resp_set_type(request, asset->content_type);
    return httpd_resp_send(request, (const char *)asset->start, asset->end - asset->start);
}

static esp_err_t serve_status(httpd_req_t *request)
{
    web_status_frame_t frame;
    bool have_frame = xQueuePeek(s_status, &frame, 0) == pdTRUE;
    char json[512];
    int length = web_status_format(json, sizeof(json), have_frame ? &frame : NULL,
                                   esp_timer_get_time());
    response_headers(request);
    httpd_resp_set_type(request, "application/json");
    if (length < 0) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Status unavailable");
    }
    return httpd_resp_send(request, json, length);
}

void web_monitor_publish(const web_status_frame_t *frame)
{
    if (s_status && frame) {
        xQueueOverwrite(s_status, frame);
    }
}

bool web_monitor_snapshot(web_status_frame_t *frame)
{
    return s_status && xQueuePeek(s_status, frame, 0) == pdTRUE;
}

esp_err_t web_monitor_start(void)
{
    if (s_status) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret;
    esp_netif_t *ap = NULL;
    bool wifi_initialized = false;
    bool wifi_started = false;
    s_status = xQueueCreate(1, sizeof(web_status_frame_t));
    ESP_RETURN_ON_FALSE(s_status, ESP_ERR_NO_MEM, TAG, "No memory for dashboard readings");

    /* Keep existing NVS contents on errors; never erase someone else's settings. */
    ESP_GOTO_ON_ERROR(nvs_flash_init(), fail, TAG, "NVS unavailable; dashboard disabled");
    ESP_GOTO_ON_ERROR(esp_netif_init(), fail, TAG, "Network initialization failed");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    ap = esp_netif_create_default_wifi_ap();
    if (!ap) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_GOTO_ON_ERROR(esp_wifi_init(&wifi_init), fail, TAG, "Wi-Fi initialization failed");
    wifi_initialized = true;
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = sizeof(WIFI_AP_SSID) - 1,
            .password = WIFI_AP_PASSWORD,
            .channel = WIFI_AP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };
    ESP_GOTO_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), fail, TAG, "Wi-Fi storage setup failed");
    ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), fail, TAG, "AP mode setup failed");
    ESP_GOTO_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), fail, TAG, "AP setup failed");
    ESP_GOTO_ON_ERROR(esp_wifi_start(), fail, TAG, "Wi-Fi startup failed");
    wifi_started = true;

    httpd_config_t server = HTTPD_DEFAULT_CONFIG();
    server.task_priority = 2; /* Below the audio task's priority 5. */
    server.stack_size = 8192;
    server.max_uri_handlers = 12;
    server.max_open_sockets = 3;
    server.lru_purge_enable = true;
    server.recv_wait_timeout = 2;
    server.send_wait_timeout = 2;
    ESP_GOTO_ON_ERROR(httpd_start(&s_server, &server), fail, TAG, "HTTP startup failed");
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[0]},
        {.uri = "/dashboard.css", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[1]},
        {.uri = "/dashboard.js", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[2]},
        {.uri = "/api/status", .method = HTTP_GET, .handler = serve_status},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_GOTO_ON_ERROR(httpd_register_uri_handler(s_server, &routes[i]), fail, TAG,
                          "HTTP route registration failed");
    }
    ESP_GOTO_ON_ERROR(calibration_web_register(s_server), fail, TAG, "Calibration routes failed");
    esp_netif_ip_info_t ip;
    ESP_GOTO_ON_ERROR(esp_netif_get_ip_info(ap, &ip), fail, TAG, "Cannot read AP address");
    ESP_LOGI(TAG, "Connect to Wi-Fi '%s', then open http://" IPSTR,
             WIFI_AP_SSID, IP2STR(&ip.ip));
    return ESP_OK;

fail:
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (wifi_started) {
        esp_wifi_stop();
    }
    if (wifi_initialized) {
        esp_wifi_deinit();
    }
    if (ap) {
        esp_netif_destroy_default_wifi(ap);
    }
    vQueueDelete(s_status);
    s_status = NULL;
    return ret;
}
