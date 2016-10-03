#include "secril-shim.h"

/* A copy of the original RIL function table. */
static const RIL_RadioFunctions *origRilFunctions;

/* A copy of the ril environment passed to RIL_Init. */
static const struct RIL_Env *rilEnv;

/* The aries variant we're running on. */
static int ariesVariant = VARIANT_INIT;

static void onRequestShim(int request, void *data, size_t datalen, RIL_Token t)
{
	switch (request) {
		case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
			/* Ugly fix for Samsung messing up SMS_SEND request in binary RIL */
			request = RIL_REQUEST_SEND_SMS;
			break;
		case RIL_REQUEST_SETUP_DATA_CALL:
			if (data != NULL) {
				/* The RIL requires the type to be RADIO_TECH_GPRS (1), as v3 specs state 0 (CDMA) or 1 (GSM) */
				char **pStrings = (char **)data;

				pStrings[0][0] = '1';

				RLOGD("%s: got request %s: overriding Radio Technology\n", __func__, requestToString(request));
				origRilFunctions->onRequest(request, pStrings, datalen, t);
				return;
			}
		case RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING:
			/* We fake this and never even send it to the real RIL */
			RLOGW("Faking reply to RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING\n");
			rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
			return;
		case RIL_REQUEST_SIM_OPEN_CHANNEL:
			/* We fake this and never even send it to the real RIL */
			RLOGW("Faking reply to RIL_REQUEST_SIM_OPEN_CHANNEL\n");
			rilEnv->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
			return;
		/* Necessary; RILJ may fake this for us if we reply not supported, but we can just implement it. */
		case RIL_REQUEST_GET_RADIO_CAPABILITY:
			; /* lol C standard */
			int raf;
			if (ariesVariant == VARIANT_GALAXYS4G) {
				raf = RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_HSUPA | RAF_HSDPA | RAF_HSPA | RAF_HSPAP | RAF_UMTS;
			} else {
				raf = RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_HSUPA | RAF_HSDPA | RAF_HSPA | RAF_UMTS;
			}
			RIL_RadioCapability rc[1] =
			{
				{ /* rc[0] */
					RIL_RADIO_CAPABILITY_VERSION, /* version */
					0, /* session */
					RC_PHASE_CONFIGURED, /* phase */
					raf, /* rat */
					{ /* logicalModemUuid */
						0,
					},
					RC_STATUS_SUCCESS /* status */
				}
			};
			RLOGW("%s: got request %s: replied with our implementation!\n", __func__, requestToString(request));
			rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, rc, sizeof(rc));
			return;

		/* The following requests were introduced post 2.3 */
		case RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE:
		case RIL_REQUEST_ISIM_AUTHENTICATION:
		case RIL_REQUEST_ACKNOWLEDGE_INCOMING_GSM_SMS_WITH_PDU:
		case RIL_REQUEST_VOICE_RADIO_TECH:
		case RIL_REQUEST_GET_CELL_INFO_LIST:
		case RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE:
		case RIL_REQUEST_SET_INITIAL_ATTACH_APN:
		case RIL_REQUEST_IMS_REGISTRATION_STATE:
		case RIL_REQUEST_IMS_SEND_SMS:
		case RIL_REQUEST_SIM_TRANSMIT_APDU_BASIC:
		case RIL_REQUEST_SIM_CLOSE_CHANNEL:
		case RIL_REQUEST_SIM_TRANSMIT_APDU_CHANNEL:
		case RIL_REQUEST_NV_READ_ITEM:
		case RIL_REQUEST_NV_WRITE_ITEM:
		case RIL_REQUEST_NV_WRITE_CDMA_PRL:
		case RIL_REQUEST_NV_RESET_CONFIG:
		case RIL_REQUEST_SET_UICC_SUBSCRIPTION:
		case RIL_REQUEST_ALLOW_DATA:
		case RIL_REQUEST_GET_HARDWARE_CONFIG:
		case RIL_REQUEST_SIM_AUTHENTICATION:
		case RIL_REQUEST_GET_DC_RT_INFO:
		case RIL_REQUEST_SET_DC_RT_INFO_RATE:
		case RIL_REQUEST_SET_DATA_PROFILE:
		case RIL_REQUEST_SHUTDOWN: /* TODO: Is there something we can do for RIL_REQUEST_SHUTDOWN ? */
		case RIL_REQUEST_SET_RADIO_CAPABILITY:
		case RIL_REQUEST_START_LCE:
		case RIL_REQUEST_STOP_LCE:
		case RIL_REQUEST_PULL_LCEDATA:
		case RIL_REQUEST_GET_ACTIVITY_INFO:
			RLOGW("%s: got request %s: replied with REQUEST_NOT_SUPPPORTED.\n", __func__, requestToString(request));
			rilEnv->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
			return;
	}

	RLOGD("%s: got request %s: forwarded to RIL.\n", __func__, requestToString(request));
	origRilFunctions->onRequest(request, data, datalen, t);
}

static void fixupSignalStrength(void *response, size_t responselen) {
	if (responselen == sizeof(RIL_SignalStrength_v5)) {
		int gsmSignalStrength;

		RIL_SignalStrength_v5 *p_cur = ((RIL_SignalStrength_v5 *) response);

		gsmSignalStrength = p_cur->GW_SignalStrength.signalStrength & 0xFF;

		if (gsmSignalStrength < 0 ||
			(gsmSignalStrength > 31 && p_cur->GW_SignalStrength.signalStrength != 99)) {
			gsmSignalStrength = p_cur->CDMA_SignalStrength.dbm;
		}

		/* Fix GSM signal strength */
		p_cur->GW_SignalStrength.signalStrength = gsmSignalStrength;
		p_cur->GW_SignalStrength.bitErrorRate = -1;
	} else {
		RLOGE("%s: Not an SignalStrength_v5\n", __func__);
	}
}

static void onRequestCompleteShim(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	int request;
	RequestInfo *pRI;

	pRI = (RequestInfo *)t;

	/* If pRI is null, this entire function is useless. */
	if (pRI == NULL)
		goto null_token_exit;

	request = pRI->pCI->requestNumber;

	switch (request) {
		case RIL_REQUEST_GET_SIM_STATUS:
			/* Android 7.0 mishandles RIL_CardStatus_v5.
			 * We can just fake a v6 response instead. */
			if (responselen == sizeof(RIL_CardStatus_v5)) {
				RLOGI("%s: got request %s: Upgrading response.\n",
				      __func__, requestToString(request));

				RIL_CardStatus_v5 *v5response = ((RIL_CardStatus_v5 *) response);

				RIL_CardStatus_v6 *v6response = malloc(sizeof(RIL_CardStatus_v6));

				v6response->card_state = v5response->card_state;
				v6response->universal_pin_state = v5response->universal_pin_state;
				v6response->gsm_umts_subscription_app_index = v5response->gsm_umts_subscription_app_index;
				v6response->cdma_subscription_app_index = v5response->cdma_subscription_app_index;
				v6response->ims_subscription_app_index = -1;
				v6response->num_applications = v5response->num_applications;
				memcpy(v6response->applications, v5response->applications, sizeof(RIL_AppStatus) * 8);

				rilEnv->OnRequestComplete(t, e, v6response, sizeof(RIL_CardStatus_v6));

				free(v6response);
				return;
			}
			/* If this was already a v6 reply, continue as usual. */
			break;
		case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
			/* Remove extra element (ignored on pre-M, now crashing the framework) */
			if (responselen > sizeof(int)) {
				rilEnv->OnRequestComplete(t, e, response, sizeof(int));
				return;
			}
			break;
		case RIL_REQUEST_SIGNAL_STRENGTH:
			/* The Samsung RIL reports the signal strength in a strange way... */
			if (response != NULL && responselen >= sizeof(RIL_SignalStrength_v5)) {
				fixupSignalStrength(response, responselen);
				rilEnv->OnRequestComplete(t, e, response, responselen);
				return;
			}
			break;
		case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
			if (response!= NULL) {
				int *p_int = (int *) response;
				if (p_int[0] == PREF_NET_TYPE_GSM_WCDMA_CDMA_EVDO_AUTO) {
					RLOGD("%s: NETWORK_MODE_GLOBAL => NETWORK_MODE_WCDMA_PREF\n", __func__);
					p_int[0] = PREF_NET_TYPE_GSM_WCDMA;
					rilEnv->OnRequestComplete(t, e, p_int, responselen);
					return;
				}
			}
			break;
	}

	RLOGD("%s: got request %s: forwarded to libril.\n", __func__, requestToString(request));
null_token_exit:
	rilEnv->OnRequestComplete(t, e, response, responselen);
}

static void onUnsolicitedResponseShim(int unsolResponse, const void *data, size_t datalen)
{
	switch (unsolResponse) {
		case RIL_UNSOL_NITZ_TIME_RECEIVED:
		{
			/**
			 * Remove the tailing information that samsung added to the string
			 * data is const char * in form "yy/mm/dd,hh:mm:ss(+/-)tz,dt"
			 * but Samsung adds ",mncmcc" to it
			 */
			char *nitz = (char*)data;

			RLOGD("%s: got unsol response %s: contents is: %s\n", __func__, requestToString(unsolResponse), nitz);

			if (strlen(nitz) > 23 && nitz[23] == ',') {
				nitz[23] = '\0';
			}

			/* Although libril doesn't currently use datalen for NITZ replies, calculate it anyways */
			rilEnv->OnUnsolicitedResponse(unsolResponse, nitz, sizeof(char)*strlen(nitz));
			return;
		}
		case RIL_UNSOL_SIGNAL_STRENGTH:
			/* The Samsung RIL reports the signal strength in a strange way... */
			if (data != NULL && datalen >= sizeof(RIL_SignalStrength_v5))
				fixupSignalStrength((void*) data, datalen);
			break;

		default:
		if (unsolResponse > 11000) {
			/* Samsung-specific call, log only */
			RLOGD("%s: got samsung unsol response %d: logging only\n", __func__, unsolResponse);
			return;
		}
	}

	RLOGD("%s: got unsol response %s: forwarded to libril.\n", __func__, requestToString(unsolResponse));
	rilEnv->OnUnsolicitedResponse(unsolResponse, data, datalen);
}

const RIL_RadioFunctions* RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
	RIL_RadioFunctions const* (*origRilInit)(const struct RIL_Env *env, int argc, char **argv);
	static RIL_RadioFunctions shimmedFunctions;
	static struct RIL_Env shimmedEnv;
	void *origRil;
	int i;
	char propBuf[PROPERTY_VALUE_MAX];

	if (CC_LIKELY(ariesVariant == VARIANT_INIT)) {
		property_get("ro.product.device", propBuf, "unknown");
		if (!strcmp(propBuf, "galaxys4gmtd")) {
			ariesVariant = VARIANT_GALAXYS4G;
		} else {
			ariesVariant = VARIANT_ARIES;
		}
		RLOGD("%s: got aries variant: %i", __func__, ariesVariant);
}

	/* Shim the RIL_Env passed to the real RIL, saving a copy of the original */
	rilEnv = env;
	shimmedEnv = *env;
	shimmedEnv.OnRequestComplete = onRequestCompleteShim;
	shimmedEnv.OnUnsolicitedResponse = onUnsolicitedResponseShim;

	/* Open and Init the original RIL. */

	origRil = dlopen(RIL_LIB_PATH, RTLD_LOCAL);
	if (CC_UNLIKELY(!origRil)) {
		RLOGE("%s: failed to load '" RIL_LIB_PATH  "': %s\n", __func__, dlerror());
		return NULL;
	}

	/* Remove "-c" command line as Samsung's RIL does not understand it - it just barfs instead */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i != argc -1) {
			/* Found it */
			memcpy(argv + i, argv + i + 2, sizeof(char*[argc - i - 2]));
			argc -= 2;
		}
	}

	origRilInit = dlsym(origRil, "RIL_Init");
	if (CC_UNLIKELY(!origRilInit)) {
		RLOGE("%s: couldn't find original RIL_Init!\n", __func__);
		goto fail_after_dlopen;
	}

	origRilFunctions = origRilInit(&shimmedEnv, argc, argv);
	if (CC_UNLIKELY(!origRilFunctions)) {
		RLOGE("%s: the original RIL_Init derped.\n", __func__);
		goto fail_after_dlopen;
	}
	RLOGD("%s: succesfully ran the original RIL_init", __func__);

	/* Shim functions as needed. */
	shimmedFunctions = *origRilFunctions;
	shimmedFunctions.onRequest = onRequestShim;
	shimmedFunctions.version = 4;

	return &shimmedFunctions;

fail_after_dlopen:
	dlclose(origRil);
	return NULL;
}
