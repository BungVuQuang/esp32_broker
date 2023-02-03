
#include "wifi_connecting.h"
const char *TAG = "wifi_connect";

extern struct wifi_info_t wifi_info;
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    int s_retry_num = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < 20)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(xCreatedEventGroup, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// void event_handler(void *arg, esp_event_base_t event_base,
//                    int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         if (provision_type == PROV_NONE)
//         {
//             esp_wifi_connect();
//         }
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         esp_wifi_connect();
//         xEventGroupClearBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         xEventGroupSetBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
//     }
//     else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
//     {
//         ESP_LOGI(TAG, "Scan done");
//     }
//     else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
//     {
//         ESP_LOGI(TAG, "Found channel");
//     }
//     else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
//     {
//         ESP_LOGI(TAG, "Got SSID and password");

//         smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
//         wifi_config_t wifi_config;
//         uint8_t ssid[33] = {0};
//         uint8_t password[65] = {0};

//         bzero(&wifi_config, sizeof(wifi_config_t)); // clear wifi_config trước
//         memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
//         memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
//         wifi_config.sta.bssid_set = evt->bssid_set;
//         if (wifi_config.sta.bssid_set == true)
//         {
//             memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
//         }

//         memcpy(ssid, evt->ssid, sizeof(evt->ssid));
//         memcpy(password, evt->password, sizeof(evt->password));
//         ESP_LOGI(TAG, "SSID:%s", ssid);
//         ESP_LOGI(TAG, "PASSWORD:%s", password);

//         ESP_ERROR_CHECK(esp_wifi_disconnect());
//         ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // ghi thông tin wifi vào flash cho lần đăng nhập sau
//         esp_wifi_connect();
//     }
// }

void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    // xCreatedEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    // ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    // ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ESP_ERROR_CHECK( esp_wifi_start() );
}

// void wifi_init_sta(void)
// {

//     wifi_config_t wifi_config = {
//         .sta = {
//             /* Setting a password implies station will connect to all security modes including WEP/WPA.
//              * However these modes are deprecated and not advisable to be used. Incase your Access point
//              * doesn't support WPA2, these mode can be enabled by commenting below line */
//             .threshold.authmode = WIFI_AUTH_WPA2_PSK,

//             .pmf_cfg = {
//                 .capable = true,
//                 .required = false},
//         },
//     };
//     strcpy((char *)wifi_config.ap.ssid, ssid);
//     strcpy((char *)wifi_config.ap.password, pass);
//     esp_wifi_stop();
//     // esp_netif_create_default_wifi_sta();
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     ESP_LOGI(TAG, "wifi_init_sta finished.");

//     /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
//      * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
//     EventBits_t bits = xEventGroupWaitBits(xCreatedEventGroup,
//                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
//                                            pdFALSE,
//                                            pdFALSE,
//                                            portMAX_DELAY);

//     /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
//      * happened. */
//     if (bits & WIFI_CONNECTED_BIT)
//     {
//         ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
//                  ssid, pass);
//         // start_webserver();
//     }
//     else if (bits & WIFI_FAIL_BIT)
//     {
//         ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
//                  ssid, pass);
//     }
//     else
//     {
//         ESP_LOGE(TAG, "UNEXPECTED EVENT");
//     }
// }

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init()); // khởi tạo network interface
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL)); // bất kể một event ESP_EVENT_ANY_ID nào của wifi thì wifi_event_handler() sẽ được thực thi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "setup",
            .ssid_len = 5,
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK},
    };
    if (strlen((char *)wifi_config.ap.password) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_webserver();
    http_post_set_callback(wifi_data_callback);
    xEventGroupWaitBits(xCreatedEventGroup, WIFI_RECV_INFO, true, false, portMAX_DELAY);
    stop_webserver();
    printf("stop_webserver\n");
    wifi_init_sta();
}

void wifi_init_local_ap(void)
{
    // ESP_ERROR_CHECK(esp_netif_init()); // khởi tạo network interface
    //  ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *my_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
    //                                                     ESP_EVENT_ANY_ID,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     NULL)); // bất kể một event ESP_EVENT_ANY_ID nào của wifi thì wifi_event_handler() sẽ được thực thi
    // ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
    //                                                     IP_EVENT_STA_GOT_IP,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     NULL));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "local",
            .ssid_len = 5,
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .beacon_interval = 100,
        },
    };
    if (strlen((char *)wifi_config.ap.password) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    start_webserver();
    local_post_set_callback(local_data_callback);
    // http_post_set_callback(wifi_data_callback);
    xEventGroupWaitBits(xCreatedEventGroup, WIFI_RECV_INFO, true, false, portMAX_DELAY);
    stop_webserver();
    wifi_init_sta();
}