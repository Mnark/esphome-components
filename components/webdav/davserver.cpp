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
#include <ctime>
//#include "esp_netif.h"
#include <esp_http_server.h>


#include "file-utils.h"
#include "davserver.h"
#include "esp_log.h"
#include "tinyxml2.h"
//#include "tiny-json.h"

#include "request-espidf.h"
#include "response-espidf.h"
#include "esphome/components/esp32_camera/esp32_camera.h"

static const char *TAG = "webdav SERVER";

namespace esphome {
//namespace webdav {
using namespace webdav;
using namespace tinyxml2;
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

// static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename){
//         if (IS_FILE_EXT(filename, ".pdf"))
//         {
//         return httpd_resp_set_type(req, "application/pdf");
//         }
//         else if (IS_FILE_EXT(filename, ".html")|| IS_FILE_EXT(filename, ".htm"))
//         {
//         return httpd_resp_set_type(req, "text/html");
//         }
//         else if (IS_FILE_EXT(filename, ".jpeg") || IS_FILE_EXT(filename, ".jpg"))
//         {
//         return httpd_resp_set_type(req, "image/jpeg");
//         }
//         else if (IS_FILE_EXT(filename, ".ico"))
//         {
//         return httpd_resp_set_type(req, "image/x-icon");
//         }
//         else if (IS_FILE_EXT(filename, ".png"))
//         {
//         return httpd_resp_set_type(req, "image/png");
//         }

        
//         /* This is a limited set only */
//         /* For any other type always set as plain text */
//         return httpd_resp_set_type(req, "text/plain");
// }

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
        } else if (IS_FILE_EXT(filename, ".avi")) {
                return "video/AV1";
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

esp_err_t chunk_file_send(struct httpd_req *req){
        //ESP_LOGI("async", "Sending chunked file send");
        webdav::DavServer *davserver = (webdav::DavServer *)req->user_ctx;
        webdav::WebDav *server = davserver->get_webdav();
        
        //std::string path = server->get_mount_point() + req->uri;
        std::string path = davserver->uriToPath(req->uri);
        ESP_LOGI(TAG, "Path: %s", path.c_str());
        struct stat sb;
        int ret = stat(path.c_str(), &sb);

        FILE *f = fopen(path.c_str(), "r");
        if (!f)
                return ESP_FAIL;
        esp_err_t res = httpd_resp_set_status(req, "200 OK");
        std::string etag = server->formatTimeETag(sb.st_mtime);
        std::string lmod = server->formatTime(sb.st_mtime);
        res = httpd_resp_set_hdr(req, "Content-Length", std::to_string(sb.st_size).c_str());
        if (res == ESP_OK)
        {
                std::string etag = davserver->formatTimeETag(sb.st_mtime);
                res = httpd_resp_set_hdr(req, "ETag",  etag.c_str());        
        }
        if (res == ESP_OK)
        {
                res = httpd_resp_set_hdr(req, "Last-Modified", lmod.c_str());        
        }
        if (res == ESP_OK)
        {
                res = httpd_resp_set_type(req, get_content_type_from_file(path.c_str()));
        }

        ret = 0;

        const int chunkSize = 16 * 1024;
        char *chunk = (char *) malloc(chunkSize);
        for (;;) {
                size_t r = fread(chunk, 1, chunkSize, f);
                if (r <= 0)
                        break;
                if (httpd_resp_send_chunk(req, chunk, r) != ESP_OK)
                {
                        fclose(f);
                        free(chunk);
                        ESP_LOGE(TAG, "File sending failed!");
                        httpd_resp_sendstr_chunk(req, NULL);
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                        return ESP_FAIL;
                }
        }

        free(chunk);
        fclose(f);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
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


    //webdav::RequestEspIdf req(httpd_req, httpd_req->uri);
    webdav::Request req(httpd_req, httpd_req->uri);
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

    //ESP_LOGI(TAG, "%s: >%s<", http_method_str((enum http_method)httpd_req->method), httpd_req->uri);

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

    switch (ret){
        case 404:
                resp.setStatus(ret, "Not Found");
                break;
        case 304:
                resp.setStatus(ret, "Not Modified");
                break;
        case 0:
                return ESP_OK;
                break;        
        default:
                resp.setStatus(ret, "");
    }

    resp.flushHeaders();
    resp.closeBody();
    
    if (ret < 300 || ret == 304 || ret == 404)
    {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Dav handler returned %d", ret); 
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

static time_t parseDateTime(const char* datetimeString, const char* format)
{
    struct tm tmStruct;
    if (strptime(datetimeString, format, &tmStruct) == NULL){
        ESP_LOGE(TAG,"Could not extract datetime from string %s", datetimeString);
    }
    return mktime(&tmStruct);
}

webdav::WebDav*  DavServer::get_webdav(void){
        return this->webdav_;
}

void DavServer::register_server(httpd_handle_t server)
{
    if (this->webdav_->get_auth() == BASIC)
    {
        this->setAuth(1);
    }

    char *uri;
    asprintf(&uri, "%s/*?", this->webdav_->get_share_name().c_str());

    httpd_uri_t uri_dav = {
        .uri = uri,
        .method = http_method(0),
        .handler = webdav_handler,
        .user_ctx = this,
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
        struct tm *lt = gmtime(&t);
        //strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", lt);
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", lt);

        return std::string(buf);
}

std::string DavServer::formatTimeETag(time_t t) {
        char buf[32];
        struct tm *lt = localtime(&t);
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", lt);
        return std::string(buf);
}

static void xmlElementstr(std::ostringstream &s, const char *name, const char *value) {
        if (value == ""){
                s << "<" << name << "/>";
        }else{
                s << "<" << name << ">" << value << "</" << name << ">";
        }
}

int  DavServer::sendRootPropResponse(Response &resp) {
        //using namespace tinyxml2;

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
        // ESP_LOGI(TAG, "Output:\n%s\n", printer.CStr());

        resp.setStatus(207, "Multi-Status");
        resp.setContentType("text/xml; charset=\"utf-8\"");
        resp.sendChunk( printer.CStr());
        resp.closeChunk(); 
        return 207;            
}

int DavServer::xmlPropResponse(XMLDocument *respXML, XMLElement *oRoot, std::string path, int recurse) {
        struct stat sb;

        int ret = stat(uriToPath(path).c_str(), &sb);
        if (ret < 0){
                ESP_LOGE(TAG,"sendPropResponse stat failed Error: %d ErrNo: %d", ret, -errno);
                return -errno;
        }
        XMLElement * oResponse = respXML->NewElement("D:response");
        XMLElement * oHref = respXML->NewElement("D:href");
        oHref->SetText(path.c_str());
        oResponse->InsertEndChild(oHref);
        
        XMLElement * oStatus = respXML->NewElement("D:status");
        oStatus->SetText("HTTP/1.1 200 OK");
        oResponse->InsertEndChild(oStatus);

        //

        XMLNode * oPropstat = respXML->NewElement("D:propstat");
        XMLNode * oProp = respXML->NewElement("D:prop");

        XMLElement * oCreationDate = respXML->NewElement("D:creationdate");
        oCreationDate->SetText(formatTime(sb.st_ctime == 0?sb.st_mtime:sb.st_ctime).c_str());
        oProp->InsertEndChild(oCreationDate);

        XMLElement * oDisplayName = respXML->NewElement("D:displayname");
        oDisplayName->SetText(basename(path.c_str()));
        oProp->InsertEndChild(oDisplayName);
        
        // XMLElement * oDisplayName = respXML->NewElement("D:displayname");
        // oProp->InsertEndChild(oDisplayName->SetText(basename(path.c_str())));
        // //oProp->InsertEndChild(respXML->NewElement("D:displayname")->SetText(basename(path.c_str())));

        //XMLElement * oLanguage = respXML->NewElement("D:getcontentlanguage");
        //oProp->InsertEndChild(oLanguage);
        oProp->InsertEndChild(respXML->NewElement("D:getcontentlanguage"));

        XMLElement * oLength = respXML->NewElement("D:getcontentlength");
        oLength->SetText((int)sb.st_size);
        oProp->InsertEndChild(oLength);
        //oProp->InsertEndChild(respXML->NewElement("D:getcontentlength")->SetText((int)sb.st_size));

        XMLElement * oType = respXML->NewElement("D:getcontenttype");
        oType->SetText(get_content_type_from_file(basename(path.c_str())));
        oProp->InsertEndChild(oType);

        XMLElement * oEtag = respXML->NewElement("D:getetag");
        oEtag->SetText(formatTimeETag(sb.st_mtime).c_str());
        oProp->InsertEndChild(oEtag);

        XMLElement * oLastMod = respXML->NewElement("D:getlastmodified");
        oLastMod->SetText(formatTimeTxt(sb.st_mtime).c_str());
        oProp->InsertEndChild(oLastMod);

        XMLElement * oIscollecton = respXML->NewElement("D:iscollection");
        oIscollecton->SetText((sb.st_mode & S_IFMT) == S_IFDIR ? 1 : 0);
        oProp->InsertEndChild(oIscollecton);       

        XMLElement * oIsHidden = respXML->NewElement("D:ishidden");
        oIsHidden->SetText(0);
        oProp->InsertEndChild(oIsHidden); 

        XMLNode * oResourceType = respXML->NewElement("D:resourcetype");
        if ((sb.st_mode & S_IFMT) == S_IFDIR){
                XMLElement * oCollection = respXML->NewElement("D:collection");
                oResourceType->InsertEndChild(oCollection);
        }

        oProp->InsertEndChild(oResourceType);       

        oPropstat->InsertEndChild(oProp);
        oResponse->InsertEndChild(oPropstat);
        
        oRoot->InsertEndChild(oResponse);
        //ESP_LOGW(TAG, "xml recurse: %d path: %s uri:%S", recurse, path.c_str(), uriToPath(path).c_str()); 
        if ((sb.st_mode & S_IFMT) == S_IFDIR && recurse > 0 ){
                DIR *dir = opendir(uriToPath(path).c_str());
                //DIR *dir = opendir(uri.c_str());
                //DIR *dir = opendir(path.c_str());
                if (dir) {
                        struct dirent *de;

                        while ((de = readdir(dir))) {
                                if (strcmp(de->d_name, ".") == 0 ||
                                    strcmp(de->d_name, "..") == 0)
                                        continue;

                                std::string rpath = path + "/" + de->d_name;
                                xmlPropResponse(respXML, oRoot, path + "/" + de->d_name,  recurse -1 );
                                //sendPropResponse(resp, rpath, recurse-1);
                        }

                        closedir(dir);
                }
          //ESP_LOGI(TAG, "xml recurse %d root %s", recurse - 1, (path + "/" + basename(path.c_str())).c_str());      
          //xmlPropResponse(respXML, oRoot, path + "/" + basename(path.c_str()) , recurse - 1);     
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
        httpd_req * http_req = req.get_httpd_req();
        webdav::WebDav *server = (webdav::WebDav *)req.get_httpd_req()->user_ctx;
        std::string translate = req.getHeader("translate");
        std::string mod_since = req.getHeader("If-Modified-Since"); 
        struct stat sb;
        int ret = stat(uriToPath(req.getPath()).c_str(), &sb);
        if (ret < 0)
                return 404;

        if (mod_since != ""){
                const char* format = "%a, %d %b %Y %H:%M:%S %Z";
                time_t parsedtime = parseDateTime(mod_since.c_str(), format);
                if(sb.st_mtime >= parsedtime){
                        ESP_LOGI(TAG, "return 304");
                        return 304;
                }
        }        

        if (sb.st_size > 16 * 1024){
                if (server->queue_request(req.get_httpd_req(), chunk_file_send) == ESP_OK) {
                        return 0;
                } else {
                        httpd_resp_set_status(req.get_httpd_req(), "503 Busy");
                        httpd_resp_sendstr(req.get_httpd_req(), "<div> no workers available. server busy.</div>");
                        return 0;
                }               
        } else {

                FILE *f = fopen(uriToPath(req.getPath()).c_str(), "r");
                if (!f)
                        return 500;

                resp.setHeader("Content-Length", sb.st_size);
                resp.setHeader("ETag", sb.st_ino);
                resp.setHeader("Last-Modified", formatTime(sb.st_mtime));
                resp.setHeader("Content-Type", get_content_type_from_file(this->uriToPath(req.getPath()).c_str()));

                char *chunk = (char *) malloc(sb.st_size);

                size_t r = fread(chunk, 1, sb.st_size, f);
                httpd_resp_send(req.get_httpd_req(), chunk, r);
                //ret = resp.sendChunk(chunk, r);

                free(chunk);
                fclose(f);
                //resp.closeChunk();

                //if (ret == 0)
                       // return 200;


        }
        return 200;
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
        //char href[255] = "https://192.168.0.49";
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

        //using namespace tinyxml2;

        XMLDocument respXML;
        XMLElement * oRoot = respXML.NewElement("D:multistatus");
        oRoot->SetAttribute("xmlns:D", "DAV");
        xmlPropResponse(&respXML, oRoot, req.getPath(), recurse);
        respXML.InsertFirstChild(oRoot);
        respXML.InsertFirstChild(respXML.NewDeclaration());
        XMLPrinter printer;
        respXML.Accept( &printer );
        //ESP_LOGI(TAG, "Output:\n%s\n", printer.CStr());
        resp.sendChunk(printer.CStr());
        resp.closeChunk();

        // resp.sendChunk("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");
        // resp.sendChunk("<D:multistatus xmlns:D=\"DAV:\">\n");
        // sendPropResponse(resp, req.getPath(), recurse);
        // resp.sendChunk("</D:multistatus>\n");
        // resp.closeChunk();

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
                        ESP_LOGE(TAG, "Failed to Parse Proppatch request body");
                        resp.setStatus(500, "Failed to Parse Proppatch request body");      
                } else {
                        //ESP_LOGI(TAG, "Body parsed succesfully" );  
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
                //ESP_LOGI(TAG, "Output: %s", printer.CStr());
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