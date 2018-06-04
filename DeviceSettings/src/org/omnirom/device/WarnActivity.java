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

import android.os.Bundle;
import android.content.Intent;
import android.app.Dialog;
import android.app.Activity;
import android.app.AlertDialog;
import android.view.Window;
import android.content.DialogInterface;
import android.util.Log;

public class WarnActivity extends Activity {
    public static final String KEY_REASON = "sanity_reason";
    public static final String REASON_INVALID_IMEI = "invalid_imei";

    @Override
    public void onCreate(Bundle icicle) {
        super.onCreate(icicle);
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        Bundle extras = getIntent().getExtras();
        String reason = extras.getString(KEY_REASON);

        if (REASON_INVALID_IMEI.equals(reason)) {
            showInvalidImei();
        }
    }

    private void showInvalidImei() {
        new AlertDialog.Builder(this)
                .setTitle(getString(R.string.imei_not_sane_title))
                .setMessage(getString(R.string.imei_not_sane_message))
                .setPositiveButton(getString(R.string.imei_not_sane_ok),
                        new DialogInterface.OnClickListener() {
                            public void onClick(DialogInterface dialog, int id) {
                                dialog.cancel();
                                finish();
                            }
                        })
                .setCancelable(false)
                .create().show();
    }
}
