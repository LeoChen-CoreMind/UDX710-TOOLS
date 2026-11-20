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
    int reset_enabled;
    int reset_day;
    long long rx_offset;
    long long tx_offset;
    int last_reset_period;
} TrafficConfig;

/* 读取流量配置 - 从SQLite数据库读取 */
static TrafficConfig read_traffic_config(void) {
    TrafficConfig config;
    config.much = config_get_ll("traffic_much", 0);
    config.switch_on = config_get_int("traffic_switch", 0);
    config.reset_enabled = config_get_int("traffic_reset_enabled", 0);
    config.reset_day = config_get_int("traffic_reset_day", 1);
    config.rx_offset = config_get_ll("traffic_rx_offset", 0);
    config.tx_offset = config_get_ll("traffic_tx_offset", 0);
    config.last_reset_period = config_get_int("traffic_last_reset_period", 0);
    return config;
}

/* 保存流量配置 - 写入SQLite数据库 */
static void save_traffic_config(TrafficConfig *config) {
    config_set_int("traffic_switch", config->switch_on);
    config_set_ll("traffic_much", config->much);
    config_set_int("traffic_reset_enabled", config->reset_enabled);
    config_set_int("traffic_reset_day", config->reset_day);
    config_set_ll("traffic_rx_offset", config->rx_offset);
    config_set_ll("traffic_tx_offset", config->tx_offset);
    config_set_int("traffic_last_reset_period", config->last_reset_period);
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


#include <time.h>

/* Get unique ID for the current billing period */
static int get_billing_period(int reset_day) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return 0;

    int year = t->tm_year + 1900;
    int month = t->tm_mon + 1; /* 1-12 */
    int day = t->tm_mday;

    /* If today is before the reset day, it belongs to the previous month's period */
    if (day < reset_day) {
        month--;
        if (month == 0) {
            month = 12;
            year--;
        }
    }

    /* Return unique period ID (e.g., 202310) */
    return year * 100 + month;
}

/* Check if auto-reset should be applied based on current date and period */
static void check_and_apply_auto_reset(TrafficConfig *config) {
    if (!config->reset_enabled) return;

    int current_period = get_billing_period(config->reset_day);

    /* If period changed, apply reset */
    if (config->last_reset_period != 0 && current_period > config->last_reset_period) {
        long long current_rx, current_tx;

        char output[4096];
        current_rx = 0;
        current_tx = 0;

        if (run_command(output, sizeof(output), "/home/root/6677/vnstat",
                        "-i", NETWORK_IFACE, "--json", NULL) == 0) {
            struct mg_str json = mg_str(output);
            current_rx = mg_json_get_long(json, "$.interfaces[0].traffic.total.rx", 0);
            current_tx = mg_json_get_long(json, "$.interfaces[0].traffic.total.tx", 0);
        }

        config->rx_offset = current_rx;
        config->tx_offset = current_tx;
        config->last_reset_period = current_period;

        save_traffic_config(config);
    } else if (config->last_reset_period == 0) {
        /* Initialize last_reset_period if it was 0 */
        config->last_reset_period = current_period;
        save_traffic_config(config);
    }
}

/* 流量控制线程 */
static void *flow_control_thread_func(void *arg) {
    (void)arg;
    while (1) {
        TrafficConfig config = read_traffic_config();

        check_and_apply_auto_reset(&config);

        if (config.switch_on == 0) {
            /* 关闭流量控制时，关闭飞行模式恢复网络 */
            set_airplane_mode(0);
            is_flow_control_running = 0;
            break;
        }

        long long rx, tx;
        get_traffic_from_vnstat(&rx, &tx);

        long long usage_rx = rx;
        long long usage_tx = tx;

        if (config.reset_enabled) {
            usage_rx = rx >= config.rx_offset ? rx - config.rx_offset : rx;
            usage_tx = tx >= config.tx_offset ? tx - config.tx_offset : tx;
        }

        long long total = usage_rx + usage_tx;

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
    check_and_apply_auto_reset(&config);

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

    TrafficConfig config = read_traffic_config();
    check_and_apply_auto_reset(&config);

    long long cum_rx = rx;
    long long cum_tx = tx;
    long long cum_total = rx + tx;

    long long usage_rx = rx;
    long long usage_tx = tx;

    if (config.reset_enabled) {
        usage_rx = rx >= config.rx_offset ? rx - config.rx_offset : rx;
        usage_tx = tx >= config.tx_offset ? tx - config.tx_offset : tx;
    }

    long long usage_total = usage_rx + usage_tx;

    char rx_str[32], tx_str[32], total_str[32];
    char cum_rx_str[32], cum_tx_str[32], cum_total_str[32];

    format_bytes(usage_rx, rx_str, sizeof(rx_str));
    format_bytes(usage_tx, tx_str, sizeof(tx_str));
    format_bytes(usage_total, total_str, sizeof(total_str));

    format_bytes(cum_rx, cum_rx_str, sizeof(cum_rx_str));
    format_bytes(cum_tx, cum_tx_str, sizeof(cum_tx_str));
    format_bytes(cum_total, cum_total_str, sizeof(cum_total_str));

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "rx", rx_str);
    json_add_str(j, "tx", tx_str);
    json_add_str(j, "total", total_str);
    json_add_str(j, "cum_rx", cum_rx_str);
    json_add_str(j, "cum_tx", cum_tx_str);
    json_add_str(j, "cum_total", cum_total_str);
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
    json_add_int(j, "reset_enabled", config.reset_enabled);
    json_add_int(j, "reset_day", config.reset_day);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/set/total - 设置流量限制 */
void handle_set_traffic_limit(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    /* 使用mongoose JSON解析 */
    long switch_val = mg_json_get_long(hm->body, "$.switch", -1);
    long long much_val = mg_json_get_long(hm->body, "$.much", -1);

    long reset_enabled_val = mg_json_get_long(hm->body, "$.reset_enabled", -1);
    long reset_day_val = mg_json_get_long(hm->body, "$.reset_day", -1);

    /* 如果没有参数，清除统计 */
    if (switch_val < 0 || much_val < 0) {
        char output[256];
        run_command(output, sizeof(output), "rm", "-f", VNSTAT_DB, NULL);
        init_vnstat_db();
        
        TrafficConfig config = read_traffic_config();
        config.rx_offset = 0;
        config.tx_offset = 0;
        config.last_reset_period = 0;
        save_traffic_config(&config);

        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_bool(j, "success", 1);
        json_add_str(j, "msg", "Clean ok");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }

    TrafficConfig config = read_traffic_config();
    config.switch_on = (int)switch_val;
    config.much = much_val;

    if (reset_enabled_val >= 0) {
        config.reset_enabled = (int)reset_enabled_val;
    }

    if (reset_day_val >= 1 && reset_day_val <= 31) {
        config.reset_day = (int)reset_day_val;
    }

    save_traffic_config(&config);
    // recalculate if we enabled reset
    check_and_apply_auto_reset(&config);

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
