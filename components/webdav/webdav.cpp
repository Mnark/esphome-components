#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "nvs.h"

#include "esphome/core/log.h"

#include "webdav.h"
#include "davserver.h"
#ifdef WEBDAV_ENABLE_WEBSERVER
#include "webserver.h"
#endif
#ifdef WEBDAV_ENABLE_CAMERA
#include "camserver.h"
#endif
#include "request-espidf.h"
#include "response-espidf.h"
#include <iomanip>
//#include "tiny-json.h"

static const char *TAG = "webdav";

static unsigned visitors = 0;

static QueueHandle_t request_queue;
static SemaphoreHandle_t worker_ready_count;
static TaskHandle_t worker_handles[CONFIG_WEBDAV_MAX_ASYNC_REQUESTS];

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

static void worker_task(void *p)
{
    ESP_LOGI(TAG, "starting async req task worker");

    while (true) {

        // counting semaphore - this signals that a worker
        // is ready to accept work
        xSemaphoreGive(worker_ready_count);

        // wait for a request
        httpd_async_req_t async_req;
        if (xQueueReceive(request_queue, &async_req, portMAX_DELAY)) {

            ESP_LOGI(TAG, "invoking %s", async_req.req->uri);

            // call the handler
            async_req.handler(async_req.req);

            // Inform the server that it can purge the socket used for
            // this request, if needed.
            if (httpd_req_async_handler_complete(async_req.req) != ESP_OK) {
                ESP_LOGE(TAG, "failed to complete async req");
            }
        }
    }

    ESP_LOGW(TAG, "worker stopped");
    vTaskDelete(NULL);
}

namespace esphome {
namespace webdav {

WebDav::WebDav() {}

WebDav::~WebDav() {}

void WebDav::setup()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    err = nvs_open("storage", NVS_READWRITE, &this->nvs_handle_);
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    // } else {
    //     printf("Done\n");

    //     // Read
    //     printf("Reading restart counter from NVS ... ");
    //     int32_t restart_counter = 0; // value will default to 0, if not set yet in NVS
    //     err = nvs_get_i32(this->nvs_handle_, "restart_counter", &restart_counter);
    //     switch (err) {
    //         case ESP_OK:
    //             printf("Done\n");
    //             printf("Restart counter = %" PRIu32 "\n", restart_counter);
    //             break;
    //         case ESP_ERR_NVS_NOT_FOUND:
    //             printf("The value is not initialized yet!\n");
    //             break;
    //         default :
    //             printf("Error (%s) reading!\n", esp_err_to_name(err));
    //     }

    //     // Write
    //     printf("Updating restart counter in NVS ... ");
    //     restart_counter++;
    //     err = nvs_set_i32(this->nvs_handle_, "restart_counter", restart_counter);
    //     printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    //     // Commit written value.
    //     // After setting any values, nvs_commit() must be called to ensure changes are written
    //     // to flash storage. Implementations may write to storage at other times,
    //     // but this is not guaranteed.
    //     printf("Committing updates in NVS ... ");
    //     err = nvs_commit(this->nvs_handle_);
    //     printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

    //     // Close
    //     nvs_close(this->nvs_handle_);
    }

    start_workers();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = this->port_;
    config.ctrl_port = 32770;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 32;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.backlog_conn = 5;
    config.keep_alive_enable = true;
    config.enable_so_linger = false;
    //config.keep_alive_interval = 3;
    //config.keep_alive_idle = 3;
    //config.keep_alive_count = 2; 

    ESP_LOGD(TAG, "Starting http server");

    err = httpd_start(&this->server, &config);
    if (err == ESP_OK)
    {
        webdav::DavServer *webDavServer = new webdav::DavServer(this);
        webDavServer->register_server(this->server);


#ifdef WEBDAV_ENABLE_WEBSERVER
        webdav::WebServer *webServer = new webdav::WebServer(this);
        webServer->register_server(this->server); 
#endif
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start http server Error: %s", esp_err_to_name(err));
    }

  
}

esp_err_t WebDav::persist_i32(const char *key, int32_t value)
{
    esp_err_t err = nvs_set_i32(this->nvs_handle_, key,  value);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Failed to save %s to nvs", key);
        return err;
    }
    err = nvs_commit(this->nvs_handle_);
        if (err != ESP_OK){
        ESP_LOGE(TAG, "Failed to commit %s to nvs", key);
        return err;
    }
    return ESP_OK;
}

esp_err_t WebDav::get_i32(const char *key, int32_t *out_value)
{
    return nvs_get_i32(this->nvs_handle_, key, out_value);
}

void WebDav::on_shutdown()
{
    httpd_stop(this->server);
    this->server = NULL;
    nvs_close(this->nvs_handle_);
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
        ESP_LOGCONFIG(TAG, "    Home Page: %s", this->home_page_.c_str());
        ESP_LOGCONFIG(TAG, "    Web Directory: %s", this->web_directory_.c_str());
#ifdef WEBDAV_ENABLE_CAMERA
        ESP_LOGCONFIG(TAG, "  Camera: Available",this->camera_? "Available": "Not Available" );
#endif
    }
}

float WebDav::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

std::string WebDav::formatTimeETag(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", lt);
        return std::string(buf);
}

std::string WebDav::formatTime(time_t t) {

        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", lt);
        return std::string(buf);
}

// void WebDav::loop()
// {
// }
void WebDav::update()
{
}
// start worker threads
void  WebDav::start_workers(void)
{

    // counting semaphore keeps track of available workers
    worker_ready_count = xSemaphoreCreateCounting(
        CONFIG_WEBDAV_MAX_ASYNC_REQUESTS,  // Max Count
        0); // Initial Count
    if (worker_ready_count == NULL) {
        ESP_LOGE(TAG, "Failed to create workers counting Semaphore");
        return;
    }

    // create queue
    request_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (request_queue == NULL){
        ESP_LOGE(TAG, "Failed to create request_queue");
        vSemaphoreDelete(worker_ready_count);
        return;
    }

    // start worker tasks
    for (int i = 0; i < CONFIG_WEBDAV_MAX_ASYNC_REQUESTS; i++) {

        bool success = xTaskCreate(worker_task, "async_req_worker",
                                    ASYNC_WORKER_TASK_STACK_SIZE, // stack size
                                    (void *)0, // argument
                                    ASYNC_WORKER_TASK_PRIORITY, // priority
                                    &worker_handles[i]);

        if (!success) {
            ESP_LOGE(TAG, "Failed to start asyncReqWorker");
            continue;
        }
    }
}

esp_err_t WebDav::queue_request(httpd_req_t *req, httpd_req_handler_t handler)
{
    // must create a copy of the request that we own
    httpd_req_t* copy = NULL;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK) {
        return err;
    }

    httpd_async_req_t async_req = {
        .req = copy,
        .handler = handler,
    };

    // How should we handle resource exhaustion?
    // In this example, we immediately respond with an
    // http error if no workers are available.
    int ticks = 0;

    // counting semaphore: if success, we know 1 or
    // more asyncReqTaskWorkers are available.
    if (xSemaphoreTake(worker_ready_count, ticks) == false) {
        ESP_LOGE(TAG, "No workers are available");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    // Since worker_ready_count > 0 the queue should already have space.
    // But lets wait up to 100ms just to be safe.
    if (xQueueSend(request_queue, &async_req, pdMS_TO_TICKS(100)) == false) {
        ESP_LOGE(TAG, "worker queue is full");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    return ESP_OK;
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

esp32_camera::ESP32Camera * WebDav::get_camera(void){
    return this->camera_;
}
void WebDav::set_snapshot_directory(std::string snapshot_directory){
    this->snapshot_directory_ = snapshot_directory;
}

std::string WebDav::get_snapshot_directory(void){
    return  this->snapshot_directory_;
}
void WebDav::set_video_directory(std::string video_directory){
    this->video_directory_ = video_directory;
}

std::string WebDav::get_video_directory(void){
    return  this->video_directory_;
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

void WebDav::set_home_page(std::string home_page)
{
    this->home_page_ = home_page;
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

std::string WebDav::get_home_page(void)
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