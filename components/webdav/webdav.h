#pragma once

#ifdef USE_ESP32

#include <cinttypes>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <dirent.h>
#include <esp_http_server.h>

#include "../sdmmc/sdmmc.h"
//#include "sdmmc.h"
//#include "davserver.h"
#ifdef WEBDAV_ENABLE_CAMERA
    #include "camserver.h"
#endif

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

//struct httpd_req;  // NOLINT(readability-identifier-naming)
#define SCRATCH_BUFSIZE 4096
// #define FILE_PATH_MAX 255

// struct file_server_data {
//     /* Base path of file storage */
//     char base_path[FILE_PATH_MAX + 1];

//     /* Scratch buffer for temporary storage during file transfer */
//     char scratch[SCRATCH_BUFSIZE];
// };

namespace esphome {
namespace webdav {

 enum WebDavAuth {
  NONE,
  BASIC,
}; 

class WebDav : public Component {
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

  void set_home_page(const char*);
  void set_web_directory(std::string);
  std::string get_web_directory(void);
  void set_share_name(std::string);
  std::string get_share_name(void);
  std::string get_mount_point(void);
  std::string get_web_uri(void);
  char *get_home_page(void);
  //void set_home_page(const char*);
  void webdav_register(httpd_handle_t, const char *, const char *);
  //esp_err_t web_handler(struct httpd_req *req);
  //esp_err_t add_listener(httpd_req_t *req);
  //std::vector<httpd_req_t *>listeners;
  void loop() override;
  #ifdef WEBDAV_ENABLE_CAMERA
  void set_camera(esp32_camera::ESP32Camera *camera);
  #endif

 protected:
  esp_err_t handler_(struct httpd_req *req);
  esp_err_t directory_handler_(struct httpd_req *req);
  esp_err_t file_handler_(struct httpd_req *req);
  esp_err_t create_directory_handler_(struct httpd_req *req);
  esp_err_t upload_post_handler_(struct httpd_req *req);

  sdmmc::SDMMC *sdmmc_;

  esp_err_t list_directory_as_html(const char *path, char *json);
  uint16_t port_{0};
  httpd_handle_t server = NULL;
  //httpd_handle_t stream_server = NULL;
  //camera_image_data_t current_image_ = {};
  bool running_{false};
  WebDavAuth auth_;
  char home_page_[32];
  bool web_enabled_;
  std::string auth_credentials_;
  std::string web_directory_;
  std::string web_uri_;
  std::string share_name_;
#ifdef WEBDAV_ENABLE_CAMERA
  webdav::CamServer *webCamServer;
  esp32_camera::ESP32Camera *camera_ = NULL;
#endif
  //struct file_server_data *ctx = NULL;
};

}  // namespace webdav
}  // namespace esphome

#endif  // USE_ESP32