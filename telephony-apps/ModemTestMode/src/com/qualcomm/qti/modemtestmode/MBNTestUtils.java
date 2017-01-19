/**
 * Copyright (c) 2014 Qualcomm Technologies, Inc.  All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 **/

package com.qualcomm.qti.modemtestmode;

import android.content.Context;
import android.content.Intent;
import android.os.SystemProperties;
import android.text.TextUtils;
import android.util.Log;

public class MBNTestUtils {
    private static final String TAG = "MBNTestUtils";
    //public static final String FIRMWARE_PARTITION_PATH = "/firmware/image";
    public static final String FIRMWARE_PARTITION_PATH = "/system/vendor/modemconfig";
    //public static final String GOLDEN_MBN_DIR = "/firmware/image/golden_mbn";
    public static final String GOLDEN_MBN_DIR = FIRMWARE_PARTITION_PATH;
    public static final String MBN_TO_GO_DIR = "/data/misc/radio/";

    //ROOT/Carrier/Device Type/MultiMode/
    public static final int MBN_PATH_MAX_LEVEL = 3;

    //TODO why need so many broadcasts?
    public static final String PDC_DATA_ACTION =
            "qualcomm.intent.action.ACTION_PDC_DATA_RECEIVED";
    public static final String PDC_CONFIG_CLEARED_ACTION =
            "qualcomm.intent.action.ACTION_PDC_CONFIGS_CLEARED";
    public static final String PDC_CONFIGS_VALIDATION_ACTION =
            "qualcomm.intent.action.ACTION_PDC_CONFIGS_VALIDATION";

    public static final String SUB_ID = "sub_id";
    public static final String PDC_ACTIVATED = "active";
    public static final String PDC_ERROR_CODE = "error";

    public static final int LOAD_MBN_SUCCESS = 0;
    public static final int LOAD_MBN_FAILED = -1;
    public static final int LOAD_MBN_NEED_CLEANUP = -2;

    public static final int CLEANUP_SUCCESS = 0;
    public static final int CLEANUP_FAILED = -1;
    public static final int CLEANUP_ALREADY = -3;

    public static final int SUB0 = 0;
    public static final int SUB1 = 1;
    public static final int SUB2 = 2;
    public static final int DEFAULT_SUB = SUB0;

    public static final int MBN_FROM_UNKNOWN = 0;
    public static final int MBN_FROM_APP = 1;
    public static final int MBN_FROM_GOLDEN = 2;
    public static final int MBN_FROM_PC = 3;
    public static final String APP_MBN_ID_PREFIX = "MBNApp_";
    public static final String GOLDEN_MBN_ID_PREFIX = "GOLDEN_";
    public static final String MBN_FILE_SUFFIX = ".mbn";

    // ROOT/carrier/device_type/DSDS/index
    public final static int MBN_PATH_MIN_LENGTH = 5;

    public static String getGeneralMBNPath() {
        String value = getPropertyValue("persist.radio.app_mbn_path");
        Log.d(TAG, "MBNTest_ persist.radio.app_mbn_path, |" + value + "|");
        if (TextUtils.isEmpty(value)) {
            return FIRMWARE_PARTITION_PATH;
        } else {
            return value;
        }
    }

    public static String getGoldenMBNPath() {
        String value = getPropertyValue("persist.radio.golden_mbn_path");
        Log.d(TAG, "MBNTest_ persist.radio.golden_mbn_path, |" + value + "|");
        if (TextUtils.isEmpty(value)) {
            return GOLDEN_MBN_DIR;
        } else {
            return value;
        }
    }

    public static boolean mbnNeedToGo() {
        String value = getPropertyValue("persist.radio.mbn_to_go");
        Log.d(TAG, "MBNTest_ persist.radio.mbn_to_go, |" + value + "|");
        if (value != null && value.toLowerCase().equals("true")) {
            return true;
        } else {
            return false;
        }
    }

    // return Multi-Mode according to ss/da/ds
    public static String getMultiSimMode(String mode) {
        if (mode != null) {
            if (mode.toLowerCase().contains("ss") || mode.toLowerCase().contains("singlesim")) {
                return "ssss";
            } else if (mode.toLowerCase().contains("da")) {
                return "dsda";
            } else if (mode.toLowerCase().contains("ds")) {
                return "dsds";
            }
        }
        return "";
    }

    public static String getPropertyValue(String property) {
        return SystemProperties.get(property);
    }

    public static boolean isMultiSimConfigure(String mode) {
        return mode.toLowerCase().contains("ds") || mode.toLowerCase().contains("da");
    }

    public static void rebootSystem(Context context) {
        Intent i = new Intent(Intent.ACTION_REBOOT);
        i.putExtra("nowait", 1);
        i.putExtra("interval", 1);
        i.putExtra("window", 0);
        context.sendBroadcast(i);
    }
}
