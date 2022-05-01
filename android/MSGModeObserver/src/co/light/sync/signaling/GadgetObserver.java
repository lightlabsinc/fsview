/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

package co.light.sync.signaling;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import android.os.SystemProperties;

public class GadgetObserver extends BroadcastReceiver {

    private static final String ACTION_USB_STATE = "android.hardware.usb.action.USB_STATE";
    @SuppressWarnings("unused")
    private static final String EXTRA_USB_CONNECTED = "connected";
    private static final String EXTRA_USB_CONFIGURED = "configured";
    private static final String EXTRA_USB_DATA_UNLOCKED = "unlocked";
    private static final String EXTRA_USB_FUNCTION_MASS_STORAGE = "mass_storage";

    private static final String PROPERTY_SYNC_ENABLE = "light.sync.enable";
    private static final String PROPERTY_SYNC_LINGER = "light.sync.linger";

    // the broadcast receiver process lifetime is 5 seconds
    // we want to keep the maximum time good enough for lab
    // testing, but a good default delay should be ~300-400
    private static final long MAX_SHUTDOWN_LINGER_MS = 2000;
    private static final long GOOD_SHUTDOWN_LINGER_MS = 400;

    private static final long clamp(long value, long min, long max) {
        return Math.min(Math.max(value, min), max);
    }

    private static final long linger() {
        try {
            return clamp(Long.valueOf(SystemProperties.get(PROPERTY_SYNC_LINGER)), 0, MAX_SHUTDOWN_LINGER_MS);
        } catch (NumberFormatException nfe) {
            return GOOD_SHUTDOWN_LINGER_MS;
        }
    }

    // constant ok: a new value would be queried after 5 seconds of silence
    private final long ifLingerMs = linger();

    private final Handler handler = new Handler();

    private final Runnable cutOff = new Runnable() {
        @Override
        public void run() {
            enable(false);
        }
    };

    private final void postCutOff() {
        if (ifLingerMs > 0) {
            handler.postDelayed(cutOff, ifLingerMs);
        } else {
            cutOff.run();
        }
    }

    private final void stopCutOff() {
        handler.removeCallbacks(cutOff);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            IntentFilter query = new IntentFilter(ACTION_USB_STATE);
            intent = context.registerReceiver(null, query);
        }
        if (ACTION_USB_STATE.equals(intent.getAction())) {
            boolean connected = intent.getBooleanExtra(EXTRA_USB_CONNECTED, false);
            boolean configured = intent.getBooleanExtra(EXTRA_USB_CONFIGURED, false);
            boolean unlocked = intent.getBooleanExtra(EXTRA_USB_DATA_UNLOCKED, false);
            boolean massStorage = intent.getBooleanExtra(EXTRA_USB_FUNCTION_MASS_STORAGE, false);
            // derivatives
            boolean dataUp = connected || configured;
            boolean modeOk = unlocked && massStorage;
            if (dataUp && modeOk) {
                stopCutOff();
                enable(true);
            } else if (modeOk) {
                postCutOff();
            } else {
                stopCutOff();
                enable(false);
            }
        }
    }

    private static void enable(boolean enable) {
        SystemProperties.set(PROPERTY_SYNC_ENABLE, enable ? "1" : "0");
    }
}
