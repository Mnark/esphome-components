#include <stdio.h>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <cctype>
#include <iomanip>
//#include "esp_netif.h"
#include <esp_http_server.h>

#include "file-utils.h"
#include "davserver.h"
#include "esp_log.h"
#include "tinyxml2.h"
#include "tiny-json.h"

#include "request-espidf.h"
#include "response-espidf.h"

static const char *TAG = "webdav SERVER";

namespace esphome {
//namespace webdav {
using namespace webdav;
// DavServer::DavServer(std::string rootPath, std::string rootURI, webdav::WebDav *webdav) :
//         rootPath(rootPath), rootURI(rootURI) {
//         this->webdav_ = webdav;
//         this->sdmmc_ = webdav->get_sdmmc();
//         }
DavServer::DavServer(webdav::WebDav *webdav)
        {
        this->webdav_ = webdav;        
        this->rootPath = webdav->get_mount_point().c_str();     
        this->rootURI = webdav->get_share_name().c_str();
        this->sdmmc_ = webdav->get_sdmmc();
        }

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
        /* This is a limited set only */
        /* For any other type always set as plain text */
        return httpd_resp_set_type(req, "text/plain");
        }
/* Set HTTP response content type according to file extension */
static const char* get_content_type_from_file(const char *filename)
{
        if (IS_FILE_EXT(filename, ".pdf")) {
                return "application/pdf";
        } else if (IS_FILE_EXT(filename, ".html")||IS_FILE_EXT(filename, ".htm")) {
                return "text/html";
        } else if (IS_FILE_EXT(filename, ".jpeg")||IS_FILE_EXT(filename, ".jpg")) {
                return "image/jpeg";
        } else if (IS_FILE_EXT(filename, ".ico")) {
                return "image/x-icon";
        } else if (IS_FILE_EXT(filename, ".txt")) {
                return "text/plain";
        }
        return "application/binary";
}

static std::string urlDecode(std::string str){
        std::string ret;
        char ch;
        int i, ii, len = str.length();

        for (i = 0; i < len; i++) {
                if (str[i] != '%') {
                        if(str[i] == '+')
                                ret += ' ';
                        else
                                ret += str[i];
                } else {
                        sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
                        ch = static_cast<char>(ii);
                        ret += ch;
                        i += 2;
                }
        }

        return ret;
}

static std::string urlEncode(const std::string &value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
                std::string::value_type c = (*i);

                // Keep alphanumeric and other accepted characters intact
                if (isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ' ') {
                        escaped << c;
                        continue;
                }

                // Any other characters are percent-encoded
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int((unsigned char) c);
                escaped << std::nouppercase;
        }

        return escaped.str();
}

static esp_err_t send_unauthorized_response(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESPHome Web Server\"");
    httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

esp_err_t webdav_handler(httpd_req_t *httpd_req)
{
    webdav::DavServer *server = (webdav::DavServer *)httpd_req->user_ctx;
    if (server->auth == 1)
    {
        //ESP_LOGD(TAG, "webdav handler with Authentication");
        char auth_header[128] = {0};

        // Retrieve the "Authorization" header
        if (httpd_req_get_hdr_value_str(httpd_req, "Authorization", auth_header, sizeof(auth_header)) == ESP_OK)
        {
            // ESP_LOGI(TAG, "Authorization Header: %s", auth_header);

            // Check if the received Authorization header matches the expected one
            //if (strcmp(auth_header, AUTH_CREDENTIALS) != 0)
            if (strcmp(auth_header, server->get_auth_credentials().c_str()) != 0)
            {
                send_unauthorized_response(httpd_req);
                ESP_LOGE(TAG, "Authorization Failed");
                return ESP_FAIL;
            }
        }
        else
        {
            send_unauthorized_response(httpd_req);
            ESP_LOGE(TAG, "Authorization Not Present");
            return ESP_FAIL;
        }
    }


    webdav::RequestEspIdf req(httpd_req, httpd_req->uri);
    webdav::ResponseEspIdf resp(httpd_req);
    int ret;

    if (!req.parseRequest())
    {
        resp.setStatus(400, "Invalid request");
        //resp.writeHeader("Connection", "close");
        resp.flushHeaders();
        resp.closeBody();
        return ESP_OK;
    }

    // httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");

    ESP_LOGI(TAG, "%s: >%s<", http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

    switch (httpd_req->method)
    {
    case HTTP_COPY:
        ret = server->doCopy(req, resp);
        break;
    case HTTP_DELETE:
        ret = server->doDelete(req, resp);
        break;
    case HTTP_GET:
        ret = server->doGet(req, resp);
        break;
    case HTTP_HEAD:
        ret = server->doHead(req, resp);
        break;
    case HTTP_LOCK:
        ret = server->doLock(req, resp);
        break;
    case HTTP_MKCOL:
        ret = server->doMkcol(req, resp);
        break;
    case HTTP_MOVE:
        ret = server->doMove(req, resp);
        ESP_LOGI(TAG, "Move returned %d", ret);
        break;
    case HTTP_OPTIONS:
        ret = server->doOptions(req, resp);
        break;
    case HTTP_PROPFIND:
        ret = server->doPropfind(req, resp);
        break;
    case HTTP_PROPPATCH:
        ret = server->doProppatch(req, resp);
        break;
    case HTTP_PUT:
        ret = server->doPut(req, resp);
        break;
    case HTTP_UNLOCK:
        ret = server->doUnlock(req, resp);
        break;
    default:
        ret = ESP_ERR_HTTPD_INVALID_REQ;
        break;
    }

    if (ret == 404){
        resp.setStatus(ret, "Not Found");
    } else {
        resp.setStatus(ret, "");
    }
    //resp.writeHeader("Connection", "close");
    resp.flushHeaders();
    resp.closeBody();
    //httpd_resp_set_hdr(req, "Connection", "close");
    
    if (ret < 300)
    {
        return ESP_OK;
    } 
    return ret;
}

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
// #ifdef WEBDAV_ENABLE_CAMERA
//     if (strcmp(req->uri, "/status") == 0){
//         //char status_json [] = "{}";
//         static char json_response[1024];

//         sensor_t *s = esp_camera_sensor_get();
//         char *p = json_response;
//         *p++ = '{';

//         // if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
//         //     for (int reg = 0x3400; reg < 0x3406; reg += 2) {
//         //     p += print_reg(p, s, reg, 0xFFF);  //12 bit
//         //     }
//         //     p += print_reg(p, s, 0x3406, 0xFF);

//         //     p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
//         //     p += print_reg(p, s, 0x3503, 0xFF);
//         //     p += print_reg(p, s, 0x350a, 0x3FF);   //10 bit
//         //     p += print_reg(p, s, 0x350c, 0xFFFF);  //16 bit

//         //     for (int reg = 0x5480; reg <= 0x5490; reg++) {
//         //     p += print_reg(p, s, reg, 0xFF);
//         //     }

//         //     for (int reg = 0x5380; reg <= 0x538b; reg++) {
//         //     p += print_reg(p, s, reg, 0xFF);
//         //     }

//         //     for (int reg = 0x5580; reg < 0x558a; reg++) {
//         //     p += print_reg(p, s, reg, 0xFF);
//         //     }
//         //     p += print_reg(p, s, 0x558a, 0x1FF);  //9 bit
//         // } else if (s->id.PID == OV2640_PID) {
//         //     p += print_reg(p, s, 0xd3, 0xFF);
//         //     p += print_reg(p, s, 0x111, 0xFF);
//         //     p += print_reg(p, s, 0x132, 0xFF);
//         // }

//         p += sprintf(p, "\"xclk\":%u,",s->xclk_freq_hz / 1000000);
//         p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
//         p += sprintf(p, "\"framesize\":%u,",  s->status.framesize);
//         p += sprintf(p, "\"quality\":%u,", s->status.quality);
//         p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
//         p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
//         p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
//         p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
//         p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
//         p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
//         p += sprintf(p, "\"awb\":%u,", s->status.awb);
//         p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
//         p += sprintf(p, "\"aec\":%u,", s->status.aec);
//         p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
//         p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
//         p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
//         p += sprintf(p, "\"agc\":%u,", s->status.agc);
//         p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
//         p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
//         p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
//         p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
//         p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
//         p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
//         p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
//         p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
//         p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
//         // #if CONFIG_LED_ILLUMINATOR_ENABLED
//         // p += sprintf(p, ",\"led_intensity\":%u", led_duty);
//         // #else
//         // p += sprintf(p, ",\"led_intensity\":%d", -1);
//         // #endif
//         *p++ = '}';
//         *p++ = 0;
//         httpd_resp_set_type(req, "application/json");
//         httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
//         return httpd_resp_send(req, json_response, strlen(json_response));
//         // httpd_resp_set_type(req, "application/json");
//         // httpd_resp_set_status(req, "200 OK");
//         // httpd_resp_send(req, status_json, strlen(status_json));
//         // return ESP_OK;
//     }
// #endif

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
    ESP_LOGI(TAG, "File sending complete");

    httpd_resp_send_chunk(req, NULL, 0);

    //httpd_resp_set_hdr(req, "Connection", "close");

    return ESP_OK;
}


static esp_err_t ws_handler(httpd_req_t *req)
{
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
        ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
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
        ESP_LOGI(TAG, "Got packet type %d with message: %s", ws_pkt.type, ws_pkt.payload);


        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT){
                enum { MAX_FIELDS = 4 };
                json_t pool[ MAX_FIELDS ];

                json_t const* parent = json_create( (char *) ws_pkt.payload, pool, MAX_FIELDS );
                if ( parent == NULL ){
                ESP_LOGE(TAG, "failed to parse json msg: %s", ws_pkt.payload);
                return ESP_FAIL; 
                //return EXIT_FAILURE;
                }
                json_t const* command = json_getProperty( parent, "command" );
                ESP_LOGI (TAG,"Command is %s",json_getValue(command));
        #ifdef WEBDAV_ENABLE_CAMERA
                if (strcmp(json_getValue(command),"get") == 0){
                free(buf);
                static char json_response[1024];
                sensor_t *s = esp_camera_sensor_get();
                char *p = json_response;
                *p++ = '{';

                // if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
                //     for (int reg = 0x3400; reg < 0x3406; reg += 2) {
                //     p += print_reg(p, s, reg, 0xFFF);  //12 bit
                //     }
                //     p += print_reg(p, s, 0x3406, 0xFF);

                //     p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
                //     p += print_reg(p, s, 0x3503, 0xFF);
                //     p += print_reg(p, s, 0x350a, 0x3FF);   //10 bit
                //     p += print_reg(p, s, 0x350c, 0xFFFF);  //16 bit

                //     for (int reg = 0x5480; reg <= 0x5490; reg++) {
                //     p += print_reg(p, s, reg, 0xFF);
                //     }

                //     for (int reg = 0x5380; reg <= 0x538b; reg++) {
                //     p += print_reg(p, s, reg, 0xFF);
                //     }

                //     for (int reg = 0x5580; reg < 0x558a; reg++) {
                //     p += print_reg(p, s, reg, 0xFF);
                //     }
                //     p += print_reg(p, s, 0x558a, 0x1FF);  //9 bit
                // } else if (s->id.PID == OV2640_PID) {
                //     p += print_reg(p, s, 0xd3, 0xFF);
                //     p += print_reg(p, s, 0x111, 0xFF);
                //     p += print_reg(p, s, 0x132, 0xFF);
                // }

                p += sprintf(p, "\"xclk\":%u,",s->xclk_freq_hz / 1000000);
                p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
                p += sprintf(p, "\"framesize\":%u,",  s->status.framesize);
                p += sprintf(p, "\"quality\":%u,", s->status.quality);
                p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
                p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
                p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
                p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
                p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
                p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
                p += sprintf(p, "\"awb\":%u,", s->status.awb);
                p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
                p += sprintf(p, "\"aec\":%u,", s->status.aec);
                p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
                p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
                p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
                p += sprintf(p, "\"agc\":%u,", s->status.agc);
                p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
                p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
                p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
                p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
                p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
                p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
                p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
                p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
                p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
                // #if CONFIG_LED_ILLUMINATOR_ENABLED
                // p += sprintf(p, ",\"led_intensity\":%u", led_duty);
                // #else
                // p += sprintf(p, ",\"led_intensity\":%d", -1);
                // #endif
                *p++ = '}';
                *p++ = 0;
                ws_pkt.len = strlen(json_response);
                ESP_LOGI(TAG, "response packet length: %d ",  ws_pkt.len);
                ws_pkt.payload = (uint8_t *)json_response;
                //return trigger_async_send(req->handle, req);
                ret = httpd_ws_send_frame(req, &ws_pkt);
                if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
                }
                //free(buf);
                return ret;
                }
                else
                {
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
                }
        #endif            
        }
        ret = httpd_ws_send_frame(req, &ws_pkt);
        if (ret != ESP_OK) {
                ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
        }
        free(buf);

        }
        return ret;
}

esp_err_t web_options_handler(struct httpd_req *req)
{
        // ESP_LOGD(TAG, "Web Options Handler");
        httpd_resp_set_hdr(req, "Allow", "OPTIONS, GET, POST");
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
}

void DavServer::register_server(httpd_handle_t server)
{
    if (this->webdav_->get_auth() == BASIC)
    {
        this->setAuth(1);
    }

    char *uri;
//    asprintf(&uri, "%s/*?", this->rootURI);
    asprintf(&uri, "%s/*?", this->webdav_->get_share_name().c_str());

    httpd_uri_t uri_dav = {
        .uri = uri,
        .method = http_method(0),
        .handler = webdav_handler,
        .user_ctx = this,
    };

    httpd_uri_t uri_web = {
        .uri = "/*",
        //.uri = this->webdav_->get_web_uri().c_str(),
        .method = HTTP_GET,
        .handler = web_handler,
        .user_ctx = this->webdav_,
    };

    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    
    httpd_uri_t uri_web_options = {
        .uri = "/*",
        .method = HTTP_OPTIONS,
        .handler = web_options_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t uri_web_propfind = {
        .uri = "/",
        .method = HTTP_PROPFIND,
        .handler = webdav_handler,
        .user_ctx = this,
    };

    http_method methods[] = {
        HTTP_COPY,
        HTTP_DELETE,
        HTTP_GET,
        HTTP_HEAD,
        HTTP_LOCK,
        HTTP_MKCOL,
        HTTP_MOVE,
        HTTP_OPTIONS,
        HTTP_PROPFIND,
        HTTP_PROPPATCH,
        HTTP_PUT,
        HTTP_UNLOCK,
    };

    for (int i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    {
        uri_dav.method = methods[i];
        ESP_LOGD(TAG, "Registering handler for %s ", uri_dav.uri);
        httpd_register_uri_handler(server, &uri_dav);
    }
    httpd_register_uri_handler(server, &uri_ws);
    httpd_register_uri_handler(server, &uri_web);
    //httpd_register_uri_handler(server, &uri_capture);
    httpd_register_uri_handler(server, &uri_web_options);
    httpd_register_uri_handler(server, &uri_web_propfind);
}

std::string DavServer::uriToPath(std::string uri) {
        if (uri.find(rootURI) != 0)
                return rootPath;

        std::string path = rootPath + uri.substr(rootURI.length());
        while (path.substr(path.length()-1, 1) == "/")
                path = path.substr(0, path.length()-1);

        return urlDecode(path);
}

std::string DavServer::pathToURI(std::string path) {
        if (path.find(rootPath) != 0)
                return "";

        const char *sep = path[rootPath.length()] == '/' ? "" : "/";
        std::string uri = rootURI + sep + path.substr(rootPath.length());

        return urlEncode(uri);
}

std::string DavServer::formatTime(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", lt);
        return std::string(buf);
}

std::string DavServer::formatTimeTxt(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", lt);
        return std::string(buf);
}

std::string DavServer::formatTimeETag(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", lt);
        return std::string(buf);
}

static void xmlElement(std::ostringstream &s, const char *name, const char *value) {
        if (value == ""){
                s << "<" << name << "/>";
        }else{
                s << "<" << name << ">" << value << "</" << name << ">";
        }
}

void DavServer::sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr) {
        std::ostringstream s;

        s << "<D:response>";
        xmlElement(s, "D:href", msr.href.c_str());
        xmlElement(s, "D:status", msr.status.c_str());
 
        if (msr.path == "/") {
                s << "<D:propstat><D:prop>";
                xmlElement(s, "D:quota-available-bytes",  std::to_string(msr.quota_available_bytes).c_str());
                xmlElement(s, "D:quota-used-bytes",  std::to_string(msr.quota_used_bytes).c_str());
                // s << "<D:quota-available-bytes>1000000</D:quota-available-bytes>";
                // s << "<D:quota-used-bytes>10000</D:quota-used-bytes>";
                s << "</D:prop></D:propstat>";              
        }else{
                s << "<D:propstat><D:prop>";

                for (const auto &p: msr.props)
                        xmlElement(s, p.first.c_str(), p.second.c_str());

                xmlElement(s, "D:resourcetype", msr.isCollection ? "<D:collection/>" : "");
                // s << "<D:supportedlock>";
                // s << "<D:lockentry>";
                // s << "<D:lockscope>";
                // s << "<D:exclusive/>";
                // s << "</D:lockscope>";
                // s << "<D:locktype>";        
                // s << "<D:write/>";
                // s << "</D:locktype>";
                // s << "</D:lockentry>";
                // s << "<D:lockentry>";
                // s << "<D:lockscope>";
                // s << "<D:shared/>";
                // s << "</D:lockscope>";
                // s << "<D:locktype>";
                // s << "<D:write/>";
                // s << "</D:locktype>";
                // s << "</D:lockentry>";
                // s << "</D:supportedlock>";

                s << "</D:prop>";
                s << "</D:propstat></D:response>";

        }

        resp.sendChunk(s.str().c_str());
}

int  DavServer::sendRootPropResponse(Response &resp) {
        using namespace tinyxml2;

        XMLDocument respXML;
        XMLElement * oRoot = respXML.NewElement("D:multistatus");
        oRoot->SetAttribute("xmlns:D", "DAV");
        XMLNode * oResponse = respXML.NewElement("D:response");
        XMLElement * oHref = respXML.NewElement("D:href");
        oHref->SetText("/");
        XMLNode * oPropstat = respXML.NewElement("D:propstat");
        XMLNode * oProp = respXML.NewElement("D:prop");
        XMLElement * oStatus = respXML.NewElement("D:status");
        oStatus->SetText("HTTP/1.1 200 OK");
        XMLElement * oAvailable = respXML.NewElement("D:quota-available-bytes");
        oAvailable->SetText(this->sdmmc_->get_free_capacity());
        XMLElement * oUsed = respXML.NewElement("D:quota-used-bytes");
        oUsed->SetText(this->sdmmc_->get_used_capacity());

        XMLNode * o2Response = respXML.NewElement("D:response");
        XMLElement * o2Href = respXML.NewElement("D:href");
        o2Href->SetText("/dav");
        XMLNode * o2Propstat = respXML.NewElement("D:propstat");
        XMLNode * o2Prop = respXML.NewElement("D:prop");
        XMLElement * o2Status = respXML.NewElement("D:status");
        oStatus->SetText("HTTP/1.1 200 OK");
        XMLElement * o2Available = respXML.NewElement("D:quota-available-bytes");
        o2Available->SetText(this->sdmmc_->get_free_capacity());
        XMLElement * o2Used = respXML.NewElement("D:quota-used-bytes");
        o2Used->SetText(this->sdmmc_->get_used_capacity()); 

        oProp->InsertFirstChild(oAvailable);
        oProp->InsertFirstChild(oUsed);
        oPropstat->InsertFirstChild(oProp);
        oResponse->InsertFirstChild(oPropstat);
        oResponse->InsertFirstChild(oHref);
        oRoot->InsertFirstChild(oResponse);

        o2Prop->InsertFirstChild(o2Available);
        o2Prop->InsertFirstChild(o2Used);
        o2Propstat->InsertFirstChild(o2Prop);
        o2Response->InsertFirstChild(o2Propstat);
        o2Response->InsertFirstChild(o2Href);
        oRoot->InsertEndChild(o2Response);

        respXML.InsertFirstChild(oRoot);
        respXML.InsertFirstChild(respXML.NewDeclaration());
        XMLPrinter printer;
        respXML.Accept( &printer );
        ESP_LOGI(TAG, "Output:\n%s\n", printer.CStr());

        resp.setStatus(207, "Multi-Status");
        resp.setContentType("text/xml; charset=\"utf-8\"");
        resp.sendChunk( printer.CStr());
        resp.closeChunk(); 
        return 207;            
}

int DavServer::sendPropResponse(Response &resp, std::string path, int recurse) {
        struct stat sb;

        int ret = stat(uriToPath(path).c_str(), &sb);
        if (ret < 0){
                ESP_LOGE(TAG,"sendPropResponse stat failed Error: %d ErrNo: %d", ret, -errno);
                return -errno;
        }

        std::string uri = uriToPath(path);

        MultiStatusResponse r;

 
        r.href = path;          

        r.path = path;
        r.status = "HTTP/1.1 200 OK",
        r.quota_available_bytes = 1234567;
        r.quota_used_bytes = 123456;


        r.props["D:getlastmodified"] = formatTimeTxt(sb.st_mtime);
        r.props["D:displayname"] = basename(path.c_str());
        r.props["D:creationdate"] = formatTime(sb.st_ctime);
        r.props["D:ishidden"] = "0";
        r.props["D:getcontentlanguage"] = "";
        //r.props["D:lockdiscovery"] = "";

        r.isCollection = ((sb.st_mode & S_IFMT) == S_IFDIR);

        if (!r.isCollection) {
                r.props["D:getcontentlength"] = std::to_string(sb.st_size);
                r.props["D:getcontenttype"] = get_content_type_from_file(basename(path.c_str()));
                //r.props["D:getcontenttype"] = "application/binary";
                r.props["D:getetag"] = formatTimeETag(sb.st_mtime);
                r.props["D:iscollection"] = "0";
        } else {
                r.props["D:getcontentlength"] = "0";
                r.props["D:getcontenttype"] = "";
                r.props["D:iscollection"] = "1";
                r.props["D:getetag"] = "";    
        }

        sendMultiStatusResponse(resp, r);

        if (r.isCollection && recurse > 0) {
                DIR *dir = opendir(uri.c_str());
                //DIR *dir = opendir(path.c_str());
                if (dir) {
                        struct dirent *de;

                        while ((de = readdir(dir))) {
                                if (strcmp(de->d_name, ".") == 0 ||
                                    strcmp(de->d_name, "..") == 0)
                                        continue;

                                std::string rpath = path + "/" + de->d_name;
                                sendPropResponse(resp, rpath, recurse-1);
                        }

                        closedir(dir);
                }
        }

        return 0;
}

int DavServer::doCopy(Request &req, Response &resp) {
        if (req.getDestination().empty())
                return 400;

        if (req.getPath() == req.getDestination())
                return 403;

        int recurse =
                (req.getDepth() == Request::DEPTH_0) ? 0 :
                (req.getDepth() == Request::DEPTH_1) ? 1 :
                32;

        std::string destination = uriToPath(req.getDestination());
        bool destinationExists = access(destination.c_str(), F_OK) == 0;

        int ret = copy_recursive(req.getPath(), destination, recurse, req.getOverwrite());

        switch (ret) {
        case 0:
                if (destinationExists)
                        return 204;

                return 201;

        case -ENOENT:
                return 409;

        case -ENOSPC:
                return 507;

        case -ENOTDIR:
        case -EISDIR:
        case -EEXIST:
                return 412;

        default:
                return 500;
        }
        
        return 0;
}

int DavServer::doDelete(Request &req, Response &resp) {
        if (req.getDepth() != Request::DEPTH_INFINITY)
                return 400;

        int ret = rm_rf(uriToPath(req.getPath()).c_str());
        if (ret < 0)
                return 404;

        return 200;
}

int DavServer::doGet(Request &req, Response &resp) {
        ESP_LOGI(TAG,"Get request for %s", this->uriToPath(req.getPath()).c_str());

        struct stat sb;
        int ret = stat(uriToPath(req.getPath()).c_str(), &sb);
        if (ret < 0)
                return 404;

        FILE *f = fopen(uriToPath(req.getPath()).c_str(), "r");
        if (!f)
                return 500;

        resp.setHeader("Content-Length", sb.st_size);
        resp.setHeader("ETag", sb.st_ino);
        resp.setHeader("Last-Modified", formatTime(sb.st_mtime));
        resp.setHeader("Content-Type", get_content_type_from_file(this->uriToPath(req.getPath()).c_str()));

        // resp.flush();

        ret = 0;

        const int chunkSize = 8192;
        char *chunk = (char *) malloc(chunkSize);

        for (;;) {
                size_t r = fread(chunk, 1, chunkSize, f);
                if (r <= 0)
                        break;

                if (!resp.sendChunk(chunk, r)) {
                        ret = -1;
                        break;
                }
        }

        free(chunk);
        fclose(f);
        resp.closeChunk();

        if (ret == 0)
                return 200;

        return 500;
}

int DavServer::doHead(Request &req, Response &resp) {
        struct stat sb;
        int ret = stat(this->uriToPath(req.getPath()).c_str(), &sb);
        if (ret < 0)
                return 404;

        resp.setHeader("Content-Length", sb.st_size);
        resp.setHeader("ETag", sb.st_ino);
        resp.setHeader("Last-Modified", formatTime(sb.st_mtime));

        return 200;
}

int DavServer::doLock(Request &req, Response &resp) {
        char href[255] = "";
        strcat (href, req.getPath().c_str());

        resp.setStatus(200, "OK");
        resp.setContentType("text/xml; charset=\"utf-8\"");

        resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock>");
        resp.sendChunk("<D:locktype><D:write/></D:locktype><D:lockscope><D:exclusive/></D:lockscope><D:depth>Infinity</D:depth><D:owner><D:href>");
        resp.sendChunk(href);
        resp.sendChunk("</D:href></D:owner><D:timeout>Second-345600</D:timeout><D:locktoken><D:href>");
        resp.sendChunk("opaquelocktoken:e71d4fae-5dec-22df-fea5-00a0c93bd5eb1");
        resp.sendChunk("</D:href></D:locktoken></D:activelock></D:lockdiscovery></D:prop>");
        resp.closeChunk();

        return 200;
        // <?xml version="1.0" encoding="utf-8" ?>
        // <d:prop xmlns:d="DAV:">
        //   <d:lockdiscovery>
        //     <d:activelock>
        //       <d:locktype><d:write/></d:locktype>
        //       <d:lockscope><d:exclusive/></d:lockscope>
        //       <d:depth>Infinity</d:depth>
        //       <d:owner>
        //         <d:href>https://www.contoso.com/~user/contact.htm</d:href>
        //       </d:owner>
        //       <d:timeout>Second-345600</d:timeout>
        //       <d:locktoken>
        //         <d:href>opaquelocktoken:e71d4fae-5dec-22df-fea5-00a0c93bd5eb1</d:href>
        //       </d:locktoken>
        //     </d:activelock>
        //   </d:lockdiscovery>
        // </d:prop>

}

int DavServer::doMkcol(Request &req, Response &resp) {
        if (req.getContentLength() != 0)
                return 415;

        int ret = mkdir(this->uriToPath(req.getPath()).c_str(), 0755);
        if (ret == 0)
                return 201;

        switch (errno) {
        case EEXIST:
                return 405;

        case ENOENT:
                return 409;

        default:
                return 500;
        }
}

int DavServer::doMove(Request &req, Response &resp) {
        if (req.getDestination().empty())
                return 400;

        ESP_LOGI(TAG,"Move From: %s To: %s",this->uriToPath(req.getPath()).c_str(), req.getDestination().c_str());        
        struct stat sourceStat;
        int ret = stat(this->uriToPath(req.getPath()).c_str(), &sourceStat);
        if (ret < 0)
                return 404;

        std::string destination = uriToPath(req.getDestination());
        ESP_LOGI(TAG,"Move From: %s To: %s",
                this->uriToPath(req.getPath()).c_str(), 
                destination.c_str());        
        if (this->uriToPath(req.getPath()) == destination){
                ESP_LOGI(TAG,"Nothing to do");
                return 201;
        }

        bool destinationExists = access(destination.c_str(), F_OK) == 0;

        if (destinationExists) {
                if (!req.getOverwrite())
                        return 412;

                rm_rf(destination.c_str());
        }

        ret = rename(this->uriToPath(req.getPath()).c_str(), destination.c_str());

        switch (ret) {
        case 0:
                if (destinationExists)
                        return 204;

                return 201;

        case -ENOENT:
                return 409;

        case -ENOSPC:
                return 507;

        case -ENOTDIR:
        case -EISDIR:
        case -EEXIST:
                return 412;

        default:
                return 500;
        }
}

int DavServer::doOptions(Request &req, Response &resp) {
        resp.setHeader("Allow", "OPTIONS, GET, HEAD, POST, PUT, DELETE, PROPFIND, PROPPATCH, MKCOL, MOVE, COPY");
        //resp.setContentType("text/xml");
        //resp.setHeader("Dav", "1");
        resp.setStatus(200, "OK");

        return 200;
}

int DavServer::doPropfind(Request &req, Response &resp) {
        //ESP_LOGI(TAG, "Propfind: getPath: %s rootPath: %s uriToPath: %s",
        // req.getPath().c_str(),
        // rootPath.c_str(),
        // this->uriToPath(req.getPath()).c_str()
        // );
        struct stat sb;
        int ret = stat(uriToPath(req.getPath()).c_str(), &sb);
        if (ret < 0)
                return 404;
        
        if (req.getPath() == "/"){
              sendRootPropResponse(resp);
              return 207;  
        }

        int recurse =
                (req.getDepth() == Request::DEPTH_0) ? 0 :
                (req.getDepth() == Request::DEPTH_1) ? 1 :
                32;

        resp.setStatus(207, "Multi-Status");
        //resp.setHeader("Transfer-Encoding", "chunked");
        resp.setContentType("text/xml; charset=\"utf-8\"");
        //resp.flushHeaders();

        resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");
        resp.sendChunk("<D:multistatus xmlns:D=\"DAV:\">\n");


        sendPropResponse(resp, req.getPath(), recurse);
        resp.sendChunk("</D:multistatus>\n");
        resp.closeChunk();

        return 207;
}

int DavServer::doProppatch(Request &req, Response &resp) {
        tinyxml2::XMLDocument reqXML;
        bool exists = access(this->uriToPath(req.getPath()).c_str(), R_OK) == 0;

        if (!exists)
                return 404;

        size_t bodyLength = req.getContentLength();
        ESP_LOGI(TAG, "getContentLength: %d", bodyLength );
        if (bodyLength > 0){
                char *inbody = (char *) malloc(bodyLength + 1);
                req.readBody(inbody, bodyLength);  

                tinyxml2::XMLError xerr = reqXML.Parse( inbody, bodyLength );
                if (xerr != tinyxml2::XML_SUCCESS){
                        ESP_LOGE(TAG, "Failed to Parse Proppatchrequest body");
                        resp.setStatus(500, "Failed to Parse Proppatch request body");      
                } else {
                        ESP_LOGI(TAG, "Body parsed succesfully" );  
                }
                tinyxml2::XMLDocument respXML;

                tinyxml2::XMLElement* update = reqXML.FirstChildElement( "D:propertyupdate" )->FirstChildElement( "D:set" )->FirstChildElement( "D:prop" );
                if (update == nullptr) {
                        ESP_LOGE(TAG, "Failed to Propertyupdate/set/prop Node"); 
                }

                tinyxml2::XMLElement * iField = update->FirstChildElement();
                tinyxml2::XMLNode * oProp = respXML.NewElement("D:prop");
                
                while (iField != nullptr)
                {
                        tinyxml2::XMLNode * oField = respXML.NewElement(iField->Name());
                        oProp->InsertEndChild(oField);
                        iField = iField->NextSiblingElement();
                }

                tinyxml2::XMLElement * oRoot = respXML.NewElement("D:propertyupdate");
                tinyxml2::XMLNode * oSet = respXML.NewElement("D:set");
                oSet->InsertFirstChild(oProp);
                oRoot->SetAttribute("xmlns:D", "DAV");
                oRoot->InsertFirstChild(oSet);

                respXML.InsertFirstChild(oRoot);
                //tinyxml2::XMLText* textNode = reqXML.FirstChild()->ToText();
                //ESP_LOGI(TAG, "Got set");
                //const char* title = textNode->Value();
                //ESP_LOGI(TAG, "Set prop: %s", title );
                respXML.InsertFirstChild(respXML.NewDeclaration());
                tinyxml2::XMLPrinter printer;
                respXML.Accept( &printer );
                ESP_LOGI(TAG, "Output: %s", printer.CStr());
                resp.setStatus(200, "OK");
                resp.setContentType("text/xml; charset=\"utf-8\"");
                resp.sendChunk( printer.CStr());
                resp.closeChunk();
                free(inbody);
        }        

        return 200;
}

int DavServer::doPut(Request &req, Response &resp) {
        bool exists = access(this->uriToPath(req.getPath()).c_str(), R_OK) == 0;
        FILE *f = fopen(this->uriToPath(req.getPath()).c_str(), "w");
        if (!f)
                return 404;

        int remaining = req.getContentLength();

        const int chunkSize = 8 * 1024;
        char *chunk = (char *) malloc(chunkSize);
        if (!chunk){
                ESP_LOGE(TAG,"Failed to allocate memory for PUT ");
                return 500;
        }

        int ret = 0;

        while (remaining > 0) {
                ESP_LOGD(TAG,"Writing to file... remaining: %d", remaining);
                int r, w;
                r = req.readBody(chunk, std::min(remaining, chunkSize));
                if (r <= 0)
                        break;

                w = fwrite(chunk, 1, r, f);
                if (w != r) {
                        ret = -errno;
                        break;
                }

                remaining -= w;
        }

        free(chunk);
        fclose(f);
        resp.closeChunk();

        if (ret < 0)
                return 500;

        if (exists)
                return 200;

        return 201;
}

int DavServer::doUnlock(Request &req, Response &resp) {
        return 200;
}

void DavServer::setAuth(int auth_level){
    this->auth =  auth_level;   
}

std::string DavServer::get_auth_credentials(){
    return this->webdav_->get_auth_credentials();   
}
//}
}