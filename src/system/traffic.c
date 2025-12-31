/**
 * @file traffic.c
 * @brief 流量控制实现 (Go: handlers/traffic.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <glib.h>
#include "mongoose.h"
#include "traffic.h"
#include "exec_utils.h"
#include "database.h"  /* 使用数据库配置函数 */
#include "airplane.h"  /* 飞行模式控制 */
#include "http_utils.h"
#include "json_builder.h"

#define VNSTAT_DB "/var/lib/vnstat/vnstat.db"
#define NETWORK_IFACE "sipa_eth0"

static int is_flow_control_running = 0;
static pthread_t flow_control_thread;

/* 流量配置 */
typedef struct {
    long long much;
    int switch_on;
} TrafficConfig;

/* 读取流量配置 - 从SQLite数据库读取 */
static TrafficConfig read_traffic_config(void) {
    TrafficConfig config;
    config.much = config_get_ll("traffic_much", 0);
    config.switch_on = config_get_int("traffic_switch", 0);
    return config;
}

/* 保存流量配置 - 写入SQLite数据库 */
static void save_traffic_config(TrafficConfig *config) {
    config_set_int("traffic_switch", config->switch_on);
    config_set_ll("traffic_much", config->much);
}


/* 从 vnstat 获取流量数据 */
static void get_traffic_from_vnstat(long long *rx, long long *tx) {
    char output[4096];
    *rx = 0;
    *tx = 0;

    if (run_command(output, sizeof(output), "/home/root/6677/vnstat", 
                    "-i", NETWORK_IFACE, "--json", NULL) != 0) {
        return;
    }

    /* 使用mongoose JSON API解析 */
    struct mg_str json = mg_str(output);
    *rx = mg_json_get_long(json, "$.interfaces[0].traffic.total.rx", 0);
    *tx = mg_json_get_long(json, "$.interfaces[0].traffic.total.tx", 0);
}

/* 格式化字节数 */
static void format_bytes(long long bytes, char *buf, size_t size) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int idx = 0;
    double value = (double)bytes;
    while (value >= 1024.0 && idx < 4) {
        value /= 1024.0;
        idx++;
    }
    snprintf(buf, size, "%.3f %s", value, units[idx]);
}

/* 流量控制线程 */
static void *flow_control_thread_func(void *arg) {
    (void)arg;
    while (1) {
        TrafficConfig config = read_traffic_config();
        if (config.switch_on == 0) {
            /* 关闭流量控制时，关闭飞行模式恢复网络 */
            set_airplane_mode(0);
            is_flow_control_running = 0;
            break;
        }

        long long rx, tx;
        get_traffic_from_vnstat(&rx, &tx);
        long long total = rx + tx;

        if (total >= config.much) {
            set_airplane_mode(1);  /* 流量超限，开启飞行模式 */
        } else {
            set_airplane_mode(0);  /* 流量正常，关闭飞行模式 */
        }
        sleep(15);
    }
    return NULL;
}

/* 初始化 vnstat 数据库 */
static void init_vnstat_db(void) {
    struct stat st;
    char output[256];
    if (stat(VNSTAT_DB, &st) != 0) {
        run_command(output, sizeof(output), "/home/root/6677/vnstatd", "--initdb", NULL);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/home/root/6677/vnstat --add -i %s", NETWORK_IFACE);
        run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
    }
    run_command(output, sizeof(output), "/home/root/6677/vnstatd", "--noadd", "--config", "/home/root/6677/vnstatd.conf", "-d", NULL);
}

/* 初始化流量统计 */
void init_traffic(void) {
    init_vnstat_db();

    /* 启动流量控制 */
    TrafficConfig config = read_traffic_config();
    if (config.switch_on && !is_flow_control_running) {
        is_flow_control_running = 1;
        pthread_create(&flow_control_thread, NULL, flow_control_thread_func, NULL);
        pthread_detach(flow_control_thread);
    }
    printf("流量统计已初始化\n");
}


/* GET /api/get/Total - 获取流量统计 */
void handle_get_traffic_total(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    long long rx, tx;
    get_traffic_from_vnstat(&rx, &tx);

    char rx_str[32], tx_str[32], total_str[32];
    format_bytes(rx, rx_str, sizeof(rx_str));
    format_bytes(tx, tx_str, sizeof(tx_str));
    format_bytes(rx + tx, total_str, sizeof(total_str));

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "rx", rx_str);
    json_add_str(j, "tx", tx_str);
    json_add_str(j, "total", total_str);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* GET /api/get/set - 获取流量配置 */
void handle_get_traffic_config(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    TrafficConfig config = read_traffic_config();
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_long(j, "much", config.much);
    json_add_int(j, "switch", config.switch_on);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/set/total - 设置流量限制 */
void handle_set_traffic_limit(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    /* 使用mongoose JSON解析 */
    long switch_val = mg_json_get_long(hm->body, "$.switch", -1);
    long long much_val = mg_json_get_long(hm->body, "$.much", -1);

    /* 如果没有参数，清除统计 */
    if (switch_val < 0 || much_val < 0) {
        char output[256];
        run_command(output, sizeof(output), "rm", "-f", VNSTAT_DB, NULL);
        init_vnstat_db();
        
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_bool(j, "success", 1);
        json_add_str(j, "msg", "Clean ok");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }

    TrafficConfig config;
    config.switch_on = (int)switch_val;
    config.much = much_val;
    save_traffic_config(&config);

    if (config.switch_on == 0) {
        /* 关闭流量控制时，立即关闭飞行模式恢复网络 */
        set_airplane_mode(0);
    } else if (!is_flow_control_running) {
        is_flow_control_running = 1;
        pthread_create(&flow_control_thread, NULL, flow_control_thread_func, NULL);
        pthread_detach(flow_control_thread);
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "success", 1);
    json_add_str(j, "msg", "added ok");
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
} 
