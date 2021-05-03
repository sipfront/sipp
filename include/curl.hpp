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
 *  Author : Richard GAYRAUD - 04 Nov 2003
 *           From Hewlett Packard Company.
 *           Charles P. Wright from IBM Research
 *           Andy Aicken
 */

#ifndef __CURL__
#define __CURL__

#include "message.hpp"

enum curl_method_t {
    CURL_GET = 0,
    CURL_POST,
    CURL_DELETE,
    CURL_PUT,
    CURL_PATCH
};

struct curl_actinfo_t {
    SendingMessage *url;
    SendingMessage *data;
    curl_method_t method;
};

void curl(curl_method_t method, char* url, char *data);

#endif
