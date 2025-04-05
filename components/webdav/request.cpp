#include <string>

#include "request.h"

namespace esphome {
using namespace webdav;

bool Request::parseRequest() {
        std::string s;

        s = getHeader("Overwrite");
        if (!s.empty()) {
                if (s == "F")
                        overwrite = false;
                else if (s != "T")
                        return false;
        }

        s = getHeader("Depth");
        if (!s.empty()) {
                if (s == "0")
                        depth = DEPTH_0;
                else if (s == "1")
                        depth = DEPTH_1;
                else if (s != "infinity")
                        return false;
        }

        return true;
}

std::string Request::getDestination() {
        std::string destination = getHeader("Destination");
        std::string host = getHeader("Host");

        if (destination.empty() || host.empty())
                return "";

        size_t pos = destination.find(host);
        if (pos == std::string::npos)
                return "";

        return destination.substr(pos + host.length());
}
        std::string Request::getHeader(std::string name) {
                size_t len = httpd_req_get_hdr_value_len(req, name.c_str());
                if (len <= 0)
                        return "";

                std::string s;
                s.resize(len);
                httpd_req_get_hdr_value_str(req, name.c_str(), &s[0], len+1);

                return s;
        }

        size_t Request::getContentLength() {
                if (!req)
                        return 0;

                return req->content_len;
        }

        int Request::readBody(char *buf, int len)  {
                int ret = httpd_req_recv(req, buf, len);
                if (ret == HTTPD_SOCK_ERR_TIMEOUT)
                        /* Retry receiving if timeout occurred */
                        return 0;

                return ret;
        }

        httpd_req_t *Request::get_httpd_req(void){
                return req;
        }

}