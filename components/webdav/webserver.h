#pragma once

#include "../sdmmc/sdmmc.h"

#include "webdav.h"

namespace esphome {
namespace webdav {
//class WebDav;

class WebServer {
public:
        WebServer(webdav::WebDav *webdav);
        ~WebServer() {};

        void register_server(httpd_handle_t server);
        esp_err_t process_message(char * message, httpd_ws_frame_t *out_pkt, httpd_req_t *req);
        esp_err_t send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt) ;

private:
        sdmmc::SDMMC *sdmmc_;
        webdav::WebDav *webdav_;
#ifdef WEBDAV_ENABLE_CAMERA        
        sensor_t *sensor;
#endif

};

}
} // namespace