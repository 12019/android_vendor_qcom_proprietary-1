/*===========================================================================

  Copyright (c) 2010-2014 Qualcomm Technologies, Inc.  All Rights Reserved.
  Qualcomm Technologies Proprietary and Confidential.

===========================================================================*/

#ifndef __THERMAL_H__
#define __THERMAL_H__

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include "thermal_config.h"
#include "devices_manager.h"
#include "sensors_manager.h"

#ifdef ANDROID
#  include "cutils/properties.h"
#  ifdef USE_ANDROID_LOG
#    define LOG_TAG         "ThermalEngine"
#    include "cutils/log.h"
#  endif
#else
#  include <syslog.h>
#endif
#include "common_log.h" /* define after cutils/log.h */

#ifdef USE_GLIB
#include <glib/gprintf.h>
#define strlcat g_strlcat
#define strlcpy g_strlcpy
#endif /* USE_GLIB */

#ifndef CONFIG_FILE_DEFAULT
#define CONFIG_FILE_DEFAULT  "/system/etc/thermal-engine.conf"
#endif

#define MAX_CPUS              (4)       /* Supporting up to 4 core systems */

/* Abstract local socket name that report actions are sent on */
#define UI_LOCALSOCKET_NAME  "THERMALE_UI"

#define MAX_PATH             (256)
#define REPORT_MSG_MAX       (60)
#define UINT_BUF_MAX         (12)

/* core limit temperature sanity check values in degC */
#define CORELIMIT_MIN        (50)
#define CORELIMIT_MAX        (150)

/* Common thermal sysfs defines */
#define TZ_NODE "/sys/class/thermal/thermal_zone%d"
#define TZ_MODE "/sys/devices/virtual/thermal/thermal_zone%d/mode"
#define TZ_TEMP "/sys/devices/virtual/thermal/thermal_zone%d/temp"
#define TZ_TYPE "/sys/devices/virtual/thermal/thermal_zone%d/type"

/* CPU Frequency Scaling Action */
#define GOVERNOR_NODE    "/cpufreq/scaling_governor"
#define FMAX_INFO_NODE   "/cpufreq/cpuinfo_max_freq"
#define FREQ_MAX_NODE    "/cpufreq/scaling_max_freq"
#define FREQ_MIN_NODE    "/cpufreq/scaling_min_freq"
#define FREQ_USR_NODE    "/cpufreq/scaling_setspeed"
#define FREQ_RPT_NODE    "/cpufreq/scaling_cur_freq"
#define HOTPLUG_NODE     "/online"
#define CPU_SYSFS_DIR    "/sys/devices/system/cpu"
#define CPU_SYSFS(NODE)  (CPU_SYSFS_DIR "/cpu%d" NODE)

/* GPU Frequency Scaling Action */
#define GPU_FREQ_MAX_NODE   "/max_gpuclk"
#define GPU_FREQ_RPT_NODE   "/gpuclk"
#define GPU_AVAIL_FREQ_NODE "/gpu_available_frequencies"
#define GPU_SYSFS_DIR       "/sys/class/kgsl"
#define GPU_SYSFS(NODE)   (GPU_SYSFS_DIR "/kgsl-3d%d" NODE)

enum therm_msm_id {
	THERM_MSM_UNKNOWN = 0,
	THERM_MSM_8960,
	THERM_MSM_8960AB,
	THERM_MSM_8930,
	THERM_MSM_8064,
	THERM_MSM_8064AB,
	THERM_MSM_8974,
	THERM_MSM_8974PRO_AA,
	THERM_MSM_8974PRO_AB,
	THERM_MSM_8974PRO_AC,
	THERM_MDM_9625,
	THERM_MSM_8226,
	THERM_MSM_8926,
	THERM_MSM_8610,
	THERM_MSM_8084,
	THERM_MSM_8x62,
	THERM_MDM_9635,
	THERM_MPQ_8092,
};

enum therm_hw_platform {
	THERM_PLATFORM_UNKNOWN = 0,
	THERM_PLATFORM_SURF,
	THERM_PLATFORM_FFA,
	THERM_PLATFORM_FLUID,
	THERM_PLATFORM_SVLTE_FFA,
	THERM_PLATFORM_SVLTE_SURF,
	THERM_PLATFORM_MTP,
	THERM_PLATFORM_LIQUID,
	THERM_PLATFORM_DRAGON,
	THERM_PLATFORM_QRD,
};

enum therm_msm_version {
	THERM_VERSION_UNKNOWN = 0,
	THERM_VERSION_V1,
	THERM_VERSION_V2,
};

enum therm_msm_id therm_get_msm_id(void);
enum therm_hw_platform therm_get_hw_platform(void);
enum therm_msm_version therm_get_msm_version(void);

/* Utility macros */
#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
/* Convert from Celcius to milli Celsius */
#define CONV(x) (int)(x * 1000)
#define RCONV(x) (int)(x / 1000)

enum {
	THRESHOLD_CLEAR = -1,
	THRESHOLD_NOCHANGE = 0,
	THRESHOLD_CROSS = 1
} threshold_trigger;

#ifdef USE_ANDROID_LOG
#define msg(format, ...)   LOGE(format, ## __VA_ARGS__)
#define info(format, ...)   LOGI(format, ## __VA_ARGS__)
#define dbg(format, ...)   LOGD(format, ##__VA_ARGS__)
#else
#define msg(format, ...)   syslog(LOG_ERR, format, ## __VA_ARGS__)
#define info(format, ...)   syslog(LOG_INFO, format, ## __VA_ARGS__)
#define dbg(format, ...)   syslog(LOG_DEBUG, format, ## __VA_ARGS__)
#endif

#define PRINT_SETTING(flag, format, ...)		\
	do {						\
		if (flag == PRINT_TO_INFO)		\
			info(format, ## __VA_ARGS__);	\
		else					\
			printf(format"\n", ## __VA_ARGS__);\
	} while (0)

#define dbgmsg(format, ...)				\
	do {						\
		if (debug_output >= LOG_LVL_DBG)	\
			dbg(format, ## __VA_ARGS__);	\
	} while (0)


enum log_lvl {
	LOG_LVL_EMER = 0,
	LOG_LVL_ERR  = 1,
	LOG_LVL_INFO = 2,
	LOG_LVL_DBG  = 3
};

enum log_type {
	LOG_NONE         = 0x0000,
	LOG_LOGCAT       = 0x0001,
	LOG_LOCAL_SOCKET = 0x0002
};

#define thermalmsg(loglevel, logtype, format, ...)				\
	do {									\
		if ((logtype & LOG_LOGCAT) && (loglevel <= debug_output))	\
			info(format, ## __VA_ARGS__);				\
		if (logtype & LOG_LOCAL_SOCKET) {				\
			char logstring[REPORT_MSG_MAX];				\
			snprintf(logstring,REPORT_MSG_MAX,format, ## __VA_ARGS__);\
			write_to_local_socket(logstring, strlen(logstring) + 1);\
		}								\
	} while (0)

#define MITIGATION    "Mitigation"
#define SENSORS       "Sensor"

extern int exit_daemon;
extern int debug_output;
extern int enable_restore_brightness;
extern int num_cpus;
extern int num_gpus;
extern int minimum_mode;
extern int new_corelimit;
extern int dump_bcl;
extern int output_conf;
extern char *dump_bcl_file;

int read_line_from_file(const char *path, char *buf, size_t count);
int write_to_file(const char *path, const char *buf, size_t count);
void add_local_socket_fd(int sfd);
int write_to_local_file_socket(const char * socket_name, const char *msg, size_t count);
int write_to_local_socket(const char *msg, size_t count);
int get_tzn(const char *sensor_name);
unsigned int get_num_cpus(void);
int read_int_list_from_file(const char *path, int *arr, uint32_t arr_sz);
void sort_int_arr(int *arr, uint32_t arr_sz, uint8_t ascending);
int get_sysdev_dt_dir_name(char *dt_dir, const char *driver_name);
char *get_target_default_thermal_config_file(void);
int check_node(char *node_name);
void update_config_with_default_temp_limit(struct setting_info *cfg,
						int cfg_size,
						enum algo_type algo);

void thermal_monitor(struct thermal_setting_t *settings);
void thermal_monitor_init_data(struct thermal_setting_t *setting);


#ifdef ENABLE_MODEM_MITIGATION
int qmi_communication_init(void);
int qmi_communication_release(void);
int modem_request(int level);
int fusion_modem_request(int level);
int vdd_restrict_qmi_request(int level);
#else
static inline int qmi_communication_init(void)
{
	return -(EFAULT);
}
static inline int qmi_communication_release(void)
{
	return -(EFAULT);
}
static inline int modem_request(int level)
{
	return -(EFAULT);
}
static inline int fusion_modem_request(int level)
{
	return -(EFAULT);
}
static inline int vdd_restrict_qmi_request(int level)
{
	return -(EFAULT);
}
#endif

#ifdef ENABLE_MODEM_TS
int modem_ts_qmi_init(void);
int modem_qmi_ts_comm_release(void);
int modem_ts_temp_request(const char *sensor_id, int send_current_temp_report,
			   struct thresholds_req_t *thresh);
#else
static inline int modem_ts_qmi_init(void)
{
	return -(EFAULT);
}
static inline int modem_qmi_ts_comm_release(void)
{
	return -(EFAULT);
}

static inline int modem_ts_temp_request(const char *sensor_id,
				  int send_current_temp_report,
				  struct thresholds_req_t *thresh)
{
	return -(EFAULT);
}
#endif

#endif  /* __THERMAL_H__ */
