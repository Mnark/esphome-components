#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <esp_http_server.h>

#include "esphome/core/log.h"

#include "webdav.h"
#include "davserver.h"
//#ifdef WEBDAV_ENABLE_WEBSERVER
#include "webserver.h"
//#endif

#include "request-espidf.h"
#include "response-espidf.h"
//#include "tiny-json.h"

static const char *TAG = "webdav";

static unsigned visitors = 0;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    static const char * data = "Async data";
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        return ESP_ERR_NO_MEM;
    }
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        free(resp_arg);
    }
    return ret;
}

namespace esphome
{
namespace webdav
{

WebDav::WebDav() {}

WebDav::~WebDav() {}

void WebDav::setup()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = this->port_;
    config.ctrl_port = 32770;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 32;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.keep_alive_enable = true;
    config.keep_alive_interval = 3;
    config.keep_alive_idle = 3;
    config.keep_alive_count = 2; 

    ESP_LOGD(TAG, "Starting http server");

    esp_err_t err = httpd_start(&this->server, &config);
    if (err == ESP_OK)
    {
        webdav::DavServer *webDavServer = new webdav::DavServer(this);
        webDavServer->register_server(this->server);
#ifdef WEBDAV_ENABLE_WEBSERVER
        webdav::WebServer *webServer = new webdav::WebServer(this);
        webServer->register_server(this->server);
#ifdef WEBDAV_ENABLE_CAMERA    
    if (this->camera_){
        ESP_LOGD(TAG, "Starting http streaming server");
        this->webCamServer = new webdav::CamServer(this->camera_);
        err = this->webCamServer->start();
    }
#endif  
#endif
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start http server Error: %s", esp_err_to_name(err));
    }

  
}

void WebDav::on_shutdown()
{
    httpd_stop(this->server);
#ifdef WEBDAV_ENABLE_CAMERA    
    this->webCamServer->stop();
#endif    
    this->server = NULL;
}

void WebDav::dump_config()
{
    ESP_LOGCONFIG(TAG, "WebDav Server:");
    ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
    ESP_LOGCONFIG(TAG, "  Authentication: %s", (this->auth_ == NONE)?"NONE":"BASIC");
    ESP_LOGCONFIG(TAG, "  SD Card: %s", this->get_sdmmc_state().c_str());
    ESP_LOGCONFIG(TAG, "  Share Name: %s", this->share_name_.c_str());

    ESP_LOGCONFIG(TAG, "  Web Browsing: %s", this->web_enabled_?"Enabled":"Not Enabled");
    if (this->web_enabled_){
        ESP_LOGCONFIG(TAG, "    Home Page: %s", this->home_page_);
        ESP_LOGCONFIG(TAG, "    Web Directory: %s", this->web_directory_.c_str());
#ifdef WEBDAV_ENABLE_CAMERA
        ESP_LOGCONFIG(TAG, "  Camera: Available",this->camera_? "Available": "Not Available" );
#endif
    }
}

float WebDav::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

void WebDav::loop()
{
}

void WebDav::set_sdmmc(sdmmc::SDMMC *sdmmc)
{
    this->sdmmc_ = sdmmc;
    sdmmc::State state = this->sdmmc_->get_state();
}

sdmmc::SDMMC * WebDav::get_sdmmc(void){
    return this->sdmmc_;
}

std::string WebDav::get_sdmmc_state(){
    std::string response;
    sdmmc::State state = this->sdmmc_->get_state();
    //ESP_LOGI(TAG, "Sdmmc returns %d", (int)state);
    switch (state){
        case sdmmc::State::UNKNOWN:
            response = "UNKNOWN";
            break;
        case sdmmc::State::UNAVAILABLE:
            response = "UNAVAILABLE";
            break;
        case sdmmc::State::IDLE:
            response = "IDLE";
            break;
        case sdmmc::State::BUSY:
            response = "BUSY";
            break;            
        default:
            response = "UNKNOWN";
    }
    return response;
}

httpd_handle_t WebDav::get_http_server(void){
    return this->server;
}

#ifdef WEBDAV_ENABLE_CAMERA
void WebDav::set_camera(esp32_camera::ESP32Camera *camera)
{
    this->camera_ = camera;
}
#endif

void WebDav::set_web_enabled(bool enabled){
    this->web_enabled_ = enabled;
}

void WebDav::set_auth(WebDavAuth auth)
{
    this->auth_ = auth;
}

WebDavAuth WebDav::get_auth(void){
    return this->auth_;
}

void  WebDav::set_auth_credentials(std::string auth_credentials){
    this->auth_credentials_ = "Basic " + auth_credentials;
}

std::string WebDav::get_auth_credentials(){
    return this->auth_credentials_;   
}

void WebDav::set_web_directory(std::string web_directory){
    this->web_directory_ = web_directory;
    if (web_directory.ends_with("/")){
        this->web_uri_ = web_directory + "*";
    }else{
        this->web_uri_ = web_directory + "/*";
    }
}

std::string WebDav::get_web_directory(void){
    return  this->web_directory_;
}

void WebDav::set_home_page(const char *home_page)
{
    strcpy(this->home_page_, home_page);
}

void WebDav::set_share_name(std::string share_name)
{
    if (share_name.starts_with("/")){
        this->share_name_ = share_name;
    }else{
        this->share_name_ = "/" + share_name;
    }
}

std::string WebDav::get_mount_point(void)
{
    return this->sdmmc_->get_mount_point();
}

std::string  WebDav::get_web_uri(void){
    return this->web_uri_;
}

char *WebDav::get_home_page(void)
{
    ESP_LOGI(TAG, "Home page is %s", this->home_page_);
    return this->home_page_;
}
std::string WebDav::get_share_name(void)
{
    return this->share_name_;
}

}
}