#include <curl/curl.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include <unordered_set>

#include "config.h"
#include "sipp.hpp"
#include "socket.hpp"
#include "curl.hpp"

static unordered_set <int> mycurl_fds;

typedef struct _GlobalInfo {
    int tfd;
    CURLM *multi;
    int still_running;
    FILE *input;
} GlobalInfo;

typedef struct _ConnInfo {
    CURL *easy;
    char *url;
    char *data;
    GlobalInfo *global;
    char error[CURL_ERROR_SIZE];
} ConnInfo;

typedef struct _SockInfo {
    curl_socket_t sockfd;
    CURL *easy;
    int action;
    long timeout;
    GlobalInfo *global;
} SockInfo;

#define mycase(code) \
  case code: s = __STRING(code)

static GlobalInfo g = {
    .tfd = -1,
    .multi = NULL,
};
static struct itimerspec its;
 
static void mcode_or_die(const char *where, CURLMcode code)
{
  if(CURLM_OK != code) {
    const char *s;
    switch(code) {
      mycase(CURLM_BAD_HANDLE); break;
      mycase(CURLM_BAD_EASY_HANDLE); break;
      mycase(CURLM_OUT_OF_MEMORY); break;
      mycase(CURLM_INTERNAL_ERROR); break;
      mycase(CURLM_UNKNOWN_OPTION); break;
      mycase(CURLM_LAST); break;
      default: s = "CURLM_unknown"; break;
      mycase(CURLM_BAD_SOCKET);
      WARNING("CURL ERROR: %s returns %s\n", where, s);
      /* ignore this error */
      return;
    }
    ERROR("CURL ERROR: %s returns %s\n", where, s);
  }
}

static void timer_cb(GlobalInfo* g, int revents);

static const char* mycurl_method2str(curl_method_t method) {
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
        default:
            return "UNKNOWN";
    };
}

static void check_multi_info(GlobalInfo *g) {
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

  while((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
    
      // TODO: agranig: should we store the result somewhere, like in a var?
      // e.g. code, body, ...
      WARNING("DONE: %s => (%d) %s\n", eff_url, res, conn->error);
      
      curl_multi_remove_handle(g->multi, easy);
      free(conn->url);
      if (conn->data) {
          free(conn->data);
      }
      curl_easy_cleanup(easy);
      free(conn);
    }
  }
}

static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g) {
  struct itimerspec its;

  if(timeout_ms > 0) {
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = timeout_ms / 1000;
    its.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
  }
  else if(timeout_ms == 0) {
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 1;
  }
  else {
    memset(&its, 0, sizeof(struct itimerspec));
  }

  timerfd_settime(g->tfd, /*flags=*/0, &its, NULL);
  return 0;
}

static void timer_cb(GlobalInfo* g, int revents) {
  CURLMcode rc;
  uint64_t count = 0;
  ssize_t err = 0;

  err = read(g->tfd, &count, sizeof(uint64_t));
  if(err == -1) {
    if(errno == EAGAIN) {
      WARNING("EAGAIN on curl tfd %d\n", g->tfd);
      return;
    }
  }
  if(err != sizeof(uint64_t)) {
    WARNING_NO("curl read(tfd) == %ld", err);
  }

  rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0, &g->still_running);
  mcode_or_die("timer_cb: curl_multi_socket_action", rc);
  check_multi_info(g);
}

static void event_cb(GlobalInfo *g, int fd, int revents) {
  CURLMcode rc;
  struct itimerspec its;

  int action = ((revents & EPOLLIN) ? CURL_CSELECT_IN : 0) |
               ((revents & EPOLLOUT) ? CURL_CSELECT_OUT : 0);

  rc = curl_multi_socket_action(g->multi, fd, action, &g->still_running);
  mcode_or_die("event_cb: curl_multi_socket_action", rc);

  check_multi_info(g);
  if(g->still_running <= 0) {
    memset(&its, 0, sizeof(struct itimerspec));
    timerfd_settime(g->tfd, 0, &its, NULL);
  }
}

static void setsock(SockInfo *f, curl_socket_t s, CURL *e, int act, GlobalInfo *g) {
  struct epoll_event ev;
  int kind = ((act & CURL_POLL_IN) ? EPOLLIN : 0) |
             ((act & CURL_POLL_OUT) ? EPOLLOUT : 0);

  if(f->sockfd) {
    if(epoll_ctl(epollfd, EPOLL_CTL_DEL, f->sockfd, NULL))
      ERROR("EPOLL_CTL_DEL failed for fd: %d : %s\n",
              f->sockfd, strerror(errno));
  }

  f->sockfd = s;
  f->action = act;
  f->easy = e;

  ev.events = kind;
  ev.data.fd = s;
  if(epoll_ctl(epollfd, EPOLL_CTL_ADD, s, &ev))
    ERROR_NO("EPOLL_CTL_ADD failed for fd: %d", s);
}

static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g) {
  SockInfo *fdp = (SockInfo*)calloc(sizeof(SockInfo), 1);

  fdp->global = g;
  setsock(fdp, s, easy, action, g);
  curl_multi_assign(g->multi, s, fdp);
  mycurl_fds.insert(s);
}


static void remsock(SockInfo *f, GlobalInfo* g)
{
  if(f) {
    if(f->sockfd) {
      if(epoll_ctl(epollfd, EPOLL_CTL_DEL, f->sockfd, NULL)) {
        // TODO: agranig: on negative reply, this fails (sometimes?), so don't make it fatal
        //ERROR_NO("EPOLL_CTL_DEL failed for fd %d\n", f->sockfd);
        
      } else {
        mycurl_fds.erase(f->sockfd);
      }
    }
    free(f);
  }
}

static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp) {
  const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE" };

  GlobalInfo *g = (GlobalInfo*) cbp;
  SockInfo *fdp = (SockInfo*) sockp;

  if(what == CURL_POLL_REMOVE) {
    remsock(fdp, g);
  }
  else {
    if(!fdp) {
      addsock(s, e, what, g);
    }
    else {
      setsock(fdp, s, e, what, g);
    }
  }
  return 0;
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data) {
  (void)ptr;
  (void)data;
  return size * nmemb;
}

static int prog_cb(void *p, double dltotal, double dlnow, double ult, double uln) {
    ConnInfo *conn = (ConnInfo *)p;
    (void)ult;
    (void)uln;

    return 0;
}

static void mycurl_init() {


    memset(&g, 0, sizeof(GlobalInfo));
    g.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g.tfd == -1) {
        ERROR_NO("Failed to create curl gobal timer fd");
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(struct itimerspec));
    its.it_interval.tv_sec = 0;
    its.it_value.tv_sec = 1;
    timerfd_settime(g.tfd, 0, &its, NULL);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = g.tfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, g.tfd, &ev);

    g.multi = curl_multi_init();
    if (!g.multi) {
        ERROR("Failed to initialize curl multi handle\n");
    }

    curl_multi_setopt(g.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
    curl_multi_setopt(g.multi, CURLMOPT_SOCKETDATA, &g);
    curl_multi_setopt(g.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    curl_multi_setopt(g.multi, CURLMOPT_TIMERDATA, &g);
}

static void set_ctype_hdr(CURL *easy, const char* ctype) {
    struct curl_slist *hs = NULL;
    char buf[1024];

    snprintf(buf, sizeof(buf), "Content-Type: %s", ctype);
    hs = curl_slist_append(hs, buf);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, hs);
}


void sipp_curl(curl_method_t method, char* url, char *data, char* content_type) {
    if (!g.multi) {
        mycurl_init();
    }

    ConnInfo *conn;
    CURLMcode rc;

    conn = (ConnInfo*)calloc(1, sizeof(ConnInfo));
    conn->error[0]='\0';

    conn->easy = curl_easy_init();
    if(!conn->easy) {
        ERROR("Failed to create curl easy connection handle\n");
    }
    conn->global = &g;
    conn->url = strdup(url);
    if (data) {
        conn->data = strdup(data);
    } else {
        conn->data = NULL;
    }

    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    curl_easy_setopt(conn->easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    switch(method) {
        case CURL_GET:
            break;
        case CURL_DELETE:
            curl_easy_setopt(conn->easy, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        case CURL_PUT:
            curl_easy_setopt(conn->easy, CURLOPT_CUSTOMREQUEST, "PUT");
            if (conn->data) {
                curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDS, conn->data);
                if (content_type) {
                    set_ctype_hdr(conn->easy, content_type);
                }
            }
            break;
        case CURL_POST:
            curl_easy_setopt(conn->easy, CURLOPT_POST, 1L);
            if (conn->data) {
                curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDS, conn->data);
                if (content_type) {
                    set_ctype_hdr(conn->easy, content_type);
                }
            }
            break;
        default:
            ERROR("Invalid curl method\n");
    }
    rc = curl_multi_add_handle(g.multi, conn->easy);
    mcode_or_die("new_conn: curl_multi_add_handle", rc);
}

// TODO: invoke on exit
void sipp_curl_cleanup() {
    curl_multi_cleanup(g.multi);
}

int sipp_curl_fd(int fd) {
    if (!g.multi)
        return 0;

    if (fd == g.tfd || mycurl_fds.find(fd) != mycurl_fds.end()) {
        return 1;
    }
    return 0;
}

void sipp_curl_handle_fd(int fd, int revents) {
    if (!g.multi)
        return;
    if (fd == g.tfd) {
        timer_cb(&g, revents);
    } else {
        event_cb(&g, fd, revents);
    }
}
