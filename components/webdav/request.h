#pragma once

#include <string>
#include "esp_log.h"
#include <esp_http_server.h>
namespace esphome {
namespace webdav {

class Request {
public:
        enum Depth {
                DEPTH_0 = 0,
                DEPTH_1 = 1,
                DEPTH_INFINITY = 2,
        };
        Request(httpd_req_t *req, std::string path) :
                path(path), req(req), depth(DEPTH_INFINITY), overwrite(true) {
                        if (this->path != "/" && this->path.ends_with("/")){
                                this->path.pop_back();
                        }
                }

        // Request(std::string path) : path(path), depth(DEPTH_INFINITY), overwrite(true) {
        //         if (this->path != "/" && this->path.ends_with("/")){
        //                 this->path.pop_back();
        //         }           
        // }
        // std::string getHeader(std::string name) override {
        //         size_t len = httpd_req_get_hdr_value_len(req, name.c_str());
        //         if (len <= 0)
        //                 return "";

        //         std::string s;
        //         s.resize(len);
        //         httpd_req_get_hdr_value_str(req, name.c_str(), &s[0], len+1);

        //         return s;
        // }

        // size_t getContentLength() override {
        //         if (!req)
        //                 return 0;

        //         return req->content_len;
        // }

        // int readBody(char *buf, int len) override {
        //         int ret = httpd_req_recv(req, buf, len);
        //         if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        //                 /* Retry receiving if timeout occurred */
        //                 return 0;

        //         return ret;
        // }
        bool parseRequest();
        std::string getDestination();

        std::string getPath() { return path; }
        enum Depth getDepth() { return depth; }
        bool getOverwrite() { return overwrite; }

        // Functions that depend on the underlying web server implementation
        std::string getHeader(std::string name);
        size_t getContentLength();
        int readBody(char *buf, int len);
        httpd_req_t *get_httpd_req(void);

protected:
        std::string path;
        enum Depth depth;
        bool overwrite;
        httpd_req_t *req;
};

}
} // namespace