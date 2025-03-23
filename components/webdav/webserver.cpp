#ifdef WEBDAV_ENABLE_WEBSERVER

#include "esp_log.h"
#include "tiny-json.h"
#include "webserver.h"

static const char *TAG = "webdav Web server";

int res = 0;
int current_zoom = 0;
// typedef enum {
//     OV2640_MODE_UXGA, OV2640_MODE_SVGA, OV2640_MODE_CIF, OV2640_MODE_MAX
// } ov2640_sensor_mode_t;
#ifdef WEBDAV_ENABLE_CAMERA
std::vector<std::vector<int>> zoom = {
        {esphome::esp32_camera::ESP32_CAMERA_SIZE_400X296,400,296},
        {esphome::esp32_camera::ESP32_CAMERA_SIZE_800X600,800,600},
        {esphome::esp32_camera::ESP32_CAMERA_SIZE_1600X1200,1600,1200}}; 
#endif

int unused = 0;
//int total_x = 1200/4;
//int total_y = 1200/4;
int w = 400;
int h = 296;
int offset_x = 0;
int offset_y = 0;
int step = 50;

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)
    

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename){
        if (IS_FILE_EXT(filename, ".pdf"))
        {
        return httpd_resp_set_type(req, "application/pdf");
        }
        else if (IS_FILE_EXT(filename, ".html")|| IS_FILE_EXT(filename, ".htm"))
        {
        return httpd_resp_set_type(req, "text/html");
        }
        else if (IS_FILE_EXT(filename, ".jpeg") || IS_FILE_EXT(filename, ".jpg"))
        {
        return httpd_resp_set_type(req, "image/jpeg");
        }
        else if (IS_FILE_EXT(filename, ".ico"))
        {
        return httpd_resp_set_type(req, "image/x-icon");
        }
        else if (IS_FILE_EXT(filename, ".png"))
        {
        return httpd_resp_set_type(req, "image/png");
        }
        /* For any other type always set as plain text */
        return httpd_resp_set_type(req, "text/plain");
}

namespace esphome {
namespace webdav {

esp_err_t web_handler(struct httpd_req *req)
{
    webdav::WebDav *server = (webdav::WebDav *)req->user_ctx;
    std::string path;
    if (strcmp(req->uri, "/") == 0){
        path = server->get_mount_point() + "/" + server->get_web_directory() + "/" + server->get_home_page();
    }else{
        path = server->get_mount_point() + "/" + server->get_web_directory() + "/" + req->uri;
    }

    ESP_LOGD(TAG, "Web Handler uri: %s path: %s", req->uri, path.c_str());


    esp_err_t res = ESP_OK;
    struct stat status = {};
    FILE *fd;

    res = stat(path.c_str(), &status);
    if (res != ESP_OK)
    {
        ESP_LOGW(TAG, "Requested file/directory doesn't exist: %s", path.c_str());
        res = httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, " URI is not available");
        return res;
    }

    fd = fopen(path.c_str(), "r");
    if (!fd)
    {
        ESP_LOGW(TAG, "File not found");
        res = httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, " URI is not available");
        return ESP_FAIL;
    }
        res = set_content_type_from_file(req, path.c_str());

        if (res != ESP_OK)
        {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed setting response type");
                ESP_LOGW(TAG, "Failed to set HTTP response type");
                return res;
        }

        res = httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");

        if (res != ESP_OK)
        {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed setting response cache-control header");
                ESP_LOGW(TAG, "Failed to set HTTP cache-control type");
                return res;
        }
    char *chunk;
    chunk = (char *)malloc(SCRATCH_BUFSIZE);
    if (chunk == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate buffer");
        return ESP_FAIL;
    }
    size_t chunksize;
    do
    {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0)
        {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK)
            {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fclose(fd);
    ESP_LOGI(TAG, "File sending complete: %s", req->uri);

    httpd_resp_send_chunk(req, NULL, 0);

    //httpd_resp_set_hdr(req, "Connection", "close");

    return ESP_OK;
}


esp_err_t WebServer::send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt) {
    static constexpr size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients] = {0};

    esp_err_t ret = httpd_get_client_list(this->webdav_->get_http_server(), &fds, client_fds);

    if (ret != ESP_OK) {
        return ret;
    }
        ESP_LOGI(TAG, "Client list reurned %d clients", fds);
    for (int i = 0; i < fds; i++) {
        ESP_LOGI(TAG, "Sending response to client %d", i);
        auto client_info = httpd_ws_get_fd_info(this->webdav_->get_http_server(), client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(this->webdav_->get_http_server(), client_fds[i], ws_pkt);
        }
    }

    return ESP_OK;
}

esp_err_t  WebServer::process_message(char * message, httpd_ws_frame_t *out_pkt, httpd_req_t *req){
        ESP_LOGI(TAG, "WS request : %s", message);
        std::string response;
        enum { MAX_FIELDS = 4 };
        json_t pool[ MAX_FIELDS ];

        json_t const* parent = json_create( message, pool, MAX_FIELDS );
        if ( parent == NULL ){
                ESP_LOGE(TAG, "failed to parse json msg: %s", message);
                return ESP_FAIL; 
        }
        json_t const* command = json_getProperty( parent, "command" );
        json_t const* action = json_getProperty( parent, "action" );
        json_t const* value = json_getProperty( parent, "value" );
        //ESP_LOGI (TAG,"Command is %s",json_getValue(command));

#ifdef WEBDAV_ENABLE_CAMERA
        if (strcmp(json_getValue(command),"get") == 0){
                 uint8_t *json_response = (uint8_t *)calloc(1, 2049);
                if (json_response == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for response");
                        return ESP_ERR_NO_MEM;
                }
                out_pkt->payload = json_response;
                char *p = (char *)json_response;
                *p++ = '{';
                p += sprintf(p, "\"xclk\":%u,",this->sensor->xclk_freq_hz / 1000000);
                p += sprintf(p, "\"pixformat\":%u,", this->sensor->pixformat);
                p += sprintf(p, "\"framesize\":%u,",  this->sensor->status.framesize);
                p += sprintf(p, "\"quality\":%u,", this->sensor->status.quality);
                p += sprintf(p, "\"brightness\":%d,", this->sensor->status.brightness);
                p += sprintf(p, "\"contrast\":%d,", this->sensor->status.contrast);
                p += sprintf(p, "\"saturation\":%d,", this->sensor->status.saturation);
                p += sprintf(p, "\"sharpness\":%d,", this->sensor->status.sharpness);
                p += sprintf(p, "\"special_effect\":%u,", this->sensor->status.special_effect);
                p += sprintf(p, "\"wb_mode\":%u,", this->sensor->status.wb_mode);
                p += sprintf(p, "\"awb\":%u,", this->sensor->status.awb);
                p += sprintf(p, "\"awb_gain\":%u,", this->sensor->status.awb_gain);
                p += sprintf(p, "\"aec\":%u,", this->sensor->status.aec);
                p += sprintf(p, "\"aec2\":%u,", this->sensor->status.aec2);
                p += sprintf(p, "\"ae_level\":%d,", this->sensor->status.ae_level);
                p += sprintf(p, "\"aec_value\":%u,", this->sensor->status.aec_value);
                p += sprintf(p, "\"agc\":%u,", this->sensor->status.agc);
                p += sprintf(p, "\"agc_gain\":%u,", this->sensor->status.agc_gain);
                p += sprintf(p, "\"gainceiling\":%u,", this->sensor->status.gainceiling);
                p += sprintf(p, "\"bpc\":%u,", this->sensor->status.bpc);
                p += sprintf(p, "\"wpc\":%u,", this->sensor->status.wpc);
                p += sprintf(p, "\"raw_gma\":%u,", this->sensor->status.raw_gma);
                p += sprintf(p, "\"lenc\":%u,", this->sensor->status.lenc);
                p += sprintf(p, "\"hmirror\":%u,", this->sensor->status.hmirror);
                p += sprintf(p, "\"dcw\":%u,", this->sensor->status.dcw);
                p += sprintf(p, "\"colorbar\":%u,", this->sensor->status.colorbar);
                p += sprintf(p, "\"zoom_level\":%u", current_zoom);
                *p++ = '}';
                *p++ = 0;
                out_pkt->len = strlen((char*)json_response);

                //ESP_LOGI(TAG, "response packet length: %d content\n %s",  out_pkt->len,(char*) json_response);

                out_pkt->payload = json_response;
                //return trigger_async_send(req->handle, req);
                esp_err_t ret = httpd_ws_send_frame(req, out_pkt);
                if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
                }
                free(json_response);
                return ret;
        }

        if (strcmp(json_getValue(command),"update") == 0) {
                ESP_LOGI(TAG, "update command recieved");
                json_t const* target = json_getProperty( parent, "target");
                json_t const* value = json_getProperty( parent, "value");
                ESP_LOGI (TAG,"update %s", json_getValue(target));
                //log_i("%s = %d", variable, json_getInteger(value));
                sensor_t *s = esp_camera_sensor_get();
                int res = 0;

                if (!strcmp(json_getValue(target), "framesize")) {
                        if (s->pixformat == PIXFORMAT_JPEG) {
                                res = s->set_framesize(s, (framesize_t)json_getInteger(value));
                        }
                } else if (!strcmp(json_getValue(target), "quality")) {
                        res = s->set_quality(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "contrast")) {
                        res = s->set_contrast(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "brightness")) {
                        res = s->set_brightness(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "saturation")) {
                        res = s->set_saturation(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "gainceiling")) {
                        res = s->set_gainceiling(s, (gainceiling_t)json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "colorbar")) {
                res = s->set_colorbar(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "awb")) {
                res = s->set_whitebal(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "agc")) {
                res = s->set_gain_ctrl(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "aec")) {
                res = s->set_exposure_ctrl(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "hmirror")) {
                res = s->set_hmirror(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "vflip")) {
                res = s->set_vflip(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "awb_gain")) {
                res = s->set_awb_gain(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "agc_gain")) {
                res = s->set_agc_gain(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "aec_value")) {
                res = s->set_aec_value(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "aec2")) {
                res = s->set_aec2(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "dcw")) {
                res = s->set_dcw(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "bpc")) {
                res = s->set_bpc(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "wpc")) {
                res = s->set_wpc(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "raw_gma")) {
                res = s->set_raw_gma(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "lenc")) {
                res = s->set_lenc(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "special_effect")) {
                res = s->set_special_effect(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "wb_mode")) {
                res = s->set_wb_mode(s, json_getInteger(value));
                } else if (!strcmp(json_getValue(target), "ae_level")) {
                res = s->set_ae_level(s, json_getInteger(value));
                }
                else {
                        ESP_LOGW(TAG, "Unknown command: %s", json_getValue(target));
                        res = -1;
                }
        }

        if (strcmp(json_getValue(command),"zoom-in") == 0) {      
                ESP_LOGI(TAG, "zoom-in command recieved");
                if (current_zoom > 1){
                        ESP_LOGI(TAG,"Camera is fully zoomed in");
                        return ESP_OK;
                }else {
                        current_zoom++;
                        offset_x = offset_x * 2;
                        offset_y = offset_y * 2;
                        ESP_LOGI(TAG,"Zooming in: Zoom Level: %d Framesize: %d OffsetX: %d Offset Y: %d",
                                current_zoom, zoom[current_zoom][0],  offset_x ,  offset_y  );
                }

                res = this->sensor->set_res_raw(this->sensor, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);

                response =  "{\"zoom_level\":" + std::to_string(current_zoom) + "}";
                httpd_ws_frame_t resp_pkt ={
                        .final = true,
                        .fragmented = false,
                        .type = HTTPD_WS_TYPE_TEXT,
                        .payload = (uint8_t*)response.c_str(),
                        .len = response.length()
                };
                send_frame_to_all_clients(&resp_pkt);
        }

        if (strcmp(json_getValue(command),"zoom-out") == 0) {      
                ESP_LOGI(TAG, "zoom-out command recieved");
                if (current_zoom == 0){
                        ESP_LOGI(TAG,"Camera is fully zoomed out");
                        return ESP_OK;
                }else {
                        current_zoom--;
                        offset_x = offset_x / 2;
                        offset_y = offset_y / 2;
                        ESP_LOGI(TAG,"Zooming out: Zoom Level: %d Framesize: %d OffsetX: %d Offset Y: %d",
                                current_zoom, zoom[current_zoom][0],  offset_x ,  offset_y  );
                }
                //sensor_t *s = esp_camera_sensor_get();
                res = this->sensor->set_res_raw(this->sensor, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
                response =  "{\"zoom_level\":" + std::to_string(current_zoom) + "}";
                httpd_ws_frame_t resp_pkt ={
                        .final = true,
                        .fragmented = false,
                        .type = HTTPD_WS_TYPE_TEXT,
                        .payload = (uint8_t*)response.c_str(),
                        .len = response.length()
                };
                send_frame_to_all_clients(&resp_pkt);
        }

        if (strcmp(json_getValue(command),"move") == 0) {
                ESP_LOGI(TAG, "Current Offsets x: %d y: %d zoom level: %d", offset_x, offset_y, current_zoom);
                json_t const* moveX = json_getProperty( parent, "X" );
                json_t const* moveY = json_getProperty( parent, "Y" );
                offset_x = offset_x - (json_getInteger(moveX) * zoom[current_zoom][1] / w);
                if (offset_x < 0){
                        offset_x = 0;
                }
                if (offset_x > (zoom[current_zoom][1] - w)){
                        offset_x = zoom[current_zoom][1] - w;
                }

                offset_y = offset_y - (json_getInteger(moveY) * zoom[current_zoom][2] / h);
                if (offset_y < 0){
                        offset_y = 0;
                }
                if (offset_y > (zoom[current_zoom][2] - h)){
                        offset_y = zoom[current_zoom][2] - h;
                }
                sensor_t *s = esp_camera_sensor_get();
                ESP_LOGI(TAG, "New Offstes x: %d y: %d", offset_x, offset_y);
                //static int set_window(sensor_t *sensor, ov2640_sensor_mode_t mode, int offset_x, int offset_y, int max_x, int max_y, int w, int h){
                //res = s->set_window(s,zoom[current_zoom][0],offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h);
                res = s->set_res_raw(s, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
        
        }

        if (strcmp(json_getValue(command),"move-up") == 0) {      
                offset_y = offset_y - step;
                if (offset_y < 0){
                        offset_y = 0;
                }
                sensor_t *s = esp_camera_sensor_get();
                res = s->set_res_raw(s, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
                ESP_LOGI(TAG, "sensor returned %d",res);
        }

        if (strcmp(json_getValue(command),"move-down") == 0) {      
                offset_y = offset_y + step;
                if (offset_y > zoom[current_zoom][2] - h){
                        offset_y = zoom[current_zoom][2] - h;
                }
                sensor_t *s = esp_camera_sensor_get();
                res = s->set_res_raw(s, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y,zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
                ESP_LOGI(TAG, "sensor returned %d",res);
        }

        if (strcmp(json_getValue(command),"move-left") == 0) {      
                offset_x = offset_x - step;
                if (offset_x < 0){
                        offset_x = 0;
                }
                sensor_t *s = esp_camera_sensor_get();
                res = s->set_res_raw(s, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
                ESP_LOGI(TAG, "sensor returned %d",res);
        }

        if (strcmp(json_getValue(command),"move-right") == 0) {      
                offset_x = offset_x + step;
                if (offset_x > zoom[current_zoom][1] - w){
                        offset_x = zoom[current_zoom][1] - w;
                }
                sensor_t *s = esp_camera_sensor_get();
                res = s->set_res_raw(s, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);
                ESP_LOGI(TAG, "sensor returned %d",res);
        }
#endif
        return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
        webdav::WebServer *server = (webdav::WebServer *)req->user_ctx;
        if (req->method == HTTP_GET) {
                ESP_LOGI(TAG, "Handshake done, the new connection was opened");
                return ESP_OK;
        }
        httpd_ws_frame_t ws_pkt;
        uint8_t *buf = NULL;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        /* Set max_len = 0 to get the frame len */
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
                return ret;
        }

        if (ws_pkt.len) {
                /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
                buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
                if (buf == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for buf");
                        return ESP_ERR_NO_MEM;
                }
                ws_pkt.payload = buf;
                /* Set max_len = ws_pkt.len to get the frame payload */
                ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
                if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
                        free(buf);
                        return ret;
                }
                //ESP_LOGI(TAG, "Got packet type %d with message: %s", ws_pkt.type, ws_pkt.payload);

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT){
                ret = server->process_message( (char *) ws_pkt.payload, &ws_pkt, req);
        }
        // ret = httpd_ws_send_frame(req, &ws_pkt);
        // if (ret != ESP_OK) {
        //         ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
        // }
        free(buf);
    }
    return ret;
}

WebServer::WebServer(webdav::WebDav *webdav) {
        this->webdav_ = webdav;        
        this->sdmmc_ = webdav->get_sdmmc();
#ifdef WEBDAV_ENABLE_CAMERA 
        this->sensor = esp_camera_sensor_get();
        this->sensor->set_res_raw(this->sensor, zoom[current_zoom][0], unused, unused, unused, offset_x, offset_y, zoom[current_zoom][1], zoom[current_zoom][2], w, h, unused, unused);

#endif        
}

void WebServer::register_server(httpd_handle_t server)
{

    httpd_uri_t uri_web = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = web_handler,
        .user_ctx = this->webdav_,
    };

    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = this,
        .is_websocket = true
    };
    
    httpd_register_uri_handler(server, &uri_ws);
    httpd_register_uri_handler(server, &uri_web);
}

} //namespace webdav
} //namespace esphome
#endif