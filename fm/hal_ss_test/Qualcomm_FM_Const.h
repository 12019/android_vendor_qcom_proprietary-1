/**
 * Copyright (c) 2013-2014 Qualcomm Technologies, Inc. All Rights Reserved.
 * Qualcomm Technologies Proprietary and Confidential.
 *
 **/

#ifndef __QCOM_FM_CONST_H__
#define __QCOM_FM_CONST_H__

//return related
#define IOCTL_SUCC 0
#define FM_SUCCESS 0
#define FM_FAILURE -1
#define PROP_SET_SUCC 0

#define MAX_VER_STR_LEN 40
#define INIT_LOOP_CNT 45

//Time in us
#define INIT_WAIT_TIMEOUT 200000
//Time in secs
#define READY_EVENT_TIMEOUT 5
#define SCAN_COMPL_TIMEOUT 1280
#define TUNE_EVENT_TIMEOUT 2
#define SEEK_COMPL_TIMEOUT 60

#define TUNE_MULT 16
#define CAL_DATA_SIZE 23
#define STD_BUF_SIZE 256

//RDS GROUPS
#define GRP_3A 64

//data related to enabling various
//rds data RT, PS, AF LIST, AF JMP
#define RDS_GRP_RT_EN 1
#define RDS_GRP_PS_EN 2
#define RDS_AF_JMP_LIST_EN 8
#define RDS_GRP_AFJMP_EN 16

//Data for PS received
#define MAX_PS_LEN 8
#define PS_STR_NUM_IND 0
#define PS_DATA_OFFSET_IND 5

//RT related
#define MAX_RT_LEN 64
#define RT_LEN_IND 0
#define RT_DATA_OFFSET_IND 5
#define RT_A_B_FLAG_IND 4

//ERT related
#define ERT_LEN_IND 0
#define ERT_DATA_OFFSET_IND 3

//Common to RT, PS
#define FIRST_CTRL_CHAR 0
#define LAST_CTRL_CHAR 31
#define FIRST_NON_PRNT_CHAR  127
#define SPACE_CHAR 32

//RT PLUS related constanst
#define RT_OR_ERT_IND 1
#define RT_PLUS_TAGS_NUM_IND 0
#define MAX_RT_PLUS_TAGS 2
#define TAGS_DATA_BEGIN_OFFSET 4
#define ITEM_TOGGLE_IND 2
#define ITEM_RUNNING_IND 3
#define DUMMY_TAG_CODE 0

typedef  unsigned int UINT;
typedef  unsigned long ULINT;

//STRING LITERALS
const char *const FM_MODE_PROP = "hw.fm.mode";
const char *const FM_VERSION_PROP = "hw.fm.version";
const char *const FM_INIT_PROP = "hw.fm.init";
const char *const SCRIPT_START_PROP = "ctl.start";
const char *const FM_SOC_DL_SCRIPT = "fm_dl";
const char *const SCRIPT_STOP_PROP = "ctl.stOP";
const char *const CALIB_DATA_NAME = "/data/app/Riva_fm_cal";
const char *const SOC_PATCH_DL_SCRPT = "fm_dl";
const char *const FM_DEVICE_PATH = "/dev/radio0";
const char *const FM_PERFORMANCE_PARAMS = "/etc/fm/fm_srch_af_th.conf";

const UINT V4L2_CTRL_CLASS_USER = 0x00980000;
const UINT V4L2_CID_BASE = (V4L2_CTRL_CLASS_USER | 0x900);
const UINT V4L2_CID_AUDIO_MUTE  = (V4L2_CID_BASE + 9);
const UINT SEARCH_DWELL_TIME = 2;
const UINT SEEK_DWELL_TIME = 0;

//BAND LIMITS
const UINT US_BAND_LOW = 87500;
const UINT US_BAND_HIGH = 108000;
const UINT EU_BAND_LOW = 87500;
const UINT EU_BAND_HIGH = 108000;
const UINT JAP_BAND_STD_LOW = 76000;
const UINT JAP_BAND_STD_HIGH = 90000;
const UINT JAP_BAND_WIDE_LOW = 90000;
const UINT JAP_BAND_WIDE_HIGH = 108000;
const UINT USR_DEF_BAND_LOW = 87500;
const UINT USR_DEF_BAND_HIGH = 108000;

//RDS standard type
enum RDS_STD
{
    RBDS,
    RDS,
    NO_RDS_RBDS,
};

enum DE_EMPHASIS
{
    DE_EMP75,
    DE_EMP50,
};

//Band Type
enum FM_REGION
{
    BAND_87500_108000 = 1,
    BAND_76000_108000,
    BAND_76000_90000,
};

enum FM_AUDIO_PATH
{
    AUDIO_DIGITAL_PATH,
    AUDIO_ANALOG_PATH,
};

enum FM_DEVICE
{
    FM_DEV_NONE,
    FM_RX,
    FM_TX,
};

enum BUFF_INDEXES
{
    STATION_LIST_IND,
    EVENT_IND,
    RT_IND,
    PS_IND,
    AF_LIST_IND = PS_IND + 2,
    RT_PLUS_IND = 11,
    ERT_IND,
};

enum SEARCH_MODE
{
    SEEK_MODE,
    SCAN_MODE,
};

enum SEARCH_DIR
{
    SEARCH_DOWN,
    SEARCH_UP,
};

//CHANNEL SPACING for samsung
enum SS_CHAN_SPACING
{
    SS_CHAN_SPACE_200 = 20,
    SS_CHAN_SPACE_100 = 10,
    SS_CHAN_SPACE_50 = 5,
};

enum QCOM_CHAN_SPACING
{
    QCOM_CHAN_SPACE_200,
    QCOM_CHAN_SPACE_100,
    QCOM_CHAN_SPACE_50,
};

enum AUDIO_MODE
{
    MONO,
    STEREO,
};

//HARD MUTE MODESS
enum HARD_MUTE_MODE
{
    UNMUTE_L_R_CHAN,
    MUTE_L_CHAN,
    MUTE_R_CHAN,
    MUTE_L_R_CHAN,
};

//SOFT MUTE STATES
enum SOFT_MUTE_MODE
{
    SMUTE_DISABLED,
    SMUTE_ENABLED,
};

//FM EVENTS FROM KERNEL
enum FM_EVENTS
{
    READY_EVENT,
    TUNE_EVENT,
    SEEK_COMPLETE_EVENT,
    SCAN_NEXT_EVENT,
    RAW_RDS_EVENT,
    RT_EVENT,
    PS_EVENT,
    ERROR_EVENT,
    BELOW_TH_EVENT,
    ABOVE_TH_EVENT,
    STEREO_EVENT,
    MONO_EVENT,
    RDS_AVAL_EVENT,
    RDS_NOT_AVAL_EVENT,
    SRCH_LIST_EVENT,
    AF_LIST_EVENT,
    DISABLED_EVENT = 18,
    RDS_GRP_MASK_REQ_EVENT,
    RT_PLUS_EVENT,
    ERT_EVENT,
    AF_JMP_EVENT,
};

//FM STATES
enum fm_states
{
    FM_OFF,
    FM_OFF_IN_PROGRESS,
    FM_ON_IN_PROGRESS,
    FM_ON,
    FM_TUNE_IN_PROGRESS,
    SEEK_IN_PROGRESS,
    SCAN_IN_PROGRESS,
};

//V4L2 CONTROLS FOR FM DRIVER
enum FM_V4L2_PRV_CONTROLS
{
    V4L2_CID_PRV_BASE = 0x8000000,
    V4L2_CID_PRV_SRCHMODE,
    V4L2_CID_PRV_SCANDWELL,
    V4L2_CID_PRV_SRCHON,
    V4L2_CID_PRV_STATE,
    V4L2_CID_PRV_TRANSMIT_MODE,
    V4L2_CID_PRV_RDSGROUP_MASK,
    V4L2_CID_PRV_REGION,
    V4L2_CID_PRV_SIGNAL_TH,
    V4L2_CID_PRV_SRCH_PTY,
    V4L2_CID_PRV_SRCH_PI,
    V4L2_CID_PRV_SRCH_CNT,
    V4L2_CID_PRV_EMPHASIS,
    V4L2_CID_PRV_RDS_STD,
    V4L2_CID_PRV_CHAN_SPACING,
    V4L2_CID_PRV_RDSON,
    V4L2_CID_PRV_RDSGROUP_PROC,
    V4L2_CID_PRV_LP_MODE,
    V4L2_CID_PRV_INTDET = V4L2_CID_PRV_BASE + 25,
    V4L2_CID_PRV_AF_JUMP = V4L2_CID_PRV_BASE + 27,
    V4L2_CID_PRV_SOFT_MUTE = V4L2_CID_PRV_BASE + 30,
    V4L2_CID_PRV_AUDIO_PATH = V4L2_CID_PRV_BASE + 41,
    V4L2_CID_PRV_SINR = V4L2_CID_PRV_BASE + 44,
    V4L2_CID_PRV_ON_CHANNEL_THRESHOLD = V4L2_CID_PRV_BASE + 0x2D,
    V4L2_CID_PRV_OFF_CHANNEL_THRESHOLD,
    V4L2_CID_PRV_SINR_THRESHOLD,
    V4L2_CID_PRV_SINR_SAMPLES,
    V4L2_CID_PRV_SPUR_FREQ,
    V4L2_CID_PRV_SPUR_FREQ_RMSSI,
    V4L2_CID_PRV_SPUR_SELECTION,
    V4L2_CID_PRV_AF_RMSSI_TH = V4L2_CID_PRV_BASE + 0x36,
    V4L2_CID_PRV_AF_RMSSI_SAMPLES,
    V4L2_CID_PRV_GOOD_CH_RMSSI_TH,
    V4L2_CID_PRV_SRCHALGOTYPE,
    V4L2_CID_PRV_CF0TH12,
    V4L2_CID_PRV_SINRFIRSTSTAGE,
    V4L2_CID_PRV_RMSSIFIRSTSTAGE,
    V4L2_CID_PRV_SOFT_MUTE_TH,
    V4L2_CID_PRV_IRIS_RDSGRP_RT,
    V4L2_CID_PRV_IRIS_RDSGRP_PS_SIMPLE,
    V4L2_CID_PRV_IRIS_BLEND_SINRHI,
    V4L2_CID_PRV_IRIS_BLEND_RMSSIHI,
    V4L2_CID_PRV_IRIS_RDSGRP_AFLIST,
    V4L2_CID_PRV_IRIS_RDSGRP_ERT,
    V4L2_CID_PRV_IRIS_RDSGRP_RT_PLUS,
    V4L2_CID_PRV_IRIS_RDSGRP_3A,

    V4L2_CID_PRV_IRIS_READ_DEFAULT = V4L2_CTRL_CLASS_USER + 0x928,
    V4L2_CID_PRV_IRIS_WRITE_DEFAULT,
    V4L2_CID_PRV_SET_CALIBRATION = V4L2_CTRL_CLASS_USER + 0x92A,
};
#endif
