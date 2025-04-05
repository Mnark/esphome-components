#pragma once
#ifdef WEBDAV_ENABLE_CAMERA

#include <vector>
#include <esp_http_server.h>
#include "esphome/components/esp32_camera/esp32_camera.h"
//#include "webserver.h"
#include "webdav.h"
//#include "../sdmmc/sdmmc.h"

#ifndef PART_BOUNDARY
#define PART_BOUNDARY "123456789000000000000987654321"
#endif
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

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
        CamServer(webdav::WebDav *webdav);
        //CamServer( esp32_camera::ESP32Camera *camera);
            //Server(const char* rootPath, const char* rootURI);
        ~CamServer();
        // esp_err_t start(void);
        // esp_err_t stop(void);
        void register_server(httpd_handle_t);
        //void register_camera(esp32_camera::ESP32Camera *);
        std::vector<httpd_req_t *>listeners;
        esp32_camera::ESP32Camera *camera = NULL;
        esp_err_t set_key_value(std::string, std::string);
        bool streaming;
        bool snapshot = false;
        int32_t snapshot_number = 0;
        std::string snapshot_filename;
        bool video = false;
        uint64_t video_end;
        uint64_t video_time = 20 * 1000 * 1000;
        int32_t video_number = 0;
        std::string video_filename;
        httpd_req *req;
        webdav::WebDav *webdav;

    private:
        //sdmmc::SDMMC *sdmmc_;
        esp_err_t cam_handler_(httpd_req_t *req);
        //esp32_camera::ESP32Camera *camera_ = NULL;
        httpd_handle_t cam_server = NULL;
        camera_image_data_t current_image_ = {};
};

}
} // namespace

#endif