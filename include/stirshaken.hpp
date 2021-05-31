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

#ifndef __STIRSHAKEN__
#define __STIRSHAKEN__

#include "message.hpp"

struct stirshaken_actinfo_t
{
    char keypath[256 + 1];
};

typedef enum {
    STIRSHAKEN_RES_FAILED = 0,
    STIRSHAKEN_RES_OK
} stirshaken_res_t;

char* stirshaken_create_identity_header(char *x5u, char *attest, char* origtn, char* desttn, char* origid, char* keypath, char* iat);

stirshaken_res_t stirshaken_validate_identity_header_with_key(char *identity, char* keypath);
stirshaken_res_t stirshaken_validate_identity_header_with_cert(char *identity, char* certpath);
stirshaken_res_t stirshaken_validate_identity_header(char *identity);

#endif
