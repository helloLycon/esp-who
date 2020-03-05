set(COMPONENT_SRCS "app_main.c" "app_wifi.c" "app_camera.c" "app_httpd.c")
set(COMPONENT_ADD_INCLUDEDIRS "include")

set(EMBED_TXTFILES /work/esp/esp-who/examples/single_chip/camera_web_server/server_certs/ca_cert.pem)

set(COMPONENT_REQUIRES
    esp32-camera
    esp-face
    nvs_flash
    esp_http_server
    fb_gfx
    )

set(COMPONENT_EMBED_FILES
        "www/index_ov2640.html.gz"
        "www/index_ov3660.html.gz")

register_component()
