/*
 * Copyright (C) The CyanogenMod Project
 * Copyright (C) The OmniROM Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.omnirom.device;

import android.content.Context;
import android.content.Intent;
import android.telephony.TelephonyManager;
import android.util.Log;

public class Sanity {
    private static final String TAG = "DeviceSettings";
    private static final String BAD_IMEI[] = {
        "004999010640000"
    };

    public static void check(Context context) {
        TelephonyManager tm = (TelephonyManager) context.getSystemService(
                Context.TELEPHONY_SERVICE);
        String id = tm.getDeviceId();
        if (tm.getPhoneType() == TelephonyManager.PHONE_TYPE_GSM && !ensureIMEISanity(id)) {
            Log.e(TAG, "Invalid IMEI!");
            Intent intent = new Intent(context, WarnActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.putExtra(WarnActivity.KEY_REASON, WarnActivity.REASON_INVALID_IMEI);
            context.startActivity(intent);
            return;
        }
        Log.d(TAG, "Device is sane.");
    }

    public static boolean ensureIMEISanity(String id) {
        Log.d(TAG, "Current IMEI: " + id);
        for (int j = 0; j < BAD_IMEI.length; j++) {
            if (BAD_IMEI[j].equals(id)) {
                return false;
            }
        }
        return true;
    }
}
