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

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceManager;
import android.widget.Toast;

public class Bigmem implements OnPreferenceChangeListener {

    private static final String FILE = "/sys/kernel/uacma/enable";

    public static boolean isSupported() {
        return Utils.fileExists(FILE);
    }

    /**
     * Restore bigmem setting from SharedPreferences. (Write to kernel.)
     * @param context       The context to read the SharedPreferences from
     */
    public static void restore(Context context) {
        if (!isSupported()) {
            return;
        }

        SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
        SharedPreferences.Editor editor = sharedPrefs.edit();
        editor.putString(DeviceSettings.KEY_BIGMEM, Utils.readValue(FILE));
        editor.commit();
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        Utils.writeValue(FILE, (String) newValue);

        String actualVal = Utils.readValue(FILE);
        if(actualVal.compareTo((String) newValue) != 0) {
            // We failed, create a dialog saying that and restore correct value
            AlertDialog.Builder builder = new AlertDialog.Builder(preference.getContext());
            builder.setTitle(R.string.ua_cma_failed_title)
                .setMessage(R.string.ua_cma_failed)
                .setNegativeButton(android.R.string.no, null)
                .setPositiveButton(android.R.string.yes, new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) { 
                        // Retry
                        onPreferenceChange(preference, newValue);
                    }
                })
                .show();
            SharedPreferences.Editor editor = preference.getEditor();
            editor.putString(DeviceSettings.KEY_BIGMEM, Utils.readValue(FILE));
            editor.commit();
            return false;
        }        
        return true;
    }
}
