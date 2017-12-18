#include <netdb.h>

#include "secril-shim.h"

/* A copy of the original RIL function table. */
static const RIL_RadioFunctions *origRilFunctions;

/* A copy of the ril environment passed to RIL_Init. */
static const struct RIL_Env *rilEnv;

/* The aries variant we're running on. */
static int ariesVariant = VARIANT_INIT;

static void patchMem(void *libHandle)
{
	/* MAX_TIMEOUT is used for a call to pthread_cond_timedwait_relative_np.
	 * The issue is bionic has switched to using absolute timeouts instead of
	 * relative timeouts, and a maximum time value can cause an overflow in
	 * the function converting relative to absolute timespecs if unpatched. */
	uint8_t *MAX_TIMEOUT;

	MAX_TIMEOUT = dlsym(libHandle, "MAX_TIMEOUT");
	if (CC_UNLIKELY(!MAX_TIMEOUT)) {
		RLOGE("%s: MAX_TIMEOUT could not be found!", __func__);
		return;
	}
	RLOGD("%s: MAX_TIMEOUT found at %p!", __func__, MAX_TIMEOUT);

	/* We need to patch the first byte, since we're little endian
	 * we need to move forward 3 bytes to get that byte. */
	MAX_TIMEOUT += 3;
	RLOGD("%s: MAX_TIMEOUT is currently 0x%" PRIX8 "FFFFFF", __func__, *MAX_TIMEOUT);
	if (CC_LIKELY(*MAX_TIMEOUT == 0x7F)) {
		*MAX_TIMEOUT = 0x1F;
		RLOGI("%s: MAX_TIMEOUT was changed to 0x%" PRIX8 "FFFFFF", __func__, *MAX_TIMEOUT);
	} else {
		RLOGW("%s: MAX_TIMEOUT was not 0x7F; leaving alone", __func__);
	}
}

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

		/* Fix GSM signal strength */
		p_cur->GW_SignalStrength.signalStrength = gsmSignalStrength;
		p_cur->GW_SignalStrength.bitErrorRate = -1;
	} else {
		RLOGE("%s: Not an SignalStrength_v5\n", __func__);
	}
}

static void fixupSetupDataCallRequest(int request, RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	/**
	 * Old (pre-GB) RILs don't return the data call list, instead they return
	 * ((char **)response)[0] indicating PDP CID, which is generated by RIL. This Connection ID is
	 *                          used in GSM/UMTS and CDMA
	 * ((char **)response)[1] indicating the network interface name for GSM/UMTS or CDMA
	 * ((char **)response)[2] indicating the IP address for this interface for GSM/UMTS
	 *                          and NULL for CDMA
	 * so hack in a call to get the list
	 **/
#if 0
	// Theoretically, this would be the better way to do things as it would return all the data calls, unfortunately the RIL hangs.
	if(e == RIL_E_SUCCESS) {
		RLOGD("%s: %s received, return RIL_REQUEST_DATA_CALL_LIST", __func__, requestToString(request));
		onRequestShim(RIL_REQUEST_DATA_CALL_LIST, NULL, 0, t);
	} else {
		RLOGE("%s: %s has error, sending as-is", __func__, requestToString(request));
		rilEnv->OnRequestComplete(t, e, response, responselen);
	}
#else
	if(e != RIL_E_SUCCESS) {
		RLOGE("%s: %s has error, sending as-is", __func__, requestToString(request));
		goto error;
	}

	if(responselen != sizeof(RIL_Data_Call_Response_v3)) {
		RLOGE("%s: %s has invalid responselen of %d, sending as-is", __func__, requestToString(request), responselen);
		goto error;
	}

	char gatewayBuf[PROPERTY_VALUE_MAX];
	char dns1Buf[PROPERTY_VALUE_MAX];
	char dns2Buf[PROPERTY_VALUE_MAX];
	char dnsString[2 * PROPERTY_VALUE_MAX + 1];
	int err;

	// Cast our original response
	RIL_Data_Call_Response_v3 *origResponse = (RIL_Data_Call_Response_v3 *) response;

	// Samsung RIL stores DNS and gateway in props
	property_get("net.pdp0.gw", gatewayBuf, "");
	property_get("net.pdp0.dns1", dns1Buf, "");
	property_get("net.pdp0.dns2", dns2Buf, "");

	if(strcmp(dns1Buf, ""))
		if(strcmp(dns2Buf, ""))
			sprintf(dnsString, "");
		else
			sprintf(dnsString, "%s", dns2Buf);
	else if(strcmp(dns2Buf, ""))
		sprintf(dnsString, "%s", dns1Buf);
	else
		sprintf(dnsString, "%s %s", dns1Buf, dns2Buf);

	RIL_Data_Call_Response_v6 *newResponse =
		alloca(sizeof(RIL_Data_Call_Response_v6));
	if(newResponse == NULL) {
		RLOGE("%s: %s failed to allocte memory, sending as-is", __func__, requestToString(request));
		goto error;
	}

	newResponse[0].status = PDP_FAIL_NONE;
	newResponse[0].suggestedRetryTime = -1;
	newResponse[0].cid = atoi(origResponse[0].cid);
	// FIXME Determine this 0=inactive, 1=active/physical link down, 2=active/physical link up
	newResponse[0].active = 2;
	struct addrinfo *res = NULL;
	err = getaddrinfo(origResponse[0].address, NULL, NULL, &res);
	if(err != 0 || res == NULL) {
		RLOGE("%s: %s couldn't determine type, using IP", __func__, requestToString(request));
		newResponse[0].type = "IP";
	} else if(res->ai_family == AF_INET) {
		newResponse[0].type = "IP";
	} else if(res->ai_family == AF_INET6) {
		newResponse[0].type = "IPV6";
	} else {
		newResponse[0].type = "IPV4V6";
	}
	free(res);

	newResponse[0].ifname = origResponse[0].ifname;
	newResponse[0].addresses = origResponse[0].address;
	newResponse[0].dnses = dnsString;
	newResponse[0].gateways = gatewayBuf;

	rilEnv->OnRequestComplete(t, e, newResponse, sizeof(RIL_Data_Call_Response_v6));
	return;
error:
	rilEnv->OnRequestComplete(t, e, response, responselen);
#endif
}

static void fixupDataCallListRequest(int request, RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	// No actual data
	if(e != RIL_E_SUCCESS) {
		RLOGE("%s: %s has error, sending as-is", __func__, requestToString(request));
		goto error;
	}

	if(responselen == 0) {
		RLOGE("%s: %s has responselen of 0, sending as-is", __func__, requestToString(request));
		goto error;
	}

	if(responselen % sizeof(RIL_Data_CallResponse_v4) != 0) {
		RLOGE("%s: %s has invalid responselen of %d, sending as-is", __func__, requestToString(request), responselen);
		goto error;
	}
	char gatewayBuf[PROPERTY_VALUE_MAX];
	char dns1Buf[PROPERTY_VALUE_MAX];
	char dns2Buf[PROPERTY_VALUE_MAX];
	char dnsString[2 * PROPERTY_VALUE_MAX + 1];
	int numEntries = responselen / sizeof(RIL_Data_CallResponse_v4);

	// Cast our original response
	RIL_Data_CallResponse_v4 *origResponses = (RIL_Data_CallResponse_v4 *) response;

	RIL_Data_Call_Response_v6 *responses =
		alloca(numEntries * sizeof(RIL_Data_Call_Response_v6));
	if(responses == NULL) {
		RLOGE("%s: %s failed to allocte memory, sending as-is", __func__, requestToString(request));
		goto error;
	}

	// Samsung RIL stores DNS and gateway in props
	property_get("net.pdp0.gw", gatewayBuf, "");
	property_get("net.pdp0.dns1", dns1Buf, "");
	property_get("net.pdp0.dns2", dns2Buf, "");

	if(strcmp(dns1Buf, ""))
		if(strcmp(dns2Buf, ""))
			sprintf(dnsString, "");
		else
			sprintf(dnsString, "%s", dns2Buf);
	else if(strcmp(dns2Buf, ""))
		sprintf(dnsString, "%s", dns1Buf);
	else
		sprintf(dnsString, "%s %s", dns1Buf, dns2Buf);

	int i;
	for (i = 0; i < numEntries; i++) {
		responses[i].status = PDP_FAIL_NONE;
		responses[i].suggestedRetryTime = -1;
		responses[i].cid = origResponses[i].cid;
		responses[i].active = origResponses[i].active;
		responses[i].type = origResponses[i].type;
		responses[i].ifname = "pdp0";
		responses[i].addresses = origResponses[i].address;
		responses[i].dnses = dnsString;
		responses[i].gateways = gatewayBuf;
	}

	rilEnv->OnRequestComplete(t, e, responses, numEntries * sizeof(RIL_Data_Call_Response_v6));
	return;
error:
		rilEnv->OnRequestComplete(t, e, response, responselen);
}

static void fixupDataCallListUnsol(int unsolResponse, const void *data, size_t datalen) {
	// No actuall data
	if(datalen == 0 || datalen % sizeof(RIL_Data_CallResponse_v4) != 0) {
		RLOGE("%s: %s has response length 0, sending as-is", __func__, requestToString(unsolResponse));
		rilEnv->OnUnsolicitedResponse(unsolResponse, data, datalen);
		return;
	}
	char gatewayBuf[PROPERTY_VALUE_MAX];
	char dns1Buf[PROPERTY_VALUE_MAX];
	char dns2Buf[PROPERTY_VALUE_MAX];
	char dnsString[2 * PROPERTY_VALUE_MAX + 1];
	int numEntries = datalen / sizeof(RIL_Data_CallResponse_v4);

	// Cast our original responses
	RIL_Data_CallResponse_v4 *origResponses = (RIL_Data_CallResponse_v4 *) data;

	RIL_Data_Call_Response_v6 *responses =
		alloca(numEntries * sizeof(RIL_Data_Call_Response_v6));
	if(responses == NULL) {
		RLOGE("%s: %s failed to allocte memory, sending as-is", __func__, requestToString(unsolResponse));
		rilEnv->OnUnsolicitedResponse(unsolResponse, data, datalen);
		return;
	}

	// Samsung RIL stores DNS and gateway in props
	property_get("net.pdp0.gw", gatewayBuf, "");
	property_get("net.pdp0.dns1", dns1Buf, "");
	property_get("net.pdp0.dns2", dns2Buf, "");

	if(strcmp(dns1Buf, ""))
		if(strcmp(dns2Buf, ""))
			sprintf(dnsString, "");
		else
			sprintf(dnsString, "%s", dns2Buf);
	else if(strcmp(dns2Buf, ""))
		sprintf(dnsString, "%s", dns1Buf);
	else
		sprintf(dnsString, "%s %s", dns1Buf, dns2Buf);

	int i;
	for (i = 0; i < numEntries; i++) {
		responses[i].status = PDP_FAIL_NONE;
		responses[i].suggestedRetryTime = -1;
		responses[i].cid = origResponses[i].cid;
		responses[i].active = origResponses[i].active;
		responses[i].type = origResponses[i].type;
		responses[i].ifname = "pdp0";
		responses[i].addresses = origResponses[i].address;
		responses[i].dnses = dnsString;
		responses[i].gateways = gatewayBuf;
	}
	rilEnv->OnUnsolicitedResponse(unsolResponse, responses, numEntries * sizeof(RIL_Data_Call_Response_v6));
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

				RIL_CardStatus_v6 v6response;

				v6response.card_state = v5response->card_state;
				v6response.universal_pin_state = v5response->universal_pin_state;
				v6response.gsm_umts_subscription_app_index = v5response->gsm_umts_subscription_app_index;
				v6response.cdma_subscription_app_index = v5response->cdma_subscription_app_index;
				v6response.ims_subscription_app_index = -1;
				v6response.num_applications = v5response->num_applications;

				int i;
				for (i = 0; i < RIL_CARD_MAX_APPS; ++i)
					memcpy(&v6response.applications[i], &v5response->applications[i], sizeof(RIL_AppStatus));

				rilEnv->OnRequestComplete(t, e, &v6response, sizeof(RIL_CardStatus_v6));
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
		case RIL_REQUEST_SETUP_DATA_CALL:
		{
			RLOGD("%s: got request %s changing to data call list\n", __func__, requestToString(request));
			fixupSetupDataCallRequest(request, t, e, response, responselen);
			return;
		}
		case RIL_REQUEST_DATA_CALL_LIST:
		{
			RLOGD("%s: got request %s changing to v6\n", __func__, requestToString(request));
			fixupDataCallListRequest(request, t, e, response, responselen);
			return;
		}			
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
		{
			/* The Samsung RIL reports the signal strength in a strange way... */
			if (data != NULL && datalen >= sizeof(RIL_SignalStrength_v5))
				fixupSignalStrength((void*) data, datalen);
			break;
		}
		case RIL_UNSOL_DATA_CALL_LIST_CHANGED:
		{
			RLOGD("%s: got unsol response %s changing to v6\n", __func__, requestToString(unsolResponse));
			fixupDataCallListUnsol(unsolResponse, data, datalen);
			return;
		}
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
		if (!strcmp(propBuf, "galaxys4gmtd") || !strcmp(propBuf, "galaxys4gubi") || !strcmp(propBuf, "fascinate4gubi")) {
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
	if (ariesVariant == VARIANT_GALAXYS4G) {
		origRil = dlopen(GALAXYS4G_RIL_LIB_PATH, RTLD_LOCAL);
		if (CC_UNLIKELY(!origRil)) {
			RLOGE("%s: failed to load '" GALAXYS4G_RIL_LIB_PATH  "': %s\n", __func__, dlerror());
			return NULL;
		}
	} else {
		origRil = dlopen(RIL_LIB_PATH, RTLD_LOCAL);
		if (CC_UNLIKELY(!origRil)) {
			RLOGE("%s: failed to load '" RIL_LIB_PATH  "': %s\n", __func__, dlerror());
			return NULL;
		}
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

	/* Fix RIL issues by patching memory: pre-init pass. */
	patchMem(origRil);

	origRilFunctions = origRilInit(&shimmedEnv, argc, argv);
	if (CC_UNLIKELY(!origRilFunctions)) {
		RLOGE("%s: the original RIL_Init derped.\n", __func__);
		goto fail_after_dlopen;
	}
	RLOGD("%s: succesfully ran the original RIL_init", __func__);

	/* Shim functions as needed. */
	shimmedFunctions = *origRilFunctions;
	shimmedFunctions.onRequest = onRequestShim;
	shimmedFunctions.version = 6;

	return &shimmedFunctions;

fail_after_dlopen:
	dlclose(origRil);
	return NULL;
}
