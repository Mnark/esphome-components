#pragma once
//#ifdef WEBDAV_ENABLE_CAMERA

#include <vector>
#include <esp_http_server.h>
#include "esphome/components/esp32_camera/esp32_camera.h"
//#include "webdav.h"

namespace esphome {
namespace webdav {

typedef
struct {
  size_t length;
  size_t width;
  size_t height;
  size_t allocated_memory;
  uint8_t *data;
} CameraImageData, camera_image_data_t;
//class WebDav;

class CamServer {
    public:
        CamServer(esp32_camera::ESP32Camera *camera);
            //Server(const char* rootPath, const char* rootURI);
        ~CamServer();
        esp_err_t start(void);
        esp_err_t stop(void);
        std::vector<httpd_req_t *>listeners;

    private:
    //    WebDav * webdav;
        esp_err_t cam_handler_(httpd_req_t *req);
        esp32_camera::ESP32Camera *camera_ = NULL;
        httpd_handle_t cam_server = NULL;
        camera_image_data_t current_image_ = {};
};

}
} // namespace

//#endif