#include "esp_log.h"
#include "tiny-json.h"
#include "webserver.h"
#include <dirent.h>
#include "esp_timer.h"
#include <esp_http_server.h>
// #define LWIP_STATS 1
// #define LWIP_STATS_DISPLAY 1 
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#ifdef WEBDAV_ENABLE_CAMERA
//#include "esphome/components/esp32_camera/esp32_camera.h"
#include "camserver.h"
#endif

static const char *TAG = "webdav Web";

#define PART_BOUNDARY "123456789000000000000987654321"

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

int res = 0;
int current_zoom = 0;

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
        else if (IS_FILE_EXT(filename, ".svg"))
        {
        return httpd_resp_set_type(req, "image/svg+xml");
        }
        else if (IS_FILE_EXT(filename, ".avi"))
        {
        return httpd_resp_set_type(req, "video/AV1");
        }
        /* For any other type always set as plain text */
        return httpd_resp_set_type(req, "text/plain");
}


namespace esphome {
namespace webdav {

std::string formatTimeETag(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", lt);
        return std::string(buf);
}

std::string formatTime(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", lt);
        return std::string(buf);
}

esp_err_t stream_avi_as_mpg(struct httpd_req *req){
        webdav::WebDav *server = (webdav::WebDav *)req->user_ctx;
        std::string path = server->get_mount_point() + req->uri;
        ESP_LOGI(TAG,"Streaming avi");
        esp_err_t res = ESP_OK;
        char *part_buf[128];
        
        size_t pos = path.find('?');
        if (pos != std::string::npos) {
                path.erase(pos);
        }

        struct stat status = {};
        FILE *fd;

        int ret = stat(path.c_str(), &status);
        if (ret != ESP_OK)
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
        res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        }

        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "Connection", "close");
        }
        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        }
        if (res != ESP_OK)
        {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed setting response cache-control header");
                ESP_LOGW(TAG, "Failed Setting response headers");
                return res;
        }
        AVIHeader *header;
        AVIChunk avi_chunk;
        MOVIHeader movi_header;
        int frames_sent = 0;
        const char movi[4] = {'m','o','v','i'};
        const char mjpg[4] = {'M','J','P','G'};
        FOURCC avi_list = {'L','I','S','T'};
        header = (AVIHeader *)malloc(sizeof(AVIHeader));
        
        size_t chunksize;
        chunksize = fread(header, 1, sizeof(AVIHeader), fd);
        ESP_LOGI(TAG, "Frames: %d Microsec per frame: %lu Buffer Size: %lu ",
                header->dwTotalFrames,
                header->dwMicroSecPerFrame,
                header->dwSuggestedBufferSize);
        
        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "X-Framerate", "60"); //Todo calculate framerate
        }
        if (memcmp(&header->dwStrh.fccHandler, &mjpg, 4) != 0){
                ESP_LOGE(TAG, "Codec not supported");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Codec not supported - only MJPG encoding");
                return ESP_FAIL;   
        }

        char *chunk;
        chunk = (char *)malloc(header->dwSuggestedBufferSize);

        if (chunk == NULL)
        {
                ESP_LOGE(TAG, "Failed to allocate buffer");
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate buffer");
                return ESP_FAIL;
        }

        fseek(fd, -12, SEEK_CUR);
        chunksize = fread(&avi_chunk, 1, 12, fd);
 
        while (true){
                if (memcmp(&avi_chunk.ckID, &avi_list, 4) == 0 &&
                        memcmp(&avi_chunk.ckData, &movi, 4) == 0 ){
                        //fseek(fd, -4, SEEK_CUR);
                        break;
                }
                ESP_LOGI(TAG, "Skiping %d bytes", avi_chunk.ckSize );
                chunksize = fread(chunk, 1, (int) avi_chunk.ckSize - 4, fd);
                if (chunksize < 1){
                        ESP_LOGE(TAG, "Could not find LIST in avi file");         
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode avi. LIST not found");
                        return ESP_FAIL;
                }

                chunksize = fread(&avi_chunk, 1, 12, fd);
                if (chunksize < 1){
                        ESP_LOGE(TAG, "Could not find LIST in avi file");         
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode avi. LIST not found");
                        return ESP_FAIL;
                }
                // ESP_LOGI(TAG, "Next %c%c%c%c ckData %c%c%c%c chSize %d ",
                //         avi_chunk.ckID[0],
                //         avi_chunk.ckID[1],
                //         avi_chunk.ckID[2],
                //         avi_chunk.ckID[3],
                //         avi_chunk.ckData[0],
                //         avi_chunk.ckData[1],
                //         avi_chunk.ckData[2],
                //         avi_chunk.ckData[3],
                //         avi_chunk.ckSize );
        }

        // if (memcmp(&header->dwMoviList, &avi_list, 4) != 0) {
        //         memcpy(&avi_chunk.ckID, &header->dwMoviList, 4);
        //         avi_chunk.ckSize = header->dwMoviSize + 4;
        //         ESP_LOGI(TAG, "Chunk is not LIST -- skipping %d", (int)header->dwMoviSize);
        //         while (!(memcmp(&avi_chunk.ckID, &avi_list, 4) == 0 &&
        //                memcmp(&avi_chunk.ckData, &movi, 4) == 0 )){
        //                 ESP_LOGI(TAG, "Skiping %d bytes", avi_chunk.ckSize );
        //                 chunksize = fread(chunk, 1, (int) avi_chunk.ckSize - 4, fd);
        //                 if (chunksize < 1){
        //                         ESP_LOGE(TAG, "Could not find LIST in avi file");         
        //                 }
        //                 ESP_LOGI(TAG, "Skipped..." );
        //                 chunksize = fread(&avi_chunk, 1, 12, fd);
        //                 ESP_LOGI(TAG, "Skipped..." );
        //                 ESP_LOGI(TAG, "Next %c%c%c%c ckData %c%c%c%c chSize %d ",
        //                 avi_chunk.ckID[0],
        //                 avi_chunk.ckID[1],
        //                 avi_chunk.ckID[2],
        //                 avi_chunk.ckID[3],
        //                 avi_chunk.ckData[0],
        //                 avi_chunk.ckData[1],
        //                 avi_chunk.ckData[2],
        //                 avi_chunk.ckData[3],
        //                 avi_chunk.ckSize );
        //         } ;

        //         ESP_LOGI(TAG, "Found a List... Failing for now" );
                
        //         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to stream file");
        //         return ESP_FAIL;
        // }
        // while (true)
        // {
        //         ESP_LOGI(TAG, "Reading Chunk");
        //         chunksize = fread(&avi_chunk, 1, 12, fd);
        //         if (memcmp(&avi_chunk.ckID, &avi_list, 4) != 0) { //LIST
        //                 ESP_LOGI(TAG, "Chunk not LIST");
        //                 chunksize = fread(chunk, 1, avi_chunk.ckSize - 4, fd);
        //         }else{
        //              if (memcmp(&avi_chunk.ckData, &movi, 4) != 0) { //movi   
        //                 ESP_LOGI(TAG, "LIST is not movi");
        //                 chunksize = fread(chunk, 1, avi_chunk.ckSize - 4, fd);
        //              }else{
        //                 memcpy(&movi_header, &movi ,4);
        //                 fread(&movi_header.dwMoviType, 1, 8, fd);
        //                 break;
        //              }
        //         }
        // } 
        int64_t frame_timer = esp_timer_get_time();
        int64_t next_frame_time = frame_timer +  header->dwTotalFrames;
        int frame_delay;
        //ESP_LOGI(TAG, "Reading start of movi chunk");
        do
        {
                ESP_LOGI(TAG, "Reading movi header");
                chunksize = fread(&movi_header, 1, sizeof(MOVIHeader), fd);
                if (chunksize < 1){
                        ESP_LOGE(TAG, "Could not find movi in avi LIST");         
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode avi. No movi LIST");
                        return ESP_FAIL;
                }
                if ((int)movi_header.dwSize < 1) {
                        ESP_LOGE(TAG, "Invalid length in movi chunk");         
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to decode avi. Invalid length in movi chunk");
                        return ESP_FAIL;     
                }
                ESP_LOGI(TAG, "Reading movi chunk - size: %d",  movi_header.dwSize);
                chunksize = fread(chunk, 1, movi_header.dwSize, fd);
                if (chunksize > 0)
                {
                        //ESP_LOGI(TAG, "Sending Boundary");
                        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
                        if (res == ESP_OK) {
                                size_t hlen = snprintf((char *)part_buf, 128, STREAM_PART, chunksize, 0, 0);
                                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
                        }
                        frame_delay =  (int)(next_frame_time - esp_timer_get_time())/1000;
                        ESP_LOGI(TAG, "Frame Delay: %d ms",frame_delay);
                        if (frame_delay > 0){
                                vTaskDelay(frame_delay/ portTICK_PERIOD_MS); 
                        }

                        if (res == ESP_OK) {
                                res = httpd_resp_send_chunk(req,  chunk, chunksize);
                        }
                        if (res != ESP_OK)
                        {
                                //fclose(fd);
                                ESP_LOGE(TAG, "File sending failed!");
                                //httpd_resp_sendstr_chunk(req, NULL);
                                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                                //return ESP_FAIL;
                                frames_sent =  header->dwTotalFrames;
                        }
                        frames_sent++;
                        next_frame_time += header->dwMicroSecPerFrame;
                        //frame_delay = header->dwMicroSecPerFrame - (esp_timer_get_time() - frame_timer); 
                        //ESP_LOGI(TAG, "Sent frame %d of %d Time taken: %llu us Delay needed: %llu",frames_sent , header->dwTotalFrames, (esp_timer_get_time() - frame_timer), frame_delay );

                        //frame_timer = esp_timer_get_time();
                } 

        } while (chunksize != 0 && frames_sent < header->dwTotalFrames);

        fclose(fd);
        ESP_LOGI(TAG, "File sending complete: %s", req->uri);

        httpd_resp_send_chunk(req, NULL, 0);

        return res;
}

esp_err_t web_handler(struct httpd_req *req)
{
    webdav::WebDav *server = (webdav::WebDav *)req->user_ctx;
    std::string path;
    if (strcmp(req->uri, "/") == 0){
        path = server->get_mount_point() + "/" + server->get_web_directory() + "/" + server->get_home_page();
    }else{
        if (strncmp(req->uri, "/pictures/", 9) == 0){
                path = server->get_mount_point() + req->uri;

        } else{
                if (strncmp(req->uri, "/videos/", 8) == 0){
                        ESP_LOGI(TAG,"Videos request");
                        path = server->get_mount_point() + req->uri;
                        size_t qlen = httpd_req_get_url_query_len(req);
                        if (qlen > 0){
                                char * qbuf = (char*)malloc((int)qlen + 1);
                                char * qval = (char*)malloc((int)qlen);
                                esp_err_t s_res = ESP_OK;
                                httpd_req_get_url_query_str(req, qbuf, qlen + 1);
                                esp_err_t qret = httpd_query_key_value(qbuf, "format", qval, qlen);
                                if (strncmp(qval,"mpg", 3) == 0){
                                        ESP_LOGI(TAG,"Query format = %s", qval);
                                        //queue_request(req, stream_avi_as_mpg);
                                        if (server->queue_request(req, stream_avi_as_mpg) == ESP_OK) {
                                                return ESP_OK;
                                        } else {
                                                httpd_resp_set_status(req, "503 Busy");
                                                httpd_resp_sendstr(req, "<div> no workers available. server busy.</div>");
                                                return ESP_OK;
                                        }
                                        //s_res = stream_avi_as_mpg(req,path);
                                }
                                free(qbuf);
                                free(qval);                        
                                return s_res;
                        }
                } else{               
                        path = server->get_mount_point() + "/" + server->get_web_directory() + req->uri;
                }
        }
    }

    ESP_LOGI(TAG, "Web Handler uri: %s path: %s", req->uri, path.c_str());


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
        // if (res == ESP_OK)
        // {
        //         res = httpd_resp_set_hdr(req, "Connection", "close");
        // }
         if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");        
        }
        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "ETag", formatTimeETag(status.st_mtime).c_str());
        }
        if (res != ESP_OK)
        {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed setting response cache-control header");
                ESP_LOGW(TAG, "Failed Setting response headers");
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
                free(chunk);
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
        free(chunk);
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
        enum { MAX_FIELDS = 8 };
        json_t pool[ MAX_FIELDS ];
        int max_response_length = 16 * 1024;
        struct dirent *f;
        struct stat f_status ;

        json_t const* parent = json_create( message, pool, MAX_FIELDS );
        if ( parent == NULL ){
                ESP_LOGE(TAG, "failed to parse json msg: %s", message);
                return ESP_FAIL; 
        }
        json_t const* target = json_getProperty( parent, "target" );
        json_t const* command = json_getProperty( parent, "command" );
        json_t const* action = json_getProperty( parent, "action" );
        json_t const* value = json_getProperty( parent, "value" );
        //ESP_LOGI (TAG,"Command is %s",json_getValue(command));
        
        if (strcmp(json_getValue(target),"system") == 0){
                static constexpr size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
                size_t fds = max_clients;
                int client_fds[max_clients] = {0};


                esp_err_t ret = httpd_get_client_list(this->webdav_->get_http_server(), &fds, client_fds);
                uint8_t *json_response = (uint8_t *)calloc(1, max_response_length);
                if (json_response == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for response");
                        return ESP_ERR_NO_MEM;
                }
                out_pkt->payload = json_response;
                char *p = (char *)json_response;
                *p++ = '{';
                p += sprintf(p, "\"target\":\"system\",");
                ESP_LOGI(TAG, "Client list reurned %d clients", fds);
                p += sprintf(p, "\"network\":{");
                p += sprintf(p, "\"client_count\": %d,", fds);
                p += sprintf(p, "\"clients\": [", fds);        
                for (int i = 0; i < fds; i++) {
                        auto client_info = httpd_ws_get_fd_info(this->webdav_->get_http_server(), client_fds[i]);
                        if (i >0){
                                *p++ = ',';       
                        }
                        p += sprintf(p, "{\"client_fds\": %d},", client_fds[i]);
                        p += sprintf(p, "{\"is_websocket\": %d}",client_info == HTTPD_WS_CLIENT_WEBSOCKET? true : false);
                }
                p += sprintf(p, "]},", fds); 
                // p += sprintf(p, "\"sockets_used\": %d,", lwip_stats.sockets.used);
                // p += sprintf(p, "\"sockets_avail\": %d,", lwip_stats.sockets.avail);
                // p += sprintf(p, "\"sockets_errors\": %d,", lwip_stats.sockets.err);
                // printf("Sockets in use: %d\n", lwip_stats.sockets.used);
                // printf("Sockets available: %d\n", lwip_stats.sockets.avail);
                // printf("Socket errors: %d\n", lwip_stats.sockets.err);
                struct sockaddr_in peer;
                socklen_t peer_len = sizeof(peer);
                int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (socket_fd >= 0) {
                if (getpeername(socket_fd, (struct sockaddr*)&peer, &peer_len) == 0) {
                        ESP_LOGI(TAG, "Socket FD: %d, Connected to: %s:%d\n",
                        socket_fd, inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
                }
                }
                multi_heap_info_t heap_info;
                heap_caps_get_info(&heap_info, MALLOC_CAP_8BIT);
                p+= sprintf(p, "\"memory\": {");
                p+= sprintf(p, "\"free_heap\": %d,",esp_get_free_heap_size());
                p+= sprintf(p, "\"free_bytes\": %d,",heap_info.total_free_bytes  );
                p+= sprintf(p, "\"allocated_bytes\": %d,",heap_info.total_allocated_bytes );
                heap_caps_get_info(&heap_info, MALLOC_CAP_32BIT);
                p+= sprintf(p, "\"free_bytes_32\": %d,",heap_info.total_free_bytes  );
                p+= sprintf(p, "\"allocated_bytes_32\": %d",heap_info.total_allocated_bytes );
                p+= sprintf(p, "}");
                *p++ = '}';
                *p++ = 0;

                out_pkt->len = strlen((char*)json_response);
                out_pkt->payload = json_response;
                ret = httpd_ws_send_frame(req, out_pkt);
                free(json_response);
        
        }
        
        if (strcmp(json_getValue(target),"pictures") == 0){
                uint8_t *json_response = (uint8_t *)calloc(1, max_response_length);
                if (json_response == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for response");
                        return ESP_ERR_NO_MEM;
                }
                out_pkt->payload = json_response;
                char *p = (char *)json_response;
                *p++ = '{';
                p += sprintf(p, "\"target\":\"pictures\",");
                p += sprintf(p, "\"files\":[ ");
                char uri[64]; 
                char fulluri[64];
                this->sdmmc_->path_to_uri("pictures", uri, false);
                DIR *d = opendir(uri);
                if (d) {
                        struct dirent *de;
                        while ((de = readdir(d))) {
                                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                                        continue;
                                strcpy(fulluri, uri);
                                strcat(fulluri, de->d_name);
                                stat(fulluri, &f_status);
                                //ESP_LOGI(TAG, "File src: %s", fulluri) ;       
                                p += sprintf(p, "{\"src\":\"/pictures/%s\",",  de->d_name);
                                p += sprintf(p, "\"date\":\"%s\"},", formatTime( f_status.st_mtime).c_str());

                        }
                        closedir(d);
                }
                p --;
                p += sprintf(p, "]");
                *p++ = '}';
                *p++ = 0;
                out_pkt->len = strlen((char*)json_response);

                //ESP_LOGI(TAG, "response packet length: %d content\n %s",  out_pkt->len,(char*) json_response);

                out_pkt->payload = json_response;
                //return trigger_async_send(req->handle, req);
                esp_err_t ret = httpd_ws_send_frame(req, out_pkt);
                free(json_response);
        }

        if (strcmp(json_getValue(target),"videos") == 0){
                uint8_t *json_response = (uint8_t *)calloc(1, max_response_length);
                if (json_response == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for response");
                        return ESP_ERR_NO_MEM;
                }
                out_pkt->payload = json_response;
                char *p = (char *)json_response;
                *p++ = '{';
                p += sprintf(p, "\"target\":\"videos\",");
                p += sprintf(p, "\"files\":[ ");
                char uri[64]; 
                char fulluri[64];
                this->sdmmc_->path_to_uri("videos", uri, false);
                DIR *d = opendir(uri);
                if (d) {
                        struct dirent *de;
                        while ((de = readdir(d))) {
                                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                                        continue;
                                strcpy(fulluri, uri);
                                strcat(fulluri, de->d_name);
                                stat(fulluri, &f_status);
                                //ESP_LOGI(TAG, "File src: %s", fulluri) ; 
                                p += sprintf(p, "{\"name\":\"%s\",",  de->d_name);      
                                p += sprintf(p, "\"src\":\"/videos/%s\",",  de->d_name);
                                p += sprintf(p, "\"date\":\"%s\"},", formatTime( f_status.st_mtime).c_str());

                        }
                        closedir(d);
                }
                p --;
                p += sprintf(p, "]");
                *p++ = '}';
                *p++ = 0;
                out_pkt->len = strlen((char*)json_response);

                //ESP_LOGI(TAG, "response packet length: %d content\n %s",  out_pkt->len,(char*) json_response);

                out_pkt->payload = json_response;
                //return trigger_async_send(req->handle, req);
                esp_err_t ret = httpd_ws_send_frame(req, out_pkt);
                free(json_response);
        }
#ifdef WEBDAV_ENABLE_CAMERA
        if (strcmp(json_getValue(target),"camera") == 0){
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
                        json_t const* key = json_getProperty( parent, "key");
                        json_t const* value = json_getProperty( parent, "value");
                        ESP_LOGI (TAG,"update %s", json_getValue(key));

                        //log_i("%s = %d", variable, json_getInteger(value));
                        sensor_t *s = esp_camera_sensor_get();
                        int res = 0;

                        if (!strcmp(json_getValue(key), "framesize")) {
                                if (s->pixformat == PIXFORMAT_JPEG) {
                                        res = s->set_framesize(s, (framesize_t)json_getInteger(value));
                                }
                        } else if (!strcmp(json_getValue(key), "quality")) {
                                res = s->set_quality(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "contrast")) {
                                res = s->set_contrast(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "brightness")) {
                                res = s->set_brightness(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "saturation")) {
                                res = s->set_saturation(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "gainceiling")) {
                                res = s->set_gainceiling(s, (gainceiling_t)json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "colorbar")) {
                        res = s->set_colorbar(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "awb")) {
                        res = s->set_whitebal(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "agc")) {
                        res = s->set_gain_ctrl(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "aec")) {
                        res = s->set_exposure_ctrl(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "hmirror")) {
                        res = s->set_hmirror(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "vflip")) {
                        res = s->set_vflip(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "awb_gain")) {
                        res = s->set_awb_gain(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "agc_gain")) {
                        res = s->set_agc_gain(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "aec_value")) {
                        res = s->set_aec_value(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "aec2")) {
                        res = s->set_aec2(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "dcw")) {
                        res = s->set_dcw(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "bpc")) {
                        res = s->set_bpc(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "wpc")) {
                        res = s->set_wpc(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "raw_gma")) {
                        res = s->set_raw_gma(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "lenc")) {
                        res = s->set_lenc(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "special_effect")) {
                        res = s->set_special_effect(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "wb_mode")) {
                        res = s->set_wb_mode(s, json_getInteger(value));
                        } else if (!strcmp(json_getValue(key), "ae_level")) {
                        res = s->set_ae_level(s, json_getInteger(value));
                        // } else if (!strcmp(json_getValue(key), "streaming")) {
                        // res = this->camserver_->set_key_value(json_getValue(key), json_getValue(value));
                        }
                        else {
                                res = this->camserver_->set_key_value(json_getValue(key), json_getValue(value));
                                if (res != ESP_OK){ 
                                        ESP_LOGW(TAG, "Unknown command: %s", json_getValue(key));
                                        res = -1;
                                }
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
        }
#endif
        if (strcmp(json_getValue(target),"webdav") == 0){
                uint8_t *json_response = (uint8_t *)calloc(1, max_response_length);
                if (json_response == NULL) {
                        ESP_LOGE(TAG, "Failed to calloc memory for response");
                        return ESP_ERR_NO_MEM;
                }
                out_pkt->payload = json_response;
                char *p = (char *)json_response;
                *p++ = '{';
                p += sprintf(p, "\"target\":\"webdav\",");
                p += sprintf(p, "\"share_name\": \"%s\",", this->webdav_->get_share_name().c_str());
                p += sprintf(p, "\"share_directory\": \"%s\",", this->webdav_->get_share_name().c_str());        
                p += sprintf(p, "\"web_enabled\": \"%s\",", "");        
                p += sprintf(p, "\"web_directory\": \"%s\",", this->webdav_->get_web_directory().c_str());        
                p += sprintf(p, "\"home_page\": \"%s\",",  this->webdav_->get_home_page().c_str());        
                p += sprintf(p, "\"camera_enabled\": \"%s\",", "");        
                p += sprintf(p, "\"snapshot_directory\": \"%s\",",  this->webdav_->get_snapshot_directory().c_str());        
                p += sprintf(p, "\"video_directory\": \"%s\"",  this->webdav_->get_video_directory().c_str());        
                *p++ = '}';
                *p++ = 0;

                out_pkt->len = strlen((char*)json_response);
                out_pkt->payload = json_response;
                esp_err_t ret = httpd_ws_send_frame(req, out_pkt);
                free(json_response);
        
        }
        return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
        webdav::WebServer *server = (webdav::WebServer *)req->user_ctx;
        if (req->method == HTTP_GET) {
                ESP_LOGI(TAG, "Handshake done, new web socket connection was opened");
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
#ifdef WEBDAV_ENABLE_CAMERA    
        ESP_LOGD(TAG, "Starting http streaming server");
        this->camserver_ = new webdav::CamServer(this->webdav_);
        this->camserver_->camera = this->webdav_->get_camera();
        this->camserver_->register_server(server);
#endif 
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
