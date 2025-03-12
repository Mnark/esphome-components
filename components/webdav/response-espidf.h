#pragma once

#include <string>
#include <esp_http_server.h>

#include "response.h"

namespace esphome {
namespace webdav {
//static const char *TAG = "webdav Response-idf";
class ResponseEspIdf : public Response {
public:
        ResponseEspIdf(httpd_req_t *req) : req(req), status(NULL), chunked(false) {
                setDavHeaders();
                
               // tcpip_adapter_get_ip_info(tcpip_adapter_if_t tcpip_if, tcpip_adapter_ip_info_t *ip_info);
        }

        ~ResponseEspIdf() {
                free(status);
        }

        void setStatus(int code, std::string message) override {
                free(status);
                status = NULL;

                asprintf(&status, "%d %s", code, message.c_str());
                httpd_resp_set_status(req, status);
        }

        void writeHeader(const char *header, const char *value) override {
                httpd_resp_set_hdr(req, header, value);
        }

        void setContentType(const char *ct) override {
                httpd_resp_set_type(req, ct);
        }

        bool sendChunk(const char *buf, ssize_t len = -1) override {
                chunked = true;

                if (len == -1)
                        //ESP_LOGI(TAG, "Calculated Length: %d", strlen(buf));
                        len = strlen(buf);

                return httpd_resp_send_chunk(req, buf, len) == ESP_OK;
        }
 
        void closeChunk() override {
                httpd_resp_send_chunk(req, NULL, 0);
        }

        void closeBody() override {
                if (!chunked)
                        httpd_resp_send(req, "", 0);
        }

private:
        httpd_req_t *req;
        char *status;
        bool chunked;
        char client_addr_[32];
};

}
} // namespace