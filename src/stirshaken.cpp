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

#ifdef STIRSHAKEN

#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C"{
#endif

#include <stir_shaken.h>

#ifdef __cplusplus
}
#endif

#include "defines.h"
#include "sip_parser.hpp"
#include "stirshaken.hpp"

static stir_shaken_as_t *as = NULL;
static stir_shaken_vs_t *vs = NULL;

// TODO: make configurable
static const char* stirshaken_as_default_key = "/home/admin/helpers/certs/sp/priv.pem";
static int stirshaken_vs_connect_timeout = 3;
static int stirshaken_vs_cache_certificates = 1;
static const char* stirshaken_vs_cache_dir = "/home/admin/helpers/certs/cache";
static int stirshaken_vs_verify_x509_cert_path = 0;
static const char* stirshaken_vs_ca_dir = "/home/admin/helpers/certs/ca";
static const char* stirshaken_vs_crl_dir = "/home/admin/helpers/certs/crl";
static size_t stirshaken_vs_cache_expire = 120;
static int stirshaken_vs_identity_expire = 60;

static unsigned long hash_to_long(const char *str)
{
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c;
	return hash;
}

static void hash_to_string(const char *url, char *buf, int buf_len)
{
	unsigned long hash_long = hash_to_long(url);
	snprintf(buf, buf_len, "%lu.pem", hash_long);
}

static int get_cert_name_hashed(const char *name, char *buf, int buf_len)
{
	char cert_hash[64] = { 0 };
	hash_to_string(name, cert_hash, sizeof(cert_hash));
	if (!stir_shaken_make_complete_path(buf, buf_len, stirshaken_vs_cache_dir, cert_hash, "/")) {
		ERROR("Failed to create stirshaken cert name hashed\n");
	}
	return 0;
}

static stir_shaken_status_t vs_callback(stir_shaken_callback_arg_t *arg) {
    stir_shaken_context_t ss = { 0 };
    stir_shaken_cert_t cache_copy = { 0 };
    char cert_full_path[STIR_SHAKEN_BUFLEN] = { 0 };
    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    WARNING("+++ vs_callback\n");

    switch (arg->action) {
        case STIR_SHAKEN_CALLBACK_ACTION_CERT_FETCH_ENQUIRY:
            WARNING("+++ cert fetch enquiry\n");
            if (!stirshaken_vs_cache_certificates) {
                WARNING("+++ cert caching disabled, returning\n");
                return STIR_SHAKEN_STATUS_NOT_HANDLED;
            }
            WARNING("+++ get cert name hashed\n");
            if (get_cert_name_hashed(arg->cert.public_url, cert_full_path, STIR_SHAKEN_BUFLEN) == -1) {
                ERROR("Failed to get stirshaken cert name hashed");
            }
            WARNING("+++ check if cert hash %s exists\n", cert_full_path);
            if (stir_shaken_file_exists(cert_full_path) == STIR_SHAKEN_STATUS_OK) {
                WARNING("+++ cert exists, check expiry\n");
                if (stirshaken_vs_cache_expire) {
                    struct stat attr = { 0 };
                    time_t now_s = time(NULL), diff = 0;

                    if (stat(cert_full_path, &attr) == -1) {
                        ERROR("Failed to get modification timestamp of stirshaken cache dir");
                    }

                    if (now_s < attr.st_mtime) {
                        ERROR("Modification timestamp of stirshaken cache dir is invalid");
                    }

                    diff = now_s - attr.st_mtime;
                    if (diff > stirshaken_vs_cache_expire) {
                        return STIR_SHAKEN_STATUS_NOT_HANDLED;
                    }
                }

                if (!(cache_copy.x = stir_shaken_load_x509_from_file(&ss, cert_full_path))) {
                    ERROR("Failed to load stirshaken X509 certificate from file '%s': %s", stir_shaken_get_error(&ss, &error_code));
                }

                if (stir_shaken_cert_copy(&ss, &arg->cert, &cache_copy) != STIR_SHAKEN_STATUS_OK) {
                    stir_shaken_cert_deinit(&cache_copy);
                    ERROR("Failed to copy stirshaken certificate '%s': %s", stir_shaken_get_error(&ss, &error_code));
                }

                stir_shaken_cert_deinit(&cache_copy);
                return STIR_SHAKEN_STATUS_HANDLED;
            }

        default:
            WARNING("+++ certificate not found in cache\n");
            return STIR_SHAKEN_STATUS_NOT_HANDLED;
    }
}

static int stirshaken_handle_cache(stir_shaken_context_t *ss, stir_shaken_passport_t *passport, stir_shaken_cert_t *cert) {

    if (!passport || !cert) {
        ERROR("stirshaken_handle_cache with NULL passport or cache called");
    }

    if (!stirshaken_vs_cache_dir) {
        ERROR("No stirshaken vs cache dir set");
    }

    if (!ss->cert_fetched_from_cache) {
        char cert_full_path[STIR_SHAKEN_BUFLEN] = { 0 };
        const char *x5u = stir_shaken_passport_get_header(ss, passport, "x5u");

        if (stir_shaken_zstr(x5u)) {
            WARNING("PASSporT has no x5u\n");
            return -1;
        }

        if (get_cert_name_hashed(x5u, cert_full_path, STIR_SHAKEN_BUFLEN) == -1) {
            ERROR("Failed to get stirshaken cert name hashed");
        }

        if (stir_shaken_file_exists(cert_full_path) == STIR_SHAKEN_STATUS_OK) {
            if (stir_shaken_file_remove(cert_full_path) != STIR_SHAKEN_STATUS_OK) {
                ERROR("Failed to remove expired cached stirshaken cert %s", cert_full_path);
            }
        }

        if (stir_shaken_x509_to_disk(ss, cert->x, cert_full_path) != STIR_SHAKEN_STATUS_OK) {
            ERROR("Failed to write stirshaken cert %s to cache file %s", x5u, cert_full_path);
        }
    }

    return 0;
}

static void stirshaken_init() {

    if (as || vs) {
        return;
    }

    stir_shaken_context_t ss = { 0 };
    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    if (stir_shaken_init(&ss, STIR_SHAKEN_LOGLEVEL_NOTHING) != STIR_SHAKEN_STATUS_OK) {
        ERROR("Failed to initialize stirshaken library");
    }

    as = stir_shaken_as_create(&ss);
    if (!as) {
        ERROR("Failed to create stirshaken authentication service: %s", stir_shaken_get_error(&ss, &error_code));
    }

    if (stirshaken_as_default_key) {
        if (stir_shaken_as_load_private_key(&ss, as, stirshaken_as_default_key) != STIR_SHAKEN_STATUS_OK) {
            ERROR("Failed to load default authentication key '%s': %s", stirshaken_as_default_key, stir_shaken_get_error(&ss, &error_code));
        }
    }

    vs = stir_shaken_vs_create(&ss);
    if (!vs) {
        ERROR("Failed to create stirshaken verification service: %s", stir_shaken_get_error(&ss, &error_code));
    }

    stir_shaken_vs_set_connect_timeout(&ss, vs, stirshaken_vs_connect_timeout);
    stir_shaken_vs_set_callback(&ss, vs, vs_callback);

    if (stirshaken_vs_cache_certificates) {
        if (stir_shaken_dir_exists(stirshaken_vs_cache_dir) != STIR_SHAKEN_STATUS_OK) {
            ERROR("The stirshaken cache directory '%s' does not exist", stirshaken_vs_cache_dir);
        }
    }

    if (stirshaken_vs_verify_x509_cert_path) {
        stir_shaken_vs_set_x509_cert_path_check(&ss, vs, 1);
        if (stir_shaken_vs_load_ca_dir(&ss, vs, stirshaken_vs_ca_dir) != STIR_SHAKEN_STATUS_OK) {
            ERROR("Failed to init stirshaken CA X509 cert store '%s': %s", 
                    stirshaken_vs_ca_dir, stir_shaken_get_error(&ss, &error_code));
        }

        if (stirshaken_vs_crl_dir) {
            if (stir_shaken_vs_load_crl_dir(&ss, vs, stirshaken_vs_crl_dir) != STIR_SHAKEN_STATUS_OK) {
                ERROR("Failed to init stirshaken CA X509 cert store CRL dir '%s': %s", 
                        stirshaken_vs_crl_dir, stir_shaken_get_error(&ss, &error_code));
            }
        }
    }
}

char* stirshaken_create_identity_header(char *x5u, char *attest, char* origtn, char* desttn, char* origid, char* keypath, char *iat) {
    stir_shaken_context_t ss = { 0 };
    stir_shaken_passport_params_t params = {
        .x5u = x5u,
        .attest = attest,
        .desttn_key = "tn",
        .desttn_val = desttn,
        .iat = iat ? atoi(iat) : (uint32_t)time(NULL),
        .origtn_key = "tn",
        .origtn_val = origtn,
        .origid = origid
    };
    stir_shaken_passport_t *passport = NULL;
    char *sih = NULL;
    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    char uuid_str[37] = { 0 };
    char *identity_hdr;
    size_t identity_hdr_len;

    if (params.origid == NULL) {
        uuid_t uuid;
        uuid_generate(uuid);
        uuid_unparse_lower(uuid, uuid_str);
        params.origid = uuid_str;
    }

    stirshaken_init();

    if (keypath) {
        unsigned char key[STIR_SHAKEN_PRIV_KEY_RAW_BUF_LEN];
        unsigned int key_len = STIR_SHAKEN_PRIV_KEY_RAW_BUF_LEN;

        if (stir_shaken_load_key_raw(&ss, keypath, key, &key_len) != STIR_SHAKEN_STATUS_OK) {
            ERROR("Failed to load private stirshaken key: %s", stir_shaken_get_error(&ss, &error_code));
        }

        sih = stir_shaken_authenticate_to_sih_with_key(&ss, &params, &passport, key, key_len);
        if (!sih) {
            ERROR("Failed to create SIP Identity header with key '%s': %s", keypath, stir_shaken_get_error(&ss, &error_code));
        }
    } else {
        sih = stir_shaken_as_authenticate_to_sih(&ss, as, &params, &passport);
        if (!sih) {
            ERROR("Failed to create SIP Identity header with default key: %s", stir_shaken_get_error(&ss, &error_code));
        }
    }
    stir_shaken_passport_destroy(&passport);
   
    identity_hdr_len = strlen(sih) + strlen("Identity: ") + 1;
    identity_hdr = (char*)malloc(identity_hdr_len);
    if (!identity_hdr) {
        free(sih);
        ERROR("Failed to allocate memory for Identity header");
    }

    snprintf(identity_hdr, identity_hdr_len, "Identity: %s", sih);
    free(sih);

    return identity_hdr;
}

// TODO: this only checks if the identity header was signed by the corresponding key of the key given here
stirshaken_res_t stirshaken_validate_identity_header_with_key(char *identity, char* keypath) {

    stirshaken_res_t res;
    stir_shaken_context_t ss = { 0 };
    stir_shaken_passport_t *passport_out = NULL;

    unsigned char key[STIR_SHAKEN_PUB_KEY_RAW_BUF_LEN] = { 0 };
    uint32_t key_len = STIR_SHAKEN_PUB_KEY_RAW_BUF_LEN;

    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    stirshaken_init();

    if (!identity) {
        return STIRSHAKEN_RES_FAILED;
    }

    if (keypath) {
        if (stir_shaken_load_key_raw(&ss, keypath, key, &key_len) != STIR_SHAKEN_STATUS_OK) {
            ERROR("Failed to load stirshaken key '%s': %s", keypath, stir_shaken_get_error(&ss, &error_code));
        }
    }

    if (stir_shaken_sih_verify_with_key(&ss, identity, key, key_len, &passport_out) != STIR_SHAKEN_STATUS_OK) {
        WARNING("Identity header did not pass verification against key\n");
        res = STIRSHAKEN_RES_FAILED;
    } else {
        res = STIRSHAKEN_RES_OK;
    }

    stir_shaken_passport_destroy(&passport_out);
    return res;
}

// TODO: this only checks if the identity header was signed by the corresponding key of the cert given here
stirshaken_res_t stirshaken_validate_identity_header_with_cert(char *identity, char* certpath) {

    stirshaken_res_t res;
    stir_shaken_context_t ss = { 0 };
    stir_shaken_passport_t *passport_out = NULL;
    stir_shaken_cert_t cert = { 0 };

    unsigned char key[STIR_SHAKEN_PUB_KEY_RAW_BUF_LEN] = { 0 };
    uint32_t key_len = STIR_SHAKEN_PUB_KEY_RAW_BUF_LEN;

    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    stirshaken_init();

    if (!identity) {
        return STIRSHAKEN_RES_FAILED;
    }

    if (!certpath) {
        ERROR("Failed to validate stirshaken with cert, no certpath given");
    }

	if (!(cert.x = stir_shaken_load_x509_from_file(&ss, certpath))) {
        ERROR("Failed to load stirshaken cert '%s': %s", certpath, stir_shaken_get_error(&ss, &error_code));
	}

    if (stir_shaken_sih_verify_with_cert(&ss, identity, &cert, &passport_out) != STIR_SHAKEN_STATUS_OK) {
        WARNING("Identity header did not pass verification against cert\n");
        res = STIRSHAKEN_RES_FAILED;
		goto out;
    }

    if (stir_shaken_passport_validate_iat_against_freshness(&ss, passport_out, stirshaken_vs_identity_expire) != STIR_SHAKEN_STATUS_OK) {
        stir_shaken_get_error(&ss, &error_code);
        if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT_VALUE_FUTURE) {
            WARNING("PASSporT not valid yet\n");
        } else if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT_VALUE_EXPIRED) {
            WARNING("PASSporT expired\n");
        } else if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT) {
            WARNING("PASSporT is missing iat grant\n");
        }

        WARNING("PASSporT of Identity header did not pass time check\n");
        res = STIRSHAKEN_RES_FAILED;
        goto out;
    }

    WARNING("Identity check ok (%s)\n", ss.x509_cert_path_checked ? "with X509 cert path check" : "without X509 cert path check");
    res = STIRSHAKEN_RES_OK;

out:
    stir_shaken_passport_destroy(&passport_out);
    return res;
}

stirshaken_res_t stirshaken_validate_identity_header(char *identity) {
    stirshaken_res_t res;
    stir_shaken_context_t ss = { 0 };
    stir_shaken_passport_t *passport_out = NULL;
    stir_shaken_cert_t *cert_out = NULL;

    stir_shaken_error_t	error_code = STIR_SHAKEN_ERROR_GENERAL;

    WARNING("++++ 1 checking identity\n");

    stirshaken_init();

    WARNING("++++ 2 checking identity\n");
    
    if (!identity) {
        return STIRSHAKEN_RES_FAILED;
    }

    WARNING("++++ 3 checking identity %s\n", identity);

    if (stir_shaken_vs_sih_verify(&ss, vs, identity, &cert_out, &passport_out) != STIR_SHAKEN_STATUS_OK) {
        WARNING("Identity header did not pass verification\n");
        res = STIRSHAKEN_RES_FAILED;
		goto out;
    }

    if (stirshaken_vs_cache_certificates) {
        stirshaken_handle_cache(&ss, passport_out, cert_out);
    }

    if (stir_shaken_passport_validate_iat_against_freshness(&ss, passport_out, stirshaken_vs_identity_expire) != STIR_SHAKEN_STATUS_OK) {
        stir_shaken_get_error(&ss, &error_code);
        if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT_VALUE_FUTURE) {
            WARNING("PASSporT not valid yet\n");
        } else if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT_VALUE_EXPIRED) {
            WARNING("PASSporT expired\n");
        } else if (error_code == STIR_SHAKEN_ERROR_PASSPORT_INVALID_IAT) {
            WARNING("PASSporT is missing iat grant\n");
        }

        WARNING("PASSporT of Identity header did not pass time check\n");
        res = STIRSHAKEN_RES_FAILED;
        goto out;
    }

    if (stirshaken_vs_verify_x509_cert_path) {
        if (stir_shaken_verify_cert_path(&ss, cert_out, vs->store) != STIR_SHAKEN_STATUS_OK) {
            WARNING("The cert did not pass X509 path validation: %s", stir_shaken_get_error(&ss, &error_code));
            res = STIRSHAKEN_RES_FAILED;
            goto out;

        }
    }

    WARNING("Identity check ok (%s)\n", ss.x509_cert_path_checked ? "with X509 cert path check" : "without X509 cert path check");
    res = STIRSHAKEN_RES_OK;

out:
    stir_shaken_passport_destroy(&passport_out);
    stir_shaken_cert_destroy(&cert_out);
    return res;
}


#endif // STIRSHAKEN
