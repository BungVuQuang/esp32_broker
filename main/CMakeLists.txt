set(COMPONENT_SRCS main.c mqtt_server.c mqtt_subscriber.c mqtt_publisher.c)
set(COMPONENT_ADD_INCLUDEDIRS ".")
register_component()

# set(pri_req mqtt mongoose nvs_flash esp_vfs_fat)
# set(app_src main.c mqtt_server.c mqtt_subscriber.c mqtt_publisher.c http_server.c)
# idf_component_register(SRCS ${app_src}
# INCLUDE_DIRS "."
# REQUIRES ${pri_req}
# )
