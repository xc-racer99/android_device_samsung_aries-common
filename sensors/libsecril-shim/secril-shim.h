#ifndef __SECRIL_SHIM_H__
#define __SECRIL_SHIM_H__

#define LOG_TAG "secril-shim"
#define RIL_SHLIB

#define RIL_UNSOL_AM 11010
#define RIL_UNSOL_HSDPA_STATE_CHANGED 11016

#include <dlfcn.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/compiler.h>
#include <cutils/properties.h>
#include <sys/cdefs.h>
#include <telephony/ril.h>
#include <utils/Log.h>

#define RIL_LIB_PATH "/system/lib/libsec-ril.so"

enum variant_type {
	VARIANT_INIT,
	VARIANT_ARIES,
	VARIANT_GALAXYS4G
};

extern const char * requestToString(int request);

/* TODO: Do we really need to redefine these? They aren't in a header... */
typedef struct {
    int requestNumber;
    void (*dispatchFunction) (void *p, void *pRI);
    int(*responseFunction) (void *p, void *response, size_t responselen);
} CommandInfo;

typedef struct RequestInfo {
    int32_t token;      //this is not RIL_Token
    CommandInfo *pCI;
    struct RequestInfo *p_next;
    char cancelled;
    char local;         // responses to local commands do not go back to command process
} RequestInfo;

#endif /* __SECRIL_SHIM_H__ */

