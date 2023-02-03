#include "app_http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
/* A simple example that demonstrates how to crea.te GET and POST
 * handlers for the web server.
 */

static const char *TAG = "app_http_server";
httpd_handle_t server = NULL;
static http_server_handle_t http_get_handle = NULL;
static http_server_handle_t http_post_handle = NULL;
static http_server_handle_t local_post_handle = NULL;
extern const uint8_t web_start[] asm("_binary_web_html_start");
extern const uint8_t web_end[] asm("_binary_web_html_end");

extern const uint8_t web_local_start[] asm("_binary_index_html_start");
extern const uint8_t web_local_end[] asm("_binary_index_html_end");

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char *buf = "Hello World";
    size_t buf_len = strlen(buf);
    // httpd_resp_set_type(req, "image/jpg");
    httpd_resp_send(req, (const char *)web_start, (web_end - web_start));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0)
    {
        // ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t hello = {
    .uri = "/hello",
    .method = HTTP_GET,
    .handler = hello_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "Hello World!",
};

static esp_err_t local_get_handler(httpd_req_t *req)
{
    // httpd_resp_set_type(req, "image/jpg");
    httpd_resp_send(req, (const char *)web_local_start, (web_local_end - web_local_start));

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0)
    {
        // ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}

static const httpd_uri_t local = {
    .uri = "/local",
    .method = HTTP_GET,
    .handler = local_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "Local controll!",
};

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char buf_sd1[100];
    // printf("da vao day \n");
    int len = httpd_req_recv(req, buf_sd1, 100);
    // printf("%s\n",buf_sd1);
    http_post_handle(buf_sd1, len);

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t wifi_info_uri = {
    .uri = "/wifi",
    .method = HTTP_POST,
    .handler = wifi_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "wifi_info_uri",
};

static esp_err_t local_post_handler(httpd_req_t *req)
{
    char buf_sd1[100];
    // printf("da vao day \n");
    int len = httpd_req_recv(req, buf_sd1, 100);
    // printf("%s\n",buf_sd1);
    local_post_handle(buf_sd1, len);

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t local_info_uri = {
    .uri = "/change",
    .method = HTTP_POST,
    .handler = wifi_post_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx = "change",
};

void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &local);
        httpd_register_uri_handler(server, &local_info_uri);
        httpd_register_uri_handler(server, &wifi_info_uri);
    }
    else
    {
        ESP_LOGI(TAG, "Error starting server!");
    }
}

void stop_webserver(void)
{
    // Stop the httpd server
    httpd_stop(server);
}

void http_get_set_callback(void *cb)
{
    if (cb)
    {
        http_get_handle = cb;
    }
}

void http_post_set_callback(void *cb)
{
    if (cb)
    {
        http_post_handle = cb;
    }
}

void local_post_set_callback(void *cb)
{
    if (cb)
    {
        http_post_handle = cb;
    }
}