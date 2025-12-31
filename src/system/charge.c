/**
 * @file charge.c
 * @brief 充电控制实现 (Go: handlers/charge.go, system/battery.go)
 * 
 * 使用 GIOChannel 回调监听电池 uevent，无需线程和循环
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <time.h>
#include <glib.h>
#include "mongoose.h"
#include "charge.h"
#include "database.h"  /* 使用数据库配置函数 */
#include "http_utils.h"
#include "json_builder.h"

#define BATTERY_UEVENT "/sys/class/power_supply/battery/uevent"
#define BATTERY_STOP_CHARGE "/sys/class/power_supply/battery/charger.0/stop_charge"
#define UEVENT_BUFFER_SIZE 2048

/* 充电配置 */
typedef struct {
    int enabled;
    int start_threshold;
    int stop_threshold;
} ChargeConfig;

/* 电池信息 */
typedef struct {
    char status[32];
    char health[32];
    int capacity;
    int temperature;
    int voltage_now;
    int current_now;
} BatteryInfo;

static ChargeConfig charge_config = {0, 20, 80};
static pthread_mutex_t charge_mutex = PTHREAD_MUTEX_INITIALIZER;
static int uevent_socket_fd = -1;
static GIOChannel *uevent_channel = NULL;
static guint uevent_watch_id = 0;

/* 电池状态变化回调 */
static battery_change_callback_t battery_callback = NULL;



/* 读取电池信息 */
static void get_battery_info(BatteryInfo *info) {
    memset(info, 0, sizeof(BatteryInfo));
    strcpy(info->status, "Unknown");
    strcpy(info->health, "Unknown");

    FILE *f = fopen(BATTERY_UEVENT, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        /* 去除换行 */
        size_t len = strlen(value);
        if (len > 0 && value[len - 1] == '\n') value[len - 1] = '\0';

        if (strcmp(key, "POWER_SUPPLY_STATUS") == 0) {
            strncpy(info->status, value, sizeof(info->status) - 1);
        } else if (strcmp(key, "POWER_SUPPLY_HEALTH") == 0) {
            strncpy(info->health, value, sizeof(info->health) - 1);
        } else if (strcmp(key, "POWER_SUPPLY_CAPACITY") == 0) {
            info->capacity = atoi(value);
        } else if (strcmp(key, "POWER_SUPPLY_TEMP") == 0) {
            info->temperature = atoi(value);
        } else if (strcmp(key, "POWER_SUPPLY_VOLTAGE_NOW") == 0) {
            info->voltage_now = atoi(value);
        } else if (strcmp(key, "POWER_SUPPLY_CURRENT_NOW") == 0) {
            info->current_now = atoi(value);
        }
    }
    fclose(f);
}


/* 设置充电开关 */
static int set_charging(int enable) {
    FILE *f = fopen(BATTERY_STOP_CHARGE, "w");
    if (!f) return -1;
    /* enable=1 开启充电 -> 写入 0; enable=0 停止充电 -> 写入 1 */
    fprintf(f, "%d", enable ? 0 : 1);
    fclose(f);
    return 0;
}

/* 加载充电配置 - 从SQLite数据库读取 */
static void load_charge_config(void) {
    charge_config.enabled = config_get_int("charge_enabled", 0);
    charge_config.start_threshold = config_get_int("charge_start_threshold", 20);
    charge_config.stop_threshold = config_get_int("charge_stop_threshold", 80);
}

/* 保存充电配置 - 写入SQLite数据库 */
static void save_charge_config(void) {
    config_set_int("charge_enabled", charge_config.enabled);
    config_set_int("charge_start_threshold", charge_config.start_threshold);
    config_set_int("charge_stop_threshold", charge_config.stop_threshold);
}

/* 检查并控制充电 */
static void check_and_control_charging(void) {
    pthread_mutex_lock(&charge_mutex);
    if (!charge_config.enabled) {
        pthread_mutex_unlock(&charge_mutex);
        return;
    }
    int start = charge_config.start_threshold;
    int stop = charge_config.stop_threshold;
    pthread_mutex_unlock(&charge_mutex);

    BatteryInfo info;
    get_battery_info(&info);
    int is_charging = (strcmp(info.status, "Charging") == 0);

    printf("[charge] 电池状态: 电量=%d%%, 充电=%s\n", info.capacity, is_charging ? "是" : "否");

    /* 高于停止阈值且正在充电 -> 停止充电 */
    if (info.capacity >= stop && is_charging) {
        printf("[charge] 电量(%d%%)>=停止阈值(%d%%)，停止充电\n", info.capacity, stop);
        set_charging(0);
    }
    /* 低于启动阈值且未充电 -> 开始充电 */
    else if (info.capacity <= start && !is_charging) {
        printf("[charge] 电量(%d%%)<=启动阈值(%d%%)，开始充电\n", info.capacity, start);
        set_charging(1);
    }
}

/* 创建 Netlink uevent socket */
static int create_uevent_socket(void) {
    int fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (fd < 0) {
        perror("[charge] 创建 Netlink socket 失败");
        return -1;
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 1;  /* 监听内核广播组 */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[charge] 绑定 Netlink socket 失败");
        close(fd);
        return -1;
    }

    return fd;
}

/* 检查是否为电池相关 uevent */
static gboolean is_battery_event(const char *buf, int len) {
    const char *p = buf;
    const char *end = buf + len;
    gboolean is_power_supply = FALSE;
    gboolean is_battery = FALSE;
    
    while (p < end) {
        if (strstr(p, "SUBSYSTEM=power_supply")) {
            is_power_supply = TRUE;
        }
        if (strstr(p, "POWER_SUPPLY_NAME=battery") || 
            (strstr(p, "DEVPATH=") && strstr(p, "/battery"))) {
            is_battery = TRUE;
        }
        p += strlen(p) + 1;
    }
    
    return is_power_supply && is_battery;
}

/**
 * @brief uevent 回调函数 - 由 GMainContext 自动调用
 * 
 * 无需线程，无需循环，事件驱动
 */
static gboolean on_uevent_callback(GIOChannel *source, 
                                    GIOCondition condition, 
                                    gpointer data) {
    (void)source;
    (void)data;
    
    if (condition & G_IO_IN) {
        char buf[UEVENT_BUFFER_SIZE];
        int len = recv(uevent_socket_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        
        if (len > 0) {
            buf[len] = '\0';
            
            if (is_battery_event(buf, len)) {
                printf("[charge] 收到电池状态变化事件\n");
                check_and_control_charging();
                
                /* 通知回调 */
                if (battery_callback) {
                    BatteryInfo info;
                    get_battery_info(&info);
                    int is_charging = (strcmp(info.status, "Charging") == 0);
                    battery_callback(info.capacity, is_charging);
                }
            }
        }
    }
    
    if (condition & (G_IO_ERR | G_IO_HUP)) {
        printf("[charge] uevent channel 异常\n");
        return FALSE;  /* 移除 watch */
    }
    
    return TRUE;  /* 继续监听 */
}

/* 启动充电监控 - 使用 GIOChannel 回调 */
static void start_charge_monitor(void) {
    if (uevent_watch_id > 0) return;  /* 已启动 */
    
    uevent_socket_fd = create_uevent_socket();
    if (uevent_socket_fd < 0) {
        printf("[charge] 无法创建 uevent socket\n");
        return;
    }
    
    /* 创建 GIOChannel 包装 socket */
    uevent_channel = g_io_channel_unix_new(uevent_socket_fd);
    if (!uevent_channel) {
        close(uevent_socket_fd);
        uevent_socket_fd = -1;
        return;
    }
    
    g_io_channel_set_encoding(uevent_channel, NULL, NULL);
    g_io_channel_set_buffered(uevent_channel, FALSE);
    
    /* 注册回调到默认 GMainContext */
    uevent_watch_id = g_io_add_watch(uevent_channel, 
                                      G_IO_IN | G_IO_ERR | G_IO_HUP,
                                      on_uevent_callback, 
                                      NULL);
    
    if (uevent_watch_id == 0) {
        g_io_channel_unref(uevent_channel);
        close(uevent_socket_fd);
        uevent_channel = NULL;
        uevent_socket_fd = -1;
        return;
    }
    
    /* 启动时检查一次 */
    check_and_control_charging();
    
    printf("[charge] GIOChannel uevent 回调已启动\n");
}

/* 停止充电监控 */
static void stop_charge_monitor(void) {
    if (uevent_watch_id > 0) {
        g_source_remove(uevent_watch_id);
        uevent_watch_id = 0;
    }
    
    if (uevent_channel) {
        g_io_channel_unref(uevent_channel);
        uevent_channel = NULL;
    }
    
    if (uevent_socket_fd >= 0) {
        close(uevent_socket_fd);
        uevent_socket_fd = -1;
    }
    
    printf("[charge] uevent 监听已停止\n");
}

/* 初始化充电控制 */
void init_charge(void) {
    load_charge_config();

    if (charge_config.enabled) {
        start_charge_monitor();
        printf("智能充电控制已启用，开始阈值: %d%%，停止阈值: %d%%\n",
               charge_config.start_threshold, charge_config.stop_threshold);
    } else {
        printf("智能充电控制未启用\n");
    }
}


/* GET/POST /api/charge/config - 获取/设置充电配置 */
void handle_charge_config(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_ANY(c, hm);

    if (http_is_method(hm, "GET")) {
        /* GET - 获取配置和电池状态 */
        BatteryInfo battery;
        get_battery_info(&battery);

        pthread_mutex_lock(&charge_mutex);
        ChargeConfig cfg = charge_config;
        pthread_mutex_unlock(&charge_mutex);

        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_key_obj_open(j, "Data");
        
        /* config对象 */
        json_key_obj_open(j, "config");
        json_add_bool(j, "enabled", cfg.enabled);
        json_add_int(j, "startThreshold", cfg.start_threshold);
        json_add_int(j, "stopThreshold", cfg.stop_threshold);
        json_obj_close(j);
        
        /* battery对象 */
        json_key_obj_open(j, "battery");
        json_add_int(j, "capacity", battery.capacity);
        json_add_bool(j, "charging", strcmp(battery.status, "Charging") == 0);
        json_add_str(j, "status", battery.status);
        json_add_str(j, "health", battery.health);
        json_add_double(j, "temperature", (double)battery.temperature / 10.0);
        json_add_double(j, "voltage", (double)battery.voltage_now / 1000000.0);
        json_add_double(j, "current", (double)battery.current_now / 1000000.0);
        json_obj_close(j);
        
        json_obj_close(j);
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    } else if (http_is_method(hm, "POST")) {
        /* POST - 设置配置 */
        int enabled = 0, start = 20, stop = 80;
        double val = 0;
        int bval = 0;

        /* 使用mongoose内置JSON解析 */
        if (mg_json_get_bool(hm->body, "$.enabled", &bval)) enabled = bval;
        if (mg_json_get_num(hm->body, "$.startThreshold", &val)) start = (int)val;
        if (mg_json_get_num(hm->body, "$.stopThreshold", &val)) stop = (int)val;

        /* 验证阈值 */
        if (enabled && (start < 0 || start > 100 || stop < 0 || stop > 100 || start >= stop)) {
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_int(j, "Code", 1);
            json_add_str(j, "Error", "无效的阈值设置");
            json_add_null(j, "Data");
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
            return;
        }

        /* 更新配置 */
        pthread_mutex_lock(&charge_mutex);
        int was_enabled = charge_config.enabled;
        charge_config.enabled = enabled;
        charge_config.start_threshold = start;
        charge_config.stop_threshold = stop;
        pthread_mutex_unlock(&charge_mutex);

        save_charge_config();

        if (enabled) {
            start_charge_monitor();
        } else if (was_enabled) {
            stop_charge_monitor();
        }

        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "充电配置已更新");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    }
}

/* POST /api/charge/on - 手动开启充电 */
void handle_charge_on(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    
    if (set_charging(1) != 0) {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "开启充电失败");
        json_add_null(j, "Data");
    } else {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "已开启充电");
    }
    
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/charge/off - 手动停止充电 */
void handle_charge_off(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    
    if (set_charging(0) != 0) {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "停止充电失败");
        json_add_null(j, "Data");
    } else {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "已停止充电");
    }
    
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}


/*============================================================================
 * 公共接口 - 供 LED 模块使用
 *============================================================================*/

/* 获取电池状态 */
void charge_get_battery_status(int *capacity, int *charging) {
    BatteryInfo info;
    get_battery_info(&info);
    
    if (capacity) *capacity = info.capacity;
    if (charging) *charging = (strcmp(info.status, "Charging") == 0) ? 1 : 0;
}

/* 注册电池状态变化回调 */
void charge_register_callback(battery_change_callback_t callback) {
    battery_callback = callback;
    
    /* 立即调用一次回调，通知当前状态 */
    if (callback) {
        BatteryInfo info;
        get_battery_info(&info);
        int is_charging = (strcmp(info.status, "Charging") == 0);
        callback(info.capacity, is_charging);
    }
}
