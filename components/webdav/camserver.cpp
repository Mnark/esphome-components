//#ifdef WEBDAV_ENABLE_CAMERA

#include "esp_log.h"
#include <esp_http_server.h>
#include "esphome/components/esp32_camera/esp32_camera.h"
#include "esp_timer.h"
#include "camserver.h"

static const char *TAG = "camserver";
static QueueHandle_t xFrameQueue = NULL;
TaskHandle_t xHandle = NULL;

namespace esphome {
namespace webdav {

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

esp_err_t stream_camera(struct httpd_req *req){
    ESP_LOGI(TAG,"Starting camera stream");
    webdav::CamServer *server = (webdav::CamServer *)req->user_ctx;
    server->req = req;
    server->streaming = true;
    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");
    // server->camera->add_image_callback([server](std::shared_ptr<esp32_camera::CameraImage> image) {
    //     ESP_LOGI(TAG,"Got image");
    //     esp_err_t ret = ESP_OK;
    //     char *part_buf[128];
    //     int data_length =  image.get()->get_data_length();
    //     ESP_LOGI(TAG,"Got image: length: %d", data_length);
    //     if (ret == ESP_OK) {
    //         ret = httpd_resp_send_chunk(server->req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    //         if (ret != ESP_OK) {
    //             ESP_LOGW(TAG,"Failed to send boundary chunk ret: %d", ret);
    //             server->streaming = false;
    //         }
    //     }
    //     if (ret == ESP_OK) {
    //         size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, data_length, 0, 0);
    //         ret = httpd_resp_send_chunk(server->req, (const char *)part_buf, hlen);
    //         if (ret != ESP_OK) {
    //             ESP_LOGW(TAG,"Failed to send part chunk ret: %d", ret);
    //             server->streaming = false;
    //         }            
    //     }
    //     if (ret == ESP_OK) {
    //         ret = httpd_resp_send_chunk(server->req, (char*)image.get()->get_data_buffer(), data_length);
    //         if (ret != ESP_OK) {
    //             ESP_LOGW(TAG,"Failed to send data chunk ret: %d", ret);
    //             server->streaming = false;
    //         }            
    //     }
    // });
    // ESP_LOGI(TAG,"Added callback");

    while (server->streaming == true){
        vTaskDelay(100);
    }
    ESP_LOGI(TAG,"Ending camera stream");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
    //this->camera_->remo

}

esp_err_t cam_handler(httpd_req_t *req)
{
    webdav::CamServer *server = (webdav::CamServer *)req->user_ctx;
    
    server->streaming = true;
    if (server->webdav->queue_request(req, stream_camera) == ESP_OK) {
            return ESP_OK;
    } else {
            httpd_resp_set_status(req, "503 Busy");
            httpd_resp_sendstr(req, "<div> no workers available. server busy.</div>");
            return ESP_OK;
    }
    return ESP_OK;
}

CamServer::CamServer(webdav::WebDav *webdav) {
    this->webdav = webdav;
    esp_err_t err = this->webdav->get_i32("video_number", &this->video_number);
    if (err != ESP_OK){
        this->video_number = 1;
    } 
    err = this->webdav->get_i32("snapshot_number", &this->snapshot_number);
    if (err != ESP_OK){
        this->snapshot_number = 1;
    } 
}

// CamServer::CamServer(esp32_camera::ESP32Camera *camera) {
//     this->camera = camera;
// }

CamServer::~CamServer() {}

//     if (this->camera_){
//         sensor_t *s = esp_camera_sensor_get();
//         xFrameQueue = xQueueCreate(1, sizeof(camera_image_data_t));
//         BaseType_t xReturned = xTaskCreatePinnedToCore(frame_process, "Frame Streamer",  4096, this, 2, &xHandle, 1);
//         if( xReturned != pdPASS ) {
//             ESP_LOGE(TAG,"Failed top create task for TFLite");
//             //return ESP_FAIL;
//         }
//         this->camera_->add_image_callback([this](std::shared_ptr<esp32_camera::CameraImage> image) {
//             if (this->listeners.size() > 0){
//                 if (uxQueueSpacesAvailable(xFrameQueue) > 0){
//                     int data_length =  image.get()->get_data_length();
//                     //ESP_LOGI(TAG, "%d byte image from camera, someones listening and theres space on the queue...", data_length);

//                     if (this->current_image_.data == NULL){
//                         //ESP_LOGI(TAG, "Allocating initial memory of %i", data_length); 
//                         this->current_image_.data = (uint8_t *) heap_caps_malloc(data_length, MALLOC_CAP_SPIRAM);
//                         this->current_image_.allocated_memory = data_length;
//                     }
//                     if (this->current_image_.allocated_memory < data_length){
//                         //ESP_LOGI(TAG, "Re-allocating memory... already allocated: %i",this->current_image_.allocated_memory); 
//                         free(this->current_image_.data); 
//                         this->current_image_.data = (uint8_t *) heap_caps_malloc( data_length, MALLOC_CAP_SPIRAM);
//                         this->current_image_.allocated_memory = data_length;
//                     }
//                     if (this->current_image_.data){
//                         //ESP_LOGI(TAG, "Storing image length: %i", image.get()->get_data_length());
//                         memcpy(this->current_image_.data, image.get()->get_data_buffer() , data_length);
//                         this->current_image_.length =  data_length; 
//                         if (!xQueueSend(xFrameQueue, &this->current_image_, 30 / portTICK_PERIOD_MS)) {
//                             ESP_LOGE(TAG, "Failed to send camera image to queue");
//                         }
//                     } else {
//                         ESP_LOGE(TAG, "Failed allocate memory for image");
//                         this->current_image_.allocated_memory = 0;
//                     }
//                 } else {
//                     ESP_LOGD(TAG, "Dropping Frame ... process busy");
//                 }
                
//             }
//         });
//     }
//     return ESP_OK;     
// }

esp_err_t CamServer::set_key_value(std::string key, std::string value){
    if (key == "streaming"){
        this->streaming = value == "true" ? true : false;
        return ESP_OK;
    }
    if (key == "snapshot"){
        this->snapshot_filename = this->webdav->get_snapshot_directory() + "/img" + std::to_string(this->snapshot_number) + ".jpg";
        this->snapshot = value == "true" ? true : false;
        this->snapshot_number++;
        this->webdav->persist_i32("snapshot_number", this->snapshot_number);
        return ESP_OK;
    }
    if (key == "video"){
        this->video_filename = this->webdav->get_video_directory() + "/vid" + std::to_string(this->video_number) + ".avi";
        this->video_end = esp_timer_get_time() + this->video_time;
        this->video = value == "true" ? true : false;
        this->video_number++;
        this->webdav->persist_i32("video_number", this->video_number);
        return ESP_OK;
    }
    return ESP_FAIL;
}

void CamServer::register_server(httpd_handle_t server)
{

    httpd_uri_t uri_stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = cam_handler,
        .user_ctx = this,
    };
    
    httpd_register_uri_handler(server, &uri_stream);
    
    this->camera->add_image_callback([this](std::shared_ptr<esp32_camera::CameraImage> image) {
        if (this->streaming){
            esp_err_t ret = ESP_OK;
            char *part_buf[128];
            int data_length =  image.get()->get_data_length();

            if (ret == ESP_OK) {
                ret = httpd_resp_send_chunk(this->req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG,"Failed to send boundary chunk ret: %d", ret);
                    this->streaming = false;
                }
            }
            if (ret == ESP_OK) {
                size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, data_length, 0, 0);
                ret = httpd_resp_send_chunk(this->req, (const char *)part_buf, hlen);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG,"Failed to send part chunk ret: %d", ret);
                    this->streaming = false;
                }            
            }
            if (ret == ESP_OK) {
                ret = httpd_resp_send_chunk(this->req, (char*)image.get()->get_data_buffer(), data_length);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG,"Failed to send data chunk ret: %d", ret);
                    this->streaming = false;
                }            
            }
        }
        if (this->snapshot){
            this->webdav->get_sdmmc()->write_file(this->snapshot_filename.c_str(), image.get()->get_data_length(), (char*)image.get()->get_data_buffer());
            this->snapshot = false;
        }
        if (this->video){
            this->webdav->get_sdmmc()->write_avi(this->video_filename.c_str(), image.get()->get_data_length(), (char*)image.get()->get_data_buffer());
            if (esp_timer_get_time() > this->video_end){
                this->webdav->get_sdmmc()->write_avi(this->video_filename.c_str(), 0, NULL);
                this->video = false;
            }

        }
    });


    // if (this->camera_){
    //     sensor_t *s = esp_camera_sensor_get();
    //     xFrameQueue = xQueueCreate(1, sizeof(camera_image_data_t));
    //     BaseType_t xReturned = xTaskCreatePinnedToCore(frame_process, "Frame Streamer",  4096, this, 2, &xHandle, 1);
    //     if( xReturned != pdPASS ) {
    //         ESP_LOGE(TAG,"Failed top create task for TFLite");
    //         //return ESP_FAIL;
    //     }
    //     this->camera_->add_image_callback([this](std::shared_ptr<esp32_camera::CameraImage> image) {
    //         if (this->listeners.size() > 0){
    //             if (uxQueueSpacesAvailable(xFrameQueue) > 0){
    //                 int data_length =  image.get()->get_data_length();
    //                 //ESP_LOGI(TAG, "%d byte image from camera, someones listening and theres space on the queue...", data_length);

    //                 if (this->current_image_.data == NULL){
    //                     //ESP_LOGI(TAG, "Allocating initial memory of %i", data_length); 
    //                     this->current_image_.data = (uint8_t *) heap_caps_malloc(data_length, MALLOC_CAP_SPIRAM);
    //                     this->current_image_.allocated_memory = data_length;
    //                 }
    //                 if (this->current_image_.allocated_memory < data_length){
    //                     //ESP_LOGI(TAG, "Re-allocating memory... already allocated: %i",this->current_image_.allocated_memory); 
    //                     free(this->current_image_.data); 
    //                     this->current_image_.data = (uint8_t *) heap_caps_malloc( data_length, MALLOC_CAP_SPIRAM);
    //                     this->current_image_.allocated_memory = data_length;
    //                 }
    //                 if (this->current_image_.data){
    //                     //ESP_LOGI(TAG, "Storing image length: %i", image.get()->get_data_length());
    //                     memcpy(this->current_image_.data, image.get()->get_data_buffer() , data_length);
    //                     this->current_image_.length =  data_length; 
    //                     if (!xQueueSend(xFrameQueue, &this->current_image_, 30 / portTICK_PERIOD_MS)) {
    //                         ESP_LOGE(TAG, "Failed to send camera image to queue");
    //                     }
    //                 } else {
    //                     ESP_LOGE(TAG, "Failed allocate memory for image");
    //                     this->current_image_.allocated_memory = 0;
    //                 }
    //             } else {
    //                 ESP_LOGD(TAG, "Dropping Frame ... process busy");
    //             }
                
    //         }
    //     });
    // }

}

}
}

//#endif
