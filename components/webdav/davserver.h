#pragma once

#include "request.h"
#include "response.h"
#include "../sdmmc/sdmmc.h"

#include "webdav.h"

namespace esphome {
namespace webdav {
//class WebDav;

class DavServer {
public:
        //DavServer(std::string rootPath, std::string rootURI, webdav::WebDav *webdav);
        DavServer(webdav::WebDav *webdav);
        ~DavServer() {};

        void register_server(httpd_handle_t server);
        std::string pathToURI(std::string path);
        std::string uriToPath(std::string uri);

        int doCopy(Request &req, Response &resp);
        int doDelete(Request &req, Response &resp);
        int doGet(Request &req, Response &resp);
        int doHead(Request &req, Response &resp);
        int doLock(Request &req, Response &resp);
        int doMkcol(Request &req, Response &resp);
        int doMove(Request &req, Response &resp);
        int doOptions(Request &req, Response &resp);
        int doPropfind(Request &req, Response &resp);
        int doProppatch(Request &req, Response &resp);
        int doPut(Request &req, Response &resp);
        int doUnlock(Request &req, Response &resp);
        void setAuth(int auth_level);
        std::string get_auth_credentials();
        int auth = 0;

private:
        std::string rootPath, rootURI;

        std::string formatTime(time_t t);
        std::string formatTimeTxt(time_t t);
        std::string formatTimeETag(time_t t);
        int sendPropResponse(Response &resp, std::string path, int recurse);
        int sendRootPropResponse(Response &resp);
        void sendMultiStatusResponse(Response &resp, MultiStatusResponse &msr);
        sdmmc::SDMMC *sdmmc_;
        webdav::WebDav *webdav_;
        
};

}
} // namespace