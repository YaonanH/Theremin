#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "calibration.h"
#include "calibration_protocol.h"
#include "calibration_web.h"
#include "web_monitor.h"

extern const unsigned char cal_html_start[] asm("_binary_calibration_html_start");
extern const unsigned char cal_html_end[] asm("_binary_calibration_html_end");
extern const unsigned char cal_css_start[] asm("_binary_calibration_css_start");
extern const unsigned char cal_css_end[] asm("_binary_calibration_css_end");
extern const unsigned char cal_js_start[] asm("_binary_calibration_js_start");
extern const unsigned char cal_js_end[] asm("_binary_calibration_js_end");
typedef struct { const unsigned char *start, *end; const char *type; } asset_t;
static const asset_t s_assets[] = {
    {cal_html_start, cal_html_end, "text/html; charset=utf-8"},
    {cal_css_start, cal_css_end, "text/css; charset=utf-8"},
    {cal_js_start, cal_js_end, "text/javascript; charset=utf-8"},
};

static void headers(httpd_req_t *request)
{
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; "
        "img-src 'self' data:; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}

static esp_err_t serve_asset(httpd_req_t *request)
{
    const asset_t *asset = request->user_ctx;
    headers(request);
    httpd_resp_set_type(request, asset->type);
    return httpd_resp_send(request, (const char *)asset->start, asset->end - asset->start);
}

static esp_err_t result(httpd_req_t *request, calibration_result_t code)
{
    headers(request);
    httpd_resp_set_type(request, "application/json");
    const char *status = code == CAL_OK ? "200 OK" : code == CAL_NO_SESSION ? "403 Forbidden"
        : code == CAL_BUSY ? "409 Conflict" : code == CAL_COMMAND_TIMEOUT ? "503 Service Unavailable"
        : "400 Bad Request";
    httpd_resp_set_status(request, status);
    char body[256];
    int length = snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}",
                          code == CAL_OK ? "true" : "false", calibration_message(code));
    return httpd_resp_send(request, body, length);
}

static esp_err_t get_calibration(httpd_req_t *request)
{
    calibration_command_t command = {.action = CAL_QUERY};
    size_t token_length = httpd_req_get_hdr_value_len(request, "X-Calibration-Token");
    if (token_length && (token_length != CAL_TOKEN_LENGTH
        || httpd_req_get_hdr_value_str(request, "X-Calibration-Token", command.token,
                                       sizeof(command.token)) != ESP_OK)) return result(request, CAL_NO_SESSION);
    calibration_engine_t snapshot;
    calibration_result_t code = calibration_exchange(&command, &snapshot);
    if (code != CAL_OK) return result(request, code);
    web_status_frame_t readings;
    bool available = web_monitor_snapshot(&readings);
    char telemetry[512], json[2048];
    int64_t now = esp_timer_get_time();
    if (web_status_format(telemetry, sizeof(telemetry), available ? &readings : NULL, now) < 0)
        return result(request, CAL_COMMAND_TIMEOUT);
    int length = calibration_format(json, sizeof(json), &snapshot, command.token, now, telemetry);
    if (length < 0) return result(request, CAL_COMMAND_TIMEOUT);
    headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, length);
}

static bool same_origin(httpd_req_t *request)
{
    if (httpd_req_get_hdr_value_len(request, "Origin") == 0) return true; /* CLI clients. */
    char origin[112], host[96], expected[112];
    if (httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK
        || httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) return false;
    snprintf(expected, sizeof(expected), "http://%s", host);
    return strcmp(origin, expected) == 0;
}

static esp_err_t post_calibration(httpd_req_t *request)
{
    char type[96];
    if (!same_origin(request)) return result(request, CAL_NO_SESSION);
    if (request->content_len <= 0 || request->content_len >= 512
        || httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK
        || (strcmp(type, "application/x-www-form-urlencoded") != 0
            && strncmp(type, "application/x-www-form-urlencoded;", 34) != 0))
        return result(request, CAL_INVALID_SETTINGS);
    char body[512];
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return result(request, CAL_COMMAND_TIMEOUT);
        received += count;
    }
    if (memchr(body, '\0', received)) return result(request, CAL_INVALID_SETTINGS);
    body[received] = '\0';
    calibration_command_t command;
    if (!calibration_parse_form(body, &command)) return result(request, CAL_INVALID_SETTINGS);
    if (command.action == CAL_CAPTURE) {
        calibration_engine_t snapshot;
        calibration_result_t code = calibration_exchange(&command, &snapshot);
        if (code != CAL_OK) return result(request, code);
        headers(request);
        httpd_resp_set_type(request, "application/json");
        char reply[80];
        int length = snprintf(reply, sizeof(reply), "{\"ok\":true,\"capture_id\":%u}",
                              (unsigned)snapshot.capture_id);
        return httpd_resp_send(request, reply, length);
    }
    return result(request, calibration_exchange(&command, NULL));
}

esp_err_t calibration_web_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/calibration", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[0]},
        {.uri = "/calibration.css", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[1]},
        {.uri = "/calibration.js", .method = HTTP_GET, .handler = serve_asset, .user_ctx = (void *)&s_assets[2]},
        {.uri = "/api/calibration", .method = HTTP_GET, .handler = get_calibration},
        {.uri = "/api/calibration", .method = HTTP_POST, .handler = post_calibration},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t error = httpd_register_uri_handler(server, &routes[i]);
        if (error != ESP_OK) return error;
    }
    return ESP_OK;
}
