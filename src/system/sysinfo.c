 /**
 * @file sysinfo.c
 * @brief System information implementation (Go: system/sysinfo.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <glib.h>
#include "sysinfo.h"
#include "dbus_core.h"
#include "exec_utils.h"
#include "ofono.h"

/* 读取文件内容 */
static int read_file(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 0;
}

/* 解析 /proc/meminfo */
static void parse_meminfo(SystemInfo *info) {
    char buf[4096];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) != 0) return;

    char *line = strtok(buf, "\n");
    while (line) {
        unsigned long val;
        if (sscanf(line, "MemTotal: %lu kB", &val) == 1) {
            info->total_ram = val / 1024;
        } else if (sscanf(line, "MemFree: %lu kB", &val) == 1) {
            info->free_ram = val / 1024;
        } else if (sscanf(line, "Cached: %lu kB", &val) == 1) {
            info->cached_ram = val / 1024;
        }
        line = strtok(NULL, "\n");
    }
}

double get_uptime(void) {
    char buf[64];
    if (read_file("/proc/uptime", buf, sizeof(buf)) != 0) return -1;
    double uptime;
    if (sscanf(buf, "%lf", &uptime) == 1) return uptime;
    return -1;
}


int get_serial(char *serial, size_t size) {
    char buf[1024];
    if (read_file("/home/cpuinfo", buf, sizeof(buf)) != 0) return -1;

    char *p = strstr(buf, "Serial");
    if (!p) return -1;

    /* 跳过 "Serial" 和非数字字符 */
    p += 6;
    while (*p && (*p < '0' || *p > '9')) p++;

    /* 提取数字 */
    size_t i = 0;
    while (*p >= '0' && *p <= '9' && i < size - 1) {
        serial[i++] = *p++;
    }
    serial[i] = '\0';
    return i > 0 ? 0 : -1;
}

int get_current_slot(char *slot, char *ril_path) {
    strcpy(slot, "unknown");
    strcpy(ril_path, "unknown");

    /* 使用 ofono D-Bus 接口获取数据卡 */
    char *datacard = ofono_get_datacard();
    if (!datacard) {
        return -1;
    }

    /* 解析路径 */
    if (strstr(datacard, "/ril_0")) {
        strcpy(slot, "slot1");
        strcpy(ril_path, "/ril_0");
    } else if (strstr(datacard, "/ril_1")) {
        strcpy(slot, "slot2");
        strcpy(ril_path, "/ril_1");
    } else {
        /* 其他路径格式，直接使用 */
        strncpy(ril_path, datacard, 31);
        ril_path[31] = '\0';
    }

    g_free(datacard);
    return 0;
}

int get_signal_strength(char *strength, size_t size) {
    char slot[16], ril_path[32];
    int strength_val = 0, dbm_val = 0;

    strcpy(strength, "N/A");

    if (get_current_slot(slot, ril_path) != 0 || strcmp(ril_path, "unknown") == 0) {
        return -1;
    }

    /* 使用 ofono D-Bus 接口获取信号强度 */
    if (ofono_network_get_signal_strength(ril_path, &strength_val, &dbm_val, OFONO_TIMEOUT_MS) != 0) {
        return -1;
    }

    /* 格式化输出: "XX%, -YY dBm" */
    snprintf(strength, size, "%d%%, -%d dBm", strength_val, dbm_val);
    return 0;
}

double get_thermal_temp(void) {
    char output[64];
    if (run_command(output, sizeof(output), "sh", "-c",
        "cat /sys/class/thermal/thermal_zone*/temp | awk '{sum+=$1} END {printf \"%.2f\", sum/NR/1000}'",
        NULL) != 0) {
        return -1;
    }
    double temp;
    if (sscanf(output, "%lf", &temp) == 1) return temp;
    return -1;
}


/* 前向声明 airplane.h 中的函数 */
extern int get_imei(char *imei, size_t size);
extern int get_iccid(char *iccid, size_t size);
extern int get_imsi(char *imsi, size_t size);
extern const char *get_carrier_from_imsi(const char *imsi);
extern int get_airplane_mode(void);

int get_system_info(SystemInfo *info) {
    struct utsname uts;
    char buf[256];

    /* 初始化默认值 */
    memset(info, 0, sizeof(SystemInfo));
    strcpy(info->hostname, "N/A");
    strcpy(info->sysname, "N/A");
    strcpy(info->release, "N/A");
    strcpy(info->version, "N/A");
    strcpy(info->machine, "N/A");
    strcpy(info->bridge_status, "N/A");
    strcpy(info->sim_slot, "N/A");
    strcpy(info->signal_strength, "N/A");
    strcpy(info->power_status, "N/A");
    strcpy(info->battery_health, "N/A");
    strcpy(info->ssid, "N/A");
    strcpy(info->passwd, "N/A");
    strcpy(info->select_network_mode, "N/A");
    strcpy(info->network_mode, "N/A");
    strcpy(info->network_type, "N/A");
    strcpy(info->network_band, "N/A");
    info->is_activated = 1;

    /* uname 信息 */
    if (uname(&uts) == 0) {
        strncpy(info->sysname, uts.sysname, sizeof(info->sysname) - 1);
        strncpy(info->release, uts.release, sizeof(info->release) - 1);
        strncpy(info->version, uts.version, sizeof(info->version) - 1);
        strncpy(info->machine, uts.machine, sizeof(info->machine) - 1);
        strncpy(info->hostname, uts.nodename, sizeof(info->hostname) - 1);
    }

    /* 内存信息 */
    parse_meminfo(info);

    /* 运行时间 */
    info->uptime = get_uptime();

    /* 序列号 */
    get_serial(info->serial, sizeof(info->serial));

    /* SIM 卡槽 */
    char ril_path[32];
    if (get_current_slot(info->sim_slot, ril_path) == 0) {
        strncpy(info->network_mode, ril_path, sizeof(info->network_mode) - 1);
    }

    /* 信号强度 */
    get_signal_strength(info->signal_strength, sizeof(info->signal_strength));

    /* 温度 */
    info->thermal_temp = get_thermal_temp();

    /* 电源状态 */
    if (read_file("/sys/class/power_supply/battery/status", buf, sizeof(buf)) == 0) {
        buf[strcspn(buf, "\n")] = '\0';
        strncpy(info->power_status, buf, sizeof(info->power_status) - 1);
    }

    /* 电池健康 */
    if (read_file("/sys/class/power_supply/battery/health", buf, sizeof(buf)) == 0) {
        buf[strcspn(buf, "\n")] = '\0';
        strncpy(info->battery_health, buf, sizeof(info->battery_health) - 1);
    }

    /* 电池容量 */
    if (read_file("/sys/class/power_supply/battery/capacity", buf, sizeof(buf)) == 0) {
        info->battery_capacity = atoi(buf);
    }

    /* IMEI */
    get_imei(info->imei, sizeof(info->imei));

    /* ICCID */
    get_iccid(info->iccid, sizeof(info->iccid));

    /* IMSI 和运营商 */
    if (get_imsi(info->imsi, sizeof(info->imsi)) == 0) {
        const char *carrier = get_carrier_from_imsi(info->imsi);
        strncpy(info->carrier, carrier, sizeof(info->carrier) - 1);
    }

    /* 飞行模式 */
    int airplane = get_airplane_mode();
    info->airplane_mode = (airplane == 1) ? 1 : 0;

    /* WiFi 信息 */
    if (read_file("/var/lib/connman/settings", buf, sizeof(buf)) == 0) {
        char *p = strstr(buf, "Tethering.Identifier=");
        if (p) {
            p += strlen("Tethering.Identifier=");
            char *end = strchr(p, '\n');
            if (end) *end = '\0';
            strncpy(info->ssid, p, sizeof(info->ssid) - 1);
        }
    }

    /* 网络模式选择 - 使用 ofono D-Bus 接口 */
    char mode_buf[64] = {0};
    if (strcmp(ril_path, "unknown") != 0 && strlen(ril_path) > 0) {
        if (ofono_network_get_mode_sync(ril_path, mode_buf, sizeof(mode_buf), OFONO_TIMEOUT_MS) == 0) {
            strncpy(info->select_network_mode, mode_buf, sizeof(info->select_network_mode) - 1);
        }
    }

    /* 网络类型和频段 */
    get_network_type_and_band(info->network_type, sizeof(info->network_type),
                              info->network_band, sizeof(info->network_band));

    /* QoS 签约速率 */
    get_qos_info(&info->qci, &info->downlink_rate, &info->uplink_rate);

    /* CPU 使用率 */
    info->cpu_usage = get_cpu_usage();

    return 0;
}


/* 获取 QoS 签约速率 */
/* AT+CGEQOSRDP 返回: +CGEQOSRDP: 1,8,0,0,0,0,500000,60000 */
/* 索引1=QCI, 索引6=下行速率(kbps), 索引7=上行速率(kbps) */
int get_qos_info(int *qci, int *downlink, int *uplink) {
    extern int execute_at(const char *command, char **result);
    char *result = NULL;
    
    *qci = 0;
    *downlink = 0;
    *uplink = 0;

    if (execute_at("AT+CGEQOSRDP", &result) != 0 || !result) {
        return -1;
    }

    /* 查找 +CGEQOSRDP: */
    char *p = strstr(result, "+CGEQOSRDP:");
    if (!p) {
        g_free(result);
        return -1;
    }

    p += strlen("+CGEQOSRDP:");
    
    /* 解析逗号分隔的值: 1,8,0,0,0,0,500000,60000 */
    int values[8] = {0};
    int count = 0;
    char *token = strtok(p, ",\n\r");
    while (token && count < 8) {
        values[count++] = atoi(token);
        token = strtok(NULL, ",\n\r");
    }

    if (count >= 8) {
        *qci = values[1];
        *downlink = values[6];
        *uplink = values[7];
    }

    g_free(result);
    return 0;
}

/* 获取网络类型和频段 */
int get_network_type_and_band(char *net_type, size_t type_size, char *band, size_t band_size) {
    char tech[32] = {0};
    int band_num = 0;
    
    strncpy(net_type, "N/A", type_size - 1);
    strncpy(band, "N/A", band_size - 1);

    /* 使用C语言D-Bus API获取网络信息 */
    if (ofono_get_serving_cell_info(tech, sizeof(tech), &band_num) != 0) {
        return -1;
    }

    /* 判断网络类型 */
    if (strcmp(tech, "nr") == 0) {
        strncpy(net_type, "5G NR", type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "N%d", band_num);
        }
    } else if (strcmp(tech, "lte") == 0) {
        strncpy(net_type, "4G LTE", type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "B%d", band_num);
        }
    } else if (strlen(tech) > 0) {
        strncpy(net_type, tech, type_size - 1);
        if (band_num > 0) {
            snprintf(band, band_size, "%d", band_num);
        }
    }

    return 0;
}

/* 获取 CPU 使用率 - 通过读取 /proc/stat 计算 */
/* /proc/stat 格式: cpu user nice system idle iowait irq softirq steal guest guest_nice */
/* CPU使用率 = 100 - (idle_diff / total_diff * 100) */

/* 静态变量保存上次采样数据 */
static unsigned long long prev_user = 0, prev_nice = 0, prev_system = 0;
static unsigned long long prev_idle = 0, prev_iowait = 0, prev_irq = 0;
static unsigned long long prev_softirq = 0, prev_steal = 0;
static int cpu_initialized = 0;

double get_cpu_usage(void) {
    char buf[1024];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    
    /* 读取 /proc/stat */
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    
    if (fgets(buf, sizeof(buf), f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);
    
    /* 解析第一行 cpu 数据 */
    /* 格式: cpu  user nice system idle iowait irq softirq steal [guest guest_nice] */
    int ret = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    if (ret < 4) return 0;
    
    /* 如果某些字段不存在，设为0 */
    if (ret < 5) iowait = 0;
    if (ret < 6) irq = 0;
    if (ret < 7) softirq = 0;
    if (ret < 8) steal = 0;
    
    /* 首次调用，保存数据并返回0 */
    if (!cpu_initialized) {
        prev_user = user;
        prev_nice = nice;
        prev_system = system;
        prev_idle = idle;
        prev_iowait = iowait;
        prev_irq = irq;
        prev_softirq = softirq;
        prev_steal = steal;
        cpu_initialized = 1;
        return 0;
    }
    
    /* 计算差值 */
    unsigned long long user_diff = user - prev_user;
    unsigned long long nice_diff = nice - prev_nice;
    unsigned long long system_diff = system - prev_system;
    unsigned long long idle_diff = idle - prev_idle;
    unsigned long long iowait_diff = iowait - prev_iowait;
    unsigned long long irq_diff = irq - prev_irq;
    unsigned long long softirq_diff = softirq - prev_softirq;
    unsigned long long steal_diff = steal - prev_steal;
    
    /* 总时间差 */
    unsigned long long total_diff = user_diff + nice_diff + system_diff + idle_diff +
                                    iowait_diff + irq_diff + softirq_diff + steal_diff;
    
    /* 保存当前值供下次使用 */
    prev_user = user;
    prev_nice = nice;
    prev_system = system;
    prev_idle = idle;
    prev_iowait = iowait;
    prev_irq = irq;
    prev_softirq = softirq;
    prev_steal = steal;
    
    /* 避免除零 */
    if (total_diff == 0) return 0;
    
    /* CPU使用率 = 100 - idle百分比 */
    /* idle时间包括 idle + iowait */
    double idle_percent = (double)(idle_diff + iowait_diff) / total_diff * 100.0;
    double usage = 100.0 - idle_percent;
    
    /* 限制范围 0-100 */
    if (usage < 0) usage = 0;
    if (usage > 100) usage = 100;
    
    return usage;
}
