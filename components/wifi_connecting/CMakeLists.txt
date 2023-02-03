set(pri_req nvs_flash app_http_server)
idf_component_register(SRCS "wifi_connecting.c"
    INCLUDE_DIRS "."
    REQUIRES ${pri_req}
)
