/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Author : Sipfront GmbH
 */

/* The <curl> action: see curl.hpp. */

#include <string.h>
#include <strings.h>

#include "curl.hpp"

curl_method_t sipp_curl_method(const char *name)
{
    if (!name || !strcasecmp(name, "GET")) {
        return CURL_METHOD_GET;
    } else if (!strcasecmp(name, "POST")) {
        return CURL_METHOD_POST;
    } else if (!strcasecmp(name, "PUT")) {
        return CURL_METHOD_PUT;
    } else if (!strcasecmp(name, "DELETE")) {
        return CURL_METHOD_DELETE;
    }
    return CURL_METHOD_INVALID;
}

const char *sipp_curl_method_name(curl_method_t method)
{
    switch (method) {
    case CURL_METHOD_GET:    return "GET";
    case CURL_METHOD_POST:   return "POST";
    case CURL_METHOD_PUT:    return "PUT";
    case CURL_METHOD_DELETE: return "DELETE";
    default:                 return "?";
    }
}

#ifdef USE_CURL

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <curl/curl.h>

#include "sipp.hpp"

struct curl_request {
    curl_method_t method;
    std::string url;
    bool has_data;
    std::string data;
    std::string content_type;
};

static const size_t CURL_WORKERS = 4;
/* Requests waiting for a worker; more than that are dropped. */
static const size_t CURL_QUEUE_MAX = 10000;
/* Failures waiting to be reported; more than that are only counted. */
static const size_t CURL_FAILURES_MAX = 100;
static const long CURL_CONNECT_TIMEOUT = 5;
static const long CURL_TIMEOUT = 10;
/* How long the workers get on exit for what is still queued. */
static const int CURL_SHUTDOWN_SECONDS = 3;

static std::mutex queue_mutex;
static std::condition_variable queue_wakeup;
static std::deque<curl_request> queue;
static bool stopping = false;
static size_t in_flight = 0;
static std::vector<std::thread> workers;

/* From the workers to the main loop. */
static std::mutex failure_mutex;
static std::deque<std::string> failures;
static unsigned long failures_unreported = 0;
static std::atomic<bool> failures_pending(false);
/* Main thread only. */
static unsigned long dropped = 0;
static unsigned long dropped_reported = 0;

static void report_failure(const std::string &text)
{
    std::lock_guard<std::mutex> lock(failure_mutex);
    if (failures.size() >= CURL_FAILURES_MAX) {
        failures_unreported++;
    } else {
        failures.push_back(text);
    }
    failures_pending = true;
}

/* The response is not wanted. */
static size_t discard(void *, size_t size, size_t nmemb, void *)
{
    return size * nmemb;
}

static void perform(CURL *easy, const curl_request &request)
{
    char error[CURL_ERROR_SIZE] = "";
    struct curl_slist *headers = nullptr;

    curl_easy_reset(easy);
    curl_easy_setopt(easy, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, CURL_TIMEOUT);
    /* http and https only, also after a redirect */
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

    switch (request.method) {
    case CURL_METHOD_POST:
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        break;
    case CURL_METHOD_PUT:
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT");
        break;
    case CURL_METHOD_DELETE:
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "DELETE");
        break;
    default:
        break;
    }
    if (request.has_data || request.method == CURL_METHOD_POST) {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request.data.length());
        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, request.data.c_str());
        if (request.method == CURL_METHOD_GET) {
            /* a body on a GET: keep the method the scenario asked for */
            curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "GET");
        }
    }
    if (!request.content_type.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + request.content_type).c_str());
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode rc = curl_easy_perform(easy);
    if (rc != CURLE_OK) {
        report_failure(std::string(sipp_curl_method_name(request.method)) + " " + request.url + ": " +
                       (error[0] ? error : curl_easy_strerror(rc)));
    } else {
        long status = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        if (status >= 400) {
            report_failure(std::string(sipp_curl_method_name(request.method)) + " " + request.url +
                           ": HTTP status " + std::to_string(status));
        }
    }
    if (headers) {
        curl_slist_free_all(headers);
    }
}

static void worker()
{
    /* One handle per worker: it keeps its connections between requests. */
    CURL *easy = curl_easy_init();

    for (;;) {
        curl_request request;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_wakeup.wait(lock, [] { return stopping || !queue.empty(); });
            if (queue.empty()) {
                break;
            }
            request = std::move(queue.front());
            queue.pop_front();
            in_flight++;
        }
        if (easy) {
            perform(easy, request);
        } else {
            report_failure(std::string(sipp_curl_method_name(request.method)) + " " + request.url +
                           ": could not set up libcurl");
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            in_flight--;
        }
        queue_wakeup.notify_all();
    }
    if (easy) {
        curl_easy_cleanup(easy);
    }
}

void sipp_curl(curl_method_t method, const std::string &url, bool has_data,
               const std::string &data, const std::string &content_type)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (workers.empty()) {
            /* The first request: before any thread uses libcurl. */
            curl_global_init(CURL_GLOBAL_DEFAULT);
            for (size_t i = 0; i < CURL_WORKERS; i++) {
                workers.emplace_back(worker);
            }
        }
        if (queue.size() >= CURL_QUEUE_MAX) {
            dropped++;
            return;
        }
        queue.push_back(curl_request{method, url, has_data, data, content_type});
    }
    queue_wakeup.notify_one();
}

void sipp_curl_poll()
{
    if (dropped != dropped_reported) {
        WARNING("curl: %lu requests dropped so far, more than %zu were waiting to be made",
                dropped, CURL_QUEUE_MAX);
        dropped_reported = dropped;
    }
    if (!failures_pending) {
        return;
    }
    std::deque<std::string> texts;
    unsigned long more;
    {
        std::lock_guard<std::mutex> lock(failure_mutex);
        texts.swap(failures);
        more = failures_unreported;
        failures_unreported = 0;
        failures_pending = false;
    }
    for (const std::string &text : texts) {
        WARNING("curl: %s", text.c_str());
    }
    if (more) {
        WARNING("curl: %lu more requests failed", more);
    }
}

void sipp_curl_shutdown()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (workers.empty()) {
            return;
        }
        /* What is queued and under way gets its chance. */
        queue_wakeup.wait_for(lock, std::chrono::seconds(CURL_SHUTDOWN_SECONDS),
                              [] { return queue.empty() && in_flight == 0; });
        queue.clear();
        stopping = true;
    }
    queue_wakeup.notify_all();
    /* A worker in a request ends with its timeout at the latest. */
    for (std::thread &thread : workers) {
        thread.join();
    }
    workers.clear();
    stopping = false;
    curl_global_cleanup();
    sipp_curl_poll();
}

#endif /* USE_CURL */

#ifdef GTEST
#include "gtest/gtest.h"

TEST(curl, Method) {
    EXPECT_EQ(CURL_METHOD_GET, sipp_curl_method(nullptr));
    EXPECT_EQ(CURL_METHOD_GET, sipp_curl_method("get"));
    EXPECT_EQ(CURL_METHOD_POST, sipp_curl_method("POST"));
    EXPECT_EQ(CURL_METHOD_PUT, sipp_curl_method("Put"));
    EXPECT_EQ(CURL_METHOD_DELETE, sipp_curl_method("delete"));
    EXPECT_EQ(CURL_METHOD_INVALID, sipp_curl_method("PATCH"));
    EXPECT_STREQ("DELETE", sipp_curl_method_name(CURL_METHOD_DELETE));
}
#endif /* GTEST */
