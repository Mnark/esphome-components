//#ifdef WEBDAV_ENABLE_CAMERA

#include "esp_log.h"
#include <esp_http_server.h>
#include "esphome/components/esp32_camera/esp32_camera.h"
#include "camserver.h"

static const char *TAG = "camserver";
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";
static QueueHandle_t xFrameQueue = NULL;
TaskHandle_t xHandle = NULL;

namespace esphome {
namespace webdav {

static void stream_free_func(void *ctx)
{
    ESP_LOGI(TAG, "/stream Free Context function called");
    free(ctx);
}

static void frame_process(void *arg)
{
    webdav::CamServer *server = (webdav::CamServer *)arg;
    camera_image_data_t frame;
    esp_err_t ret;
    //truct timeval _timestamp;
    char *part_buf[128];
    while (true){
        if ( xQueuePeek(xFrameQueue, &frame, 50 / portTICK_PERIOD_MS)){
            //_timestamp.tv_sec = 0; //fb->timestamp.tv_sec;
            //_timestamp.tv_usec = 0; //fb->timestamp.tv_usec;
            for(int i = 0; i < server->listeners.size(); i++){
                httpd_req_t * req = server->listeners[i];
                ret = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
                if (ret == ESP_OK) {
                    size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, frame.length, 0, 0);
                    ret = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
                }
                if (ret == ESP_OK) {
                    ret = httpd_resp_send_chunk(server->listeners[i],  (const char *)frame.data, frame.length);
                }
                if (ret != ESP_OK){
                  server->listeners.erase(server->listeners.begin() + i); 
                }
            } 
            xQueueReceive(xFrameQueue, &frame, 10 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay( 100 / portTICK_PERIOD_MS);
        }
    }
}

esp_err_t cam_handler(httpd_req_t *req)
{
    webdav::CamServer *server = (webdav::CamServer *)req->user_ctx;
    
    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    server->listeners.push_back(req);
    if (! req->sess_ctx) {
        ESP_LOGI(TAG, "/adder PUT allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        req->free_ctx = stream_free_func;
    }
    *(int *)req->sess_ctx = 1;
    while (server->listeners.size()>0){
        vTaskDelay(500);
    }

    return ESP_OK;
}

CamServer::CamServer(esp32_camera::ESP32Camera *camera) {
    this->camera_ = camera;
}

CamServer::~CamServer() {}

esp_err_t CamServer::start() {
    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = 32769;
    stream_config.lru_purge_enable = true;
    stream_config.keep_alive_enable = true; 

    esp_err_t err = httpd_start(&this->cam_server, &stream_config);
    if (err == ESP_OK)
    {
        httpd_uri_t uri_stream = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = cam_handler,
            .user_ctx = this,
        };
        httpd_register_uri_handler(this->cam_server, &uri_stream);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start http stream server Error: %s", esp_err_to_name(err));
        return err;
    }
    
    if (this->camera_){
        sensor_t *s = esp_camera_sensor_get();
        xFrameQueue = xQueueCreate(1, sizeof(camera_image_data_t));
        BaseType_t xReturned = xTaskCreatePinnedToCore(frame_process, "Frame Streamer",  4096, this, 2, &xHandle, 1);
        if( xReturned != pdPASS ) {
            ESP_LOGE(TAG,"Failed top create task for TFLite");
            //return ESP_FAIL;
        }
        this->camera_->add_image_callback([this](std::shared_ptr<esp32_camera::CameraImage> image) {
            if (this->listeners.size() > 0){
                if (uxQueueSpacesAvailable(xFrameQueue) > 0){
                    int data_length =  image.get()->get_data_length();
                    //ESP_LOGI(TAG, "%d byte image from camera, someones listening and theres space on the queue...", data_length);

                    if (this->current_image_.data == NULL){
                        //ESP_LOGI(TAG, "Allocating initial memory of %i", data_length); 
                        this->current_image_.data = (uint8_t *) heap_caps_malloc(data_length, MALLOC_CAP_SPIRAM);
                        this->current_image_.allocated_memory = data_length;
                    }
                    if (this->current_image_.allocated_memory < data_length){
                        //ESP_LOGI(TAG, "Re-allocating memory... already allocated: %i",this->current_image_.allocated_memory); 
                        free(this->current_image_.data); 
                        this->current_image_.data = (uint8_t *) heap_caps_malloc( data_length, MALLOC_CAP_SPIRAM);
                        this->current_image_.allocated_memory = data_length;
                    }
                    if (this->current_image_.data){
                        //ESP_LOGI(TAG, "Storing image length: %i", image.get()->get_data_length());
                        memcpy(this->current_image_.data, image.get()->get_data_buffer() , data_length);
                        this->current_image_.length =  data_length; 
                        if (!xQueueSend(xFrameQueue, &this->current_image_, 30 / portTICK_PERIOD_MS)) {
                            ESP_LOGE(TAG, "Failed to send camera image to queue");
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed allocate memory for image");
                        this->current_image_.allocated_memory = 0;
                    }
                } else {
                    ESP_LOGD(TAG, "Dropping Frame ... process busy");
                }
                
            }
        });
    }
    return ESP_OK;     
}

esp_err_t CamServer::stop() {
    return httpd_stop(this->cam_server);
}

}
}

//#endif
