#include <map>
#include <string>
#include <vector>

#include "response.h"
#include <time.h>
#include "esp_log.h"

static const char *TAG = "webdav Response";

namespace esphome {
using namespace webdav;

void Response::setDavHeaders() {
        time_t now;
        //char strftime_buf[64];
        struct tm timeinfo;

        //struct tm timeinfo;
        char dateString[20];

        setHeader("DAV", "1, 2");
        //setHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
        setHeader("Server", "ESPHome");
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(dateString, sizeof(dateString), "%a, %d %b %Y %H:%M:%S %Z", &timeinfo);
        setHeader("Date", dateString);
        setHeader("MS-Author-Via", "DAV");
}

void Response::setHeader(std::string header, std::string value) {
        headers[header] = value;
}

void Response::setHeader(std::string header, size_t value) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%zu", value);
        headers[header] = tmp;
}

void Response::flushHeaders() {
        for (const auto &h: headers)
                writeHeader(h.first.c_str(), h.second.c_str());
}


}