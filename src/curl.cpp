#include "defines.h";
#include "curl.hpp";

static const char* curl_method2str(curl_method_t method) {
    switch (method) {
        case CURL_POST:
            return "POST";
            break;
        case CURL_GET:
            return "GET";
            break;
        case CURL_DELETE:
            return "DELETE";
            break;
        case CURL_PUT:
            return "PUT";
            break;
        case CURL_PATCH:
            return "PATCH";
            break;
        default:
            return "UNKNOWN";
            break;
    };
}

void curl(curl_method_t method, const char* url, const char *data) {
    WARNING("+++++ curl %s to %s with data '%s'", curl_method2str(method), url, data);
}
