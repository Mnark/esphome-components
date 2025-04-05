#pragma once

#ifdef USE_ESP32

#include <cinttypes>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <dirent.h>
#include <esp_http_server.h>
#include "nvs_flash.h"

#include "../sdmmc/sdmmc.h"
//#include "sdmmc.h"
//#include "davserver.h"
#ifdef WEBDAV_ENABLE_CAMERA
#include "esphome/components/esp32_camera/esp32_camera.h"
//     #include "camserver.h"
#endif

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

//struct httpd_req;  // NOLINT(readability-identifier-naming)
#define SCRATCH_BUFSIZE 4096
#define CONFIG_WEBDAV_MAX_ASYNC_REQUESTS 5
#define ASYNC_WORKER_TASK_PRIORITY      5
#define ASYNC_WORKER_TASK_STACK_SIZE    3074
// #define FILE_PATH_MAX 255

// struct file_server_data {
//     /* Base path of file storage */
//     char base_path[FILE_PATH_MAX + 1];

//     /* Scratch buffer for temporary storage during file transfer */
//     char scratch[SCRATCH_BUFSIZE];
// };
typedef esp_err_t (*httpd_req_handler_t)(httpd_req_t *req);
typedef struct {
        httpd_req_t* req;
        httpd_req_handler_t handler;
} httpd_async_req_t;

namespace esphome {
namespace webdav {

 enum WebDavAuth {
  NONE,
  BASIC,
}; 

class WebDav : public PollingComponent {
 public:
  WebDav();
  ~WebDav();

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void set_port(uint16_t port) { this->port_ = port; }
  void set_sdmmc(sdmmc::SDMMC *sdmmc);
  sdmmc::SDMMC *get_sdmmc(void);
  std::string get_sdmmc_state();
  void set_web_enabled(bool enabled);

  void set_auth(WebDavAuth auth);
  WebDavAuth get_auth(void);
  void set_auth_credentials(std::string auth_credentials);
  std::string get_auth_credentials();

  void set_home_page(std::string );
  void set_web_directory(std::string);
  std::string get_web_directory(void);

  void set_share_name(std::string);
  std::string get_share_name(void);
  std::string get_mount_point(void);
  std::string get_web_uri(void);
  httpd_handle_t get_http_server(void);
  std::string get_home_page(void);
  //void set_home_page(const char*);
  void webdav_register(httpd_handle_t, const char *, const char *);
  esp_err_t queue_request(httpd_req_t *req, httpd_req_handler_t handler);
  
  std::string formatTimeETag(time_t t);
  std::string formatTime(time_t t);
  esp_err_t persist_i32(const char *key, int32_t value);
  esp_err_t get_i32(const char *key, int32_t *out_value);
  //void loop() override;
  void update() override;
  #ifdef WEBDAV_ENABLE_CAMERA
  void set_camera(esp32_camera::ESP32Camera *camera);
  esp32_camera::ESP32Camera * get_camera(void);
  void set_snapshot_directory(std::string);
  std::string get_snapshot_directory(void);
  void set_video_directory(std::string);
  std::string get_video_directory(void);
  #endif

 protected:
  esp_err_t handler_(struct httpd_req *req);
  esp_err_t directory_handler_(struct httpd_req *req);
  esp_err_t file_handler_(struct httpd_req *req);
  esp_err_t create_directory_handler_(struct httpd_req *req);
  esp_err_t upload_post_handler_(struct httpd_req *req);
  void  start_workers(void);
  sdmmc::SDMMC *sdmmc_;
  nvs_handle_t nvs_handle_;
  esp_err_t list_directory_as_html(const char *path, char *json);
  uint16_t port_{0};
  httpd_handle_t server = NULL;
  //httpd_handle_t stream_server = NULL;
  //camera_image_data_t current_image_ = {};
  bool running_{false};
  WebDavAuth auth_;
  std::string  home_page_;
  bool web_enabled_;
  std::string auth_credentials_;
  std::string web_directory_;
  std::string web_uri_;
  std::string share_name_;
#ifdef WEBDAV_ENABLE_CAMERA
  //webdav::CamServer *webCamServer;
  esp32_camera::ESP32Camera *camera_ = NULL;
  std::string snapshot_directory_;
  std::string video_directory_;
#endif
  //struct file_server_data *ctx = NULL;
};

}  // namespace webdav
}  // namespace esphome

#endif  // USE_ESP32