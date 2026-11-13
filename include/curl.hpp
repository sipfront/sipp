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

#ifndef __SIPP_CURL_HPP__
#define __SIPP_CURL_HPP__

/*
 * The <curl> action: an HTTP request a call makes on its way, to tell a
 * system outside of SIP what it is doing.
 *
 *   <curl method="POST" url="http://host/calls/[call_id]"
 *         content_type="application/sdp" data="[$sdp]"/>
 *
 * The call does not wait for it. The request is queued and made by one of a
 * few worker threads, so that a slow HTTP server never holds up the SIP main
 * loop; a request that fails (no connection, a timeout, a status of 400 or
 * more) is reported as a warning by the main loop. With more requests waiting
 * than the queue holds, new ones are dropped and counted.
 */

#include <string>

enum curl_method_t {
    CURL_METHOD_GET = 0,
    CURL_METHOD_POST,
    CURL_METHOD_PUT,
    CURL_METHOD_DELETE,
    CURL_METHOD_INVALID
};

/* "GET", "post", ... as a method; CURL_METHOD_INVALID for anything else. */
curl_method_t sipp_curl_method(const char *name);
const char *sipp_curl_method_name(curl_method_t method);

#ifdef USE_CURL

/* Queue a request. has_data says whether there is a body, which may be
 * empty; content_type may be empty for libcurl's default. Main thread. */
void sipp_curl(curl_method_t method, const std::string &url, bool has_data,
               const std::string &data, const std::string &content_type);

/* Report the requests that failed since the last call. Main loop. */
void sipp_curl_poll();

/* Give the requests still waiting a moment to be made, then stop the
 * workers. Safe to call when no request was ever queued. */
void sipp_curl_shutdown();

#endif /* USE_CURL */

#endif /* __SIPP_CURL_HPP__ */
