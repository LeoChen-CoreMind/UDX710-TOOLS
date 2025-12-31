/**
 * @file handlers.c
 * @brief HTTP API handlers implementation (Go: handlers)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <glib.h>
#include "mongoose.h"
#include "handlers.h"
#include "dbus_core.h"
#include "sysinfo.h"
#include "exec_utils.h"
#include "airplane.h"
#include "modem.h"
#include "http_utils.h"
#include "apn.h"
#include "ofono.h"
#include "json_builder.h"


/* GET /api/info - 获取系统信息 */
void handle_info(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    SystemInfo info;
    get_system_info(&info);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "hostname", info.hostname);
    json_add_str(j, "sysname", info.sysname);
    json_add_str(j, "release", info.release);
    json_add_str(j, "version", info.version);
    json_add_str(j, "machine", info.machine);
    json_add_ulong(j, "total_ram", info.total_ram);
    json_add_ulong(j, "free_ram", info.free_ram);
    json_add_ulong(j, "cached_ram", info.cached_ram);
    json_add_double(j, "cpu_usage", info.cpu_usage);
    json_add_double(j, "uptime", info.uptime);
    json_add_str(j, "bridge_status", info.bridge_status);
    json_add_str(j, "sim_slot", info.sim_slot);
    json_add_str(j, "signal_strength", info.signal_strength);
    json_add_double(j, "thermal_temp", info.thermal_temp);
    json_add_str(j, "power_status", info.power_status);
    json_add_str(j, "battery_health", info.battery_health);
    json_add_int(j, "battery_capacity", info.battery_capacity);
    json_add_str(j, "ssid", info.ssid);
    json_add_str(j, "passwd", info.passwd);
    json_add_str(j, "select_network_mode", info.select_network_mode);
    json_add_int(j, "is_activated", info.is_activated);
    json_add_str(j, "serial", info.serial);
    json_add_str(j, "network_mode", info.network_mode);
    json_add_bool(j, "airplane_mode", info.airplane_mode);
    json_add_str(j, "imei", info.imei);
    json_add_str(j, "iccid", info.iccid);
    json_add_str(j, "imsi", info.imsi);
    json_add_str(j, "carrier", info.carrier);
    json_add_str(j, "network_type", info.network_type);
    json_add_str(j, "network_band", info.network_band);
    json_add_int(j, "qci", info.qci);
    json_add_int(j, "downlink_rate", info.downlink_rate);
    json_add_int(j, "uplink_rate", info.uplink_rate);
    json_obj_close(j);

    HTTP_OK_FREE(c, json_finish(j));
}


/* POST /api/at - 执行 AT 命令 */
void handle_execute_at(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char cmd[256] = {0};
    char *result = NULL;

    /* 使用mongoose内置JSON解析 */
    char *cmd_str = mg_json_get_str(hm->body, "$.command");
    if (cmd_str) {
        strncpy(cmd, cmd_str, sizeof(cmd) - 1);
        free(cmd_str);
    }

    if (strlen(cmd) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"命令不能为空\",\"Data\":null}");
        return;
    }

    /* 自动添加 AT 前缀 */
    if (strncasecmp(cmd, "AT", 2) != 0) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "AT%s", cmd);
        strncpy(cmd, tmp, sizeof(cmd) - 1);
    }

    printf("执行 AT 命令: %s\n", cmd);

    JsonBuilder *j = json_new();
    json_obj_open(j);

    /* 执行 AT 命令 */
    if (execute_at(cmd, &result) == 0) {
        printf("AT 命令执行成功: %s\n", result);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", result ? result : "");
        g_free(result);
    } else {
        printf("AT 命令执行失败: %s\n", dbus_get_last_error());
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", dbus_get_last_error());
        json_add_null(j, "Data");
    }

    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}


/* POST /api/set_network - 设置网络模式 */
void handle_set_network(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char mode[32] = {0};
    char slot[16] = {0};

    /* 使用mongoose内置JSON解析 */
    char *mode_str = mg_json_get_str(hm->body, "$.mode");
    char *slot_str = mg_json_get_str(hm->body, "$.slot");
    if (mode_str) { strncpy(mode, mode_str, sizeof(mode) - 1); free(mode_str); }
    if (slot_str) { strncpy(slot, slot_str, sizeof(slot) - 1); free(slot_str); }

    if (strlen(mode) == 0) {
        HTTP_ERROR(c, 400, "Mode parameter is required");
        return;
    }

    if (!is_valid_network_mode(mode)) {
        HTTP_ERROR(c, 400, "Invalid mode value");
        return;
    }

    if (strlen(slot) > 0 && !is_valid_slot(slot)) {
        HTTP_ERROR(c, 400, "Invalid slot value. Must be 'slot1' or 'slot2'");
        return;
    }

    if (set_network_mode_for_slot(mode, strlen(slot) > 0 ? slot : NULL) == 0) {
        HTTP_SUCCESS(c, "Network mode updated successfully");
    } else {
        HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to update network mode\"}");
    }
}

/* POST /api/switch - 切换 SIM 卡槽 */
void handle_switch(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char slot[16] = {0};
    char *slot_str = mg_json_get_str(hm->body, "$.slot");
    if (slot_str) { strncpy(slot, slot_str, sizeof(slot) - 1); free(slot_str); }

    if (strlen(slot) == 0) {
        HTTP_ERROR(c, 400, "Slot parameter is required");
        return;
    }

    if (!is_valid_slot(slot)) {
        HTTP_ERROR(c, 400, "Invalid slot value. Must be 'slot1' or 'slot2'");
        return;
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (switch_slot(slot) == 0) {
        json_add_str(j, "status", "success");
        char msg[64];
        snprintf(msg, sizeof(msg), "Slot switched to %s successfully", slot);
        json_add_str(j, "message", msg);
    } else {
        json_add_str(j, "status", "error");
        char msg[64];
        snprintf(msg, sizeof(msg), "Failed to switch slot to %s", slot);
        json_add_str(j, "message", msg);
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/airplane_mode - 飞行模式控制 */
void handle_airplane_mode(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    int enabled = -1;
    int val = 0;
    if (mg_json_get_bool(hm->body, "$.enabled", &val)) {
        enabled = val;
    }

    if (enabled == -1) {
        HTTP_ERROR(c, 400, "Invalid request body");
        return;
    }

    if (set_airplane_mode(enabled) == 0) {
        HTTP_SUCCESS(c, "Airplane mode updated successfully");
    } else {
        HTTP_ERROR(c, 500, "Failed to set airplane mode: AT command failed");
    }
}

/* POST /api/device_control - 设备控制 */
void handle_device_control(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char action[32] = {0};
    char *action_str = mg_json_get_str(hm->body, "$.action");
    if (action_str) { strncpy(action, action_str, sizeof(action) - 1); free(action_str); }

    if (strlen(action) == 0) {
        HTTP_ERROR(c, 400, "Action parameter is required");
        return;
    }

    if (strcmp(action, "reboot") == 0) {
        HTTP_SUCCESS(c, "Reboot command sent");
        device_reboot();
    } else if (strcmp(action, "poweroff") == 0) {
        HTTP_SUCCESS(c, "Poweroff command sent");
        device_poweroff();
    } else {
        HTTP_ERROR(c, 400, "Invalid action. Must be 'reboot' or 'poweroff'");
    }
}

/* POST /api/clear_cache - 清除缓存 */
void handle_clear_cache(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    if (clear_cache() == 0) {
        HTTP_SUCCESS(c, "Cache cleared successfully");
    } else {
        HTTP_ERROR(c, 500, "Failed to clear cache");
    }
}


/* 解析 AT 命令返回的小区数据 (Go: parseCellToVec) */
/* 返回解析后的行数，data 是二维数组 [行][列] */
int parse_cell_to_vec(const char *input, char data[64][16][32]) {
    char cleaned[4096];
    strncpy(cleaned, input, sizeof(cleaned) - 1);
    cleaned[sizeof(cleaned) - 1] = '\0';

    /* 去除 OK 和换行符 */
    char *ok_pos = strstr(cleaned, "OK");
    if (ok_pos) *ok_pos = '\0';
    
    /* 替换 \r\n 为空 */
    char *p = cleaned;
    char *dst = cleaned;
    while (*p) {
        if (*p != '\r' && *p != '\n') {
            *dst++ = *p;
        }
        p++;
    }
    *dst = '\0';

    int row = 0;
    int col = 0;
    char current_part[4096] = {0};
    int part_len = 0;
    char prev_char = 0;

    p = cleaned;
    while (*p && row < 64) {
        char c = *p;
        
        if (c == '-') {
            if (prev_char == ',') {
                /* 规则2: ,- 作为负数处理 */
                current_part[part_len++] = c;
            } else if (*(p + 1) == '-') {
                /* 规则3: -- 分割换行并保留第二个 - */
                if (part_len > 0) {
                    current_part[part_len] = '\0';
                    /* 按逗号分割 */
                    col = 0;
                    char *token = strtok(current_part, ",");
                    while (token && col < 16) {
                        while (*token == ' ') token++;
                        strncpy(data[row][col], token, 31);
                        data[row][col][31] = '\0';
                        col++;
                        token = strtok(NULL, ",");
                    }
                    row++;
                    part_len = 0;
                }
                current_part[part_len++] = '-';
                p++; /* 跳过下一个 - */
            } else {
                /* 规则1: 单独 - 换行 */
                if (part_len > 0) {
                    current_part[part_len] = '\0';
                    col = 0;
                    char *token = strtok(current_part, ",");
                    while (token && col < 16) {
                        while (*token == ' ') token++;
                        strncpy(data[row][col], token, 31);
                        data[row][col][31] = '\0';
                        col++;
                        token = strtok(NULL, ",");
                    }
                    row++;
                    part_len = 0;
                }
            }
        } else {
            current_part[part_len++] = c;
        }
        prev_char = c;
        p++;
    }

    /* 处理最后剩余部分 */
    if (part_len > 0 && row < 64) {
        current_part[part_len] = '\0';
        col = 0;
        char *token = strtok(current_part, ",");
        while (token && col < 16) {
            while (*token == ' ') token++;
            strncpy(data[row][col], token, 31);
            data[row][col][31] = '\0';
            col++;
            token = strtok(NULL, ",");
        }
        row++;
    }

    return row;
}

/**
 * 判断当前网络是否为 5G
 * 通过 D-Bus 查询 oFono NetworkMonitor 获取网络类型
 * @return 1=5G, 0=4G/其他
 */
static int is_5g_network(void) {
    char tech[32] = {0};
    int band = 0;
    
    /* 使用C语言D-Bus API获取网络信息 */
    if (ofono_get_serving_cell_info(tech, sizeof(tech), &band) != 0) {
        printf("D-Bus 查询网络类型失败，默认使用 4G\n");
        return 0;
    }

    /* 判断网络类型 - 检查是否为 "nr" */
    if (strcmp(tech, "nr") == 0) {
        return 1; /* 5G */
    }
    
    return 0; /* 4G 或其他 */
}

/* GET /api/current_band - 获取当前连接频段 */
void handle_get_current_band(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char net_type[32] = "N/A";
    char band[32] = "N/A";
    int arfcn = 0, pci = 0;
    double rsrp = 0, rsrq = 0, sinr = 0;
    char response[512];

    /* 通过 D-Bus 判断网络类型 (与 Go 版本一致) */
    int is_5g = is_5g_network();
    char *result = NULL;

    if (is_5g) {
        /* 5G 网络: AT+SPENGMD=0,14,1 */
        if (execute_at("AT+SPENGMD=0,14,1", &result) == 0 && result && strlen(result) > 100) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            
            if (rows > 15) {
                strcpy(net_type, "5G NR");
                if (strlen(data[0][0]) > 0) {
                    snprintf(band, sizeof(band), "N%s", data[0][0]);
                }
                if (strlen(data[1][0]) > 0) {
                    arfcn = atoi(data[1][0]);
                }
                if (strlen(data[2][0]) > 0) {
                    pci = atoi(data[2][0]);
                }
                if (strlen(data[3][0]) > 0) {
                    rsrp = atof(data[3][0]) / 100.0;
                }
                if (strlen(data[4][0]) > 0) {
                    rsrq = atof(data[4][0]) / 100.0;
                }
                if (strlen(data[15][0]) > 0) {
                    sinr = atof(data[15][0]) / 100.0;
                }
                printf("当前连接5G频段: Band=%s, ARFCN=%d, PCI=%d, RSRP=%.2f, RSRQ=%.2f, SINR=%.2f\n",
                       band, arfcn, pci, rsrp, rsrq, sinr);
            }
        }
        if (result) { g_free(result); result = NULL; }
    } else {
        /* 4G 网络: AT+SPENGMD=0,6,0 */
        if (execute_at("AT+SPENGMD=0,6,0", &result) == 0 && result && strlen(result) > 100) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            
            if (rows > 33) {
                strcpy(net_type, "4G LTE");
                if (strlen(data[0][0]) > 0) {
                    snprintf(band, sizeof(band), "B%s", data[0][0]);
                }
                if (strlen(data[1][0]) > 0) {
                    arfcn = atoi(data[1][0]);
                }
                if (strlen(data[2][0]) > 0) {
                    pci = atoi(data[2][0]);
                }
                if (strlen(data[3][0]) > 0) {
                    rsrp = atof(data[3][0]) / 100.0;
                }
                if (strlen(data[4][0]) > 0) {
                    rsrq = atof(data[4][0]) / 100.0;
                }
                if (strlen(data[33][0]) > 0) {
                    sinr = atof(data[33][0]) / 100.0;
                }
                printf("当前连接4G频段: Band=%s, ARFCN=%d, PCI=%d, RSRP=%.2f, RSRQ=%.2f, SINR=%.2f\n",
                       band, arfcn, pci, rsrp, rsrq, sinr);
            }
        }
        if (result) { g_free(result); result = NULL; }
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_str(j, "network_type", net_type);
    json_add_str(j, "band", band);
    json_add_int(j, "arfcn", arfcn);
    json_add_int(j, "pci", pci);
    json_add_double(j, "rsrp", rsrp);
    json_add_double(j, "rsrq", rsrq);
    json_add_double(j, "sinr", sinr);
    json_obj_close(j);
    json_obj_close(j);

    HTTP_OK_FREE(c, json_finish(j));
}


/* ==================== 短信 API ==================== */
#include "sms.h"

/* GET /api/sms - 获取短信列表 */
void handle_sms_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    SmsMessage messages[100];
    int count = sms_get_list(messages, 100);
    
    if (count < 0) {
        HTTP_ERROR(c, 500, "获取短信列表失败");
        return;
    }

    /* 使用JSON Builder构建数组 */
    JsonBuilder *j = json_new();
    json_arr_open(j, NULL);
    
    for (int i = 0; i < count; i++) {
        char time_str[32];
        struct tm *tm_info = localtime(&messages[i].timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
        
        json_arr_obj_open(j);
        json_add_int(j, "id", messages[i].id);
        json_add_str(j, "sender", messages[i].sender);
        json_add_str(j, "content", messages[i].content);
        json_add_str(j, "timestamp", time_str);
        json_add_bool(j, "read", messages[i].is_read);
        json_obj_close(j);
    }
    
    json_arr_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/sms/send - 发送短信 */
void handle_sms_send(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char recipient[64] = {0};
    char content[1024] = {0};
    
    /* 使用mongoose内置JSON解析 */
    char *r = mg_json_get_str(hm->body, "$.recipient");
    char *ct = mg_json_get_str(hm->body, "$.content");
    if (r) { strncpy(recipient, r, sizeof(recipient) - 1); free(r); }
    if (ct) { strncpy(content, ct, sizeof(content) - 1); free(ct); }

    if (strlen(recipient) == 0 || strlen(content) == 0) {
        HTTP_ERROR(c, 400, "收件人和内容不能为空");
        return;
    }

    char result_path[256] = {0};
    if (sms_send(recipient, content, result_path, sizeof(result_path)) == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "status", "success");
        json_add_str(j, "message", "短信发送成功");
        json_add_str(j, "path", result_path);
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    } else {
        HTTP_ERROR(c, 500, "短信发送失败");
    }
}

/* DELETE /api/sms/:id - 删除短信 */
void handle_sms_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    int id = 0;
    const char *uri = hm->uri.buf;
    const char *id_start = strstr(uri, "/api/sms/");
    if (id_start) {
        id_start += 9;
        id = atoi(id_start);
    }

    if (id <= 0) {
        HTTP_ERROR(c, 400, "无效的短信ID");
        return;
    }

    if (sms_delete(id) == 0) {
        HTTP_SUCCESS(c, "短信已删除");
    } else {
        HTTP_ERROR(c, 500, "删除短信失败");
    }
}

/* GET /api/sms/webhook - 获取Webhook配置 */
void handle_sms_webhook_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    WebhookConfig config;
    if (sms_get_webhook_config(&config) != 0) {
        HTTP_ERROR(c, 500, "获取配置失败");
        return;
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "enabled", config.enabled);
    json_add_str(j, "platform", config.platform);
    json_add_str(j, "url", config.url);
    json_add_str(j, "body", config.body);
    json_add_str(j, "headers", config.headers);
    json_obj_close(j);

    HTTP_OK_FREE(c, json_finish(j));
}

/* 辅助函数：使用mongoose解析JSON字符串并复制到目标缓冲区 */
static void mg_json_get_str_to_buf(struct mg_str json, const char *path, char *out, size_t out_size) {
    char *str = mg_json_get_str(json, path);
    if (str) {
        strncpy(out, str, out_size - 1);
        out[out_size - 1] = '\0';
        free(str);
    }
}

/* POST /api/sms/webhook - 保存Webhook配置 */
void handle_sms_webhook_save(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    WebhookConfig config = {0};
    
    /* 使用mongoose JSON API解析 */
    bool enabled = false;
    mg_json_get_bool(hm->body, "$.enabled", &enabled);
    config.enabled = enabled ? 1 : 0;
    
    /* 使用mongoose解析字符串字段 */
    mg_json_get_str_to_buf(hm->body, "$.platform", config.platform, sizeof(config.platform));
    mg_json_get_str_to_buf(hm->body, "$.url", config.url, sizeof(config.url));
    mg_json_get_str_to_buf(hm->body, "$.body", config.body, sizeof(config.body));
    mg_json_get_str_to_buf(hm->body, "$.headers", config.headers, sizeof(config.headers));

    if (sms_save_webhook_config(&config) == 0) {
        HTTP_SUCCESS(c, "配置已保存");
    } else {
        HTTP_ERROR(c, 500, "保存配置失败");
    }
}

/* POST /api/sms/webhook/test - 测试Webhook */
void handle_sms_webhook_test(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    if (sms_test_webhook() == 0) {
        HTTP_SUCCESS(c, "测试通知已发送");
    } else {
        HTTP_ERROR(c, 500, "Webhook未启用或URL为空");
    }
}

/* GET /api/sms/sent - 获取发送记录列表 */
void handle_sms_sent_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    SentSmsMessage messages[150];
    int count = sms_get_sent_list(messages, 150);
    
    if (count < 0) {
        HTTP_ERROR(c, 500, "获取发送记录失败");
        return;
    }

    JsonBuilder *j = json_new();
    json_arr_open(j, NULL);
    
    for (int i = 0; i < count; i++) {
        json_arr_obj_open(j);
        json_add_int(j, "id", messages[i].id);
        json_add_str(j, "recipient", messages[i].recipient);
        json_add_str(j, "content", messages[i].content);
        json_add_long(j, "timestamp", (long long)messages[i].timestamp);
        json_add_str(j, "status", messages[i].status);
        json_obj_close(j);
    }
    
    json_arr_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* GET /api/sms/config - 获取短信配置 */
void handle_sms_config_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    int max_count = sms_get_max_count();
    int max_sent_count = sms_get_max_sent_count();
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "max_count", max_count);
    json_add_int(j, "max_sent_count", max_sent_count);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/sms/config - 保存短信配置 */
void handle_sms_config_save(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    double val = 0;
    int max_count = sms_get_max_count();
    int max_sent_count = sms_get_max_sent_count();
    
    if (mg_json_get_num(hm->body, "$.max_count", &val)) {
        max_count = (int)val;
    }
    if (mg_json_get_num(hm->body, "$.max_sent_count", &val)) {
        max_sent_count = (int)val;
    }
    
    if (max_count < 10 || max_count > 150) {
        HTTP_ERROR(c, 400, "收件箱最大存储数量必须在10-150之间");
        return;
    }
    if (max_sent_count < 1 || max_sent_count > 50) {
        HTTP_ERROR(c, 400, "发件箱最大存储数量必须在1-50之间");
        return;
    }

    sms_set_max_count(max_count);
    sms_set_max_sent_count(max_sent_count);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "status", "success");
    json_add_int(j, "max_count", max_count);
    json_add_int(j, "max_sent_count", max_sent_count);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* DELETE /api/sms/sent/:id - 删除发送记录 */
void handle_sms_sent_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    int id = 0;
    const char *uri = hm->uri.buf;
    const char *id_start = strstr(uri, "/api/sms/sent/");
    if (id_start) {
        id_start += 14;
        id = atoi(id_start);
    }

    if (id <= 0) {
        HTTP_ERROR(c, 400, "无效的ID");
        return;
    }

    if (sms_delete_sent(id) == 0) {
        HTTP_OK(c, "{\"status\":\"success\"}");
    } else {
        HTTP_ERROR(c, 500, "删除失败");
    }
}

/* GET /api/sms/fix - 获取短信接收修复开关状态 */
void handle_sms_fix_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    int enabled = sms_get_fix_enabled();
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "enabled", enabled);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/sms/fix - 设置短信接收修复开关 */
void handle_sms_fix_set(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    int enabled = 0;
    int val = 0;
    if (mg_json_get_bool(hm->body, "$.enabled", &val)) {
        enabled = val;
    }
    
    if (sms_set_fix_enabled(enabled) == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "status", "success");
        json_add_bool(j, "enabled", enabled);
        json_add_str(j, "message", enabled ? "短信接收修复已开启" : "短信接收修复已关闭");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    } else {
        HTTP_ERROR(c, 500, "设置失败，AT命令执行错误");
    }
}

/* ==================== OTA更新 API ==================== */
#include "update.h"

/* GET /api/update/version - 获取当前版本 */
void handle_update_version(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "version", update_get_version());
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/update/upload - 上传更新包 */
void handle_update_upload(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    struct mg_http_part part;
    size_t ofs = 0;
    
    while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
        if (part.filename.len > 0) {
            update_cleanup();
            
            FILE *fp = fopen(UPDATE_ZIP_PATH, "wb");
            if (!fp) {
                HTTP_ERROR(c, 500, "无法创建文件");
                return;
            }
            
            fwrite(part.body.buf, 1, part.body.len, fp);
            fclose(fp);
            
            printf("更新包上传成功: %lu bytes\n", (unsigned long)part.body.len);
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "success");
            json_add_str(j, "message", "上传成功");
            json_add_ulong(j, "size", (unsigned long)part.body.len);
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
            return;
        }
    }
    
    HTTP_ERROR(c, 400, "未找到上传文件");
}

/* POST /api/update/download - 从URL下载更新包 */
void handle_update_download(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char url[512] = {0};
    char *url_str = mg_json_get_str(hm->body, "$.url");
    if (url_str) { strncpy(url, url_str, sizeof(url) - 1); free(url_str); }
    
    if (strlen(url) == 0) {
        HTTP_ERROR(c, 400, "URL参数不能为空");
        return;
    }

    if (update_download(url) == 0) {
        HTTP_SUCCESS(c, "下载成功");
    } else {
        HTTP_ERROR(c, 500, "下载失败");
    }
}

/* POST /api/update/extract - 解压更新包 */
void handle_update_extract(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    if (update_extract() == 0) {
        HTTP_SUCCESS(c, "解压成功");
    } else {
        HTTP_ERROR(c, 500, "解压失败");
    }
}

/* POST /api/update/install - 执行安装并重启 */
void handle_update_install(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char output[2048] = {0};
    
    if (update_install(output, sizeof(output)) == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "status", "success");
        json_add_str(j, "message", "安装成功，正在重启...");
        json_add_str(j, "output", output);
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        c->is_draining = 1;
        sleep(2);
        device_reboot();
    } else {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "error", "安装失败");
        json_add_str(j, "output", output);
        json_obj_close(j);
        HTTP_JSON_FREE(c, 500, json_finish(j));
    }
}

/* GET /api/update/check - 检查远程版本 */
void handle_update_check(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_ANY(c, hm);

    const char *check_url = UPDATE_CHECK_URL;

    update_info_t info;
    if (update_check_version(check_url, &info) == 0) {
        const char *current = update_get_version();
        int has_update = strcmp(info.version, current) > 0 ? 1 : 0;
        
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "current_version", current);
        json_add_str(j, "latest_version", info.version);
        json_add_bool(j, "has_update", has_update);
        json_add_str(j, "url", info.url);
        json_add_str(j, "changelog", info.changelog);
        json_add_ulong(j, "size", (unsigned long)info.size);
        json_add_bool(j, "required", info.required);
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    } else {
        HTTP_ERROR(c, 500, "检查版本失败");
    }
}

/* 已删除 /api/update/config - 版本检查URL已嵌入程序 */

/* GET /api/get/time - 获取系统时间 */
void handle_get_system_time(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char datetime[64];
    strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char date[16], time_str[16];
    strftime(date, sizeof(date), "%Y-%m-%d", tm_info);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_key_obj_open(j, "Data");
    json_add_str(j, "datetime", datetime);
    json_add_str(j, "date", date);
    json_add_str(j, "time", time_str);
    json_add_long(j, "timestamp", (long long)now);
    json_obj_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/set/time - NTP同步系统时间 */
void handle_set_system_time(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char output[512];
    
    const char *ntp_servers[] = {
        "ntp.aliyun.com",
        "pool.ntp.org",
        "time.windows.com",
        NULL
    };
    
    int success = 0;
    const char *used_server = NULL;
    
    for (int i = 0; ntp_servers[i] != NULL; i++) {
        if (run_command(output, sizeof(output), "ntpdate", ntp_servers[i], NULL) == 0) {
            success = 1;
            used_server = ntp_servers[i];
            break;
        }
    }
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (success) {
        run_command(output, sizeof(output), "hwclock", "-w", NULL);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Data", "NTP同步成功");
        json_add_str(j, "server", used_server);
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "所有NTP服务器同步失败");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* ==================== 数据连接和漫游 API ==================== */
#include "ofono.h"

/* GET/POST /api/data - 数据连接开关 */
void handle_data_status(struct mg_connection *c, struct mg_http_message *hm) {
    if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        /* GET - 查询数据连接状态 */
        int active = 0;
        if (ofono_get_data_status(&active) == 0) {
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "ok");
            json_add_str(j, "message", "Success");
            json_key_obj_open(j, "data");
            json_add_bool(j, "active", active);
            json_obj_close(j);
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to get data connection status\"}");
        }
    } else if (hm->method.len == 4 && memcmp(hm->method.buf, "POST", 4) == 0) {
        /* POST - 设置数据连接状态 */
        int active = 0;
        int val = 0;
        if (mg_json_get_bool(hm->body, "$.active", &val)) {
            active = val;
        } else {
            HTTP_ERROR(c, 400, "Invalid request body, 'active' field required");
            return;
        }

        if (ofono_set_data_status(active) == 0) {
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "ok");
            char msg[64];
            snprintf(msg, sizeof(msg), "Data connection %s successfully", active ? "enabled" : "disabled");
            json_add_str(j, "message", msg);
            json_key_obj_open(j, "data");
            json_add_bool(j, "active", active);
            json_obj_close(j);
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set data connection\"}");
        }
    } else {
        HTTP_ERROR(c, 405, "Method not allowed");
    }
}

/* GET/POST /api/roaming - 漫游开关 */
void handle_roaming_status(struct mg_connection *c, struct mg_http_message *hm) {
    if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        /* GET - 查询漫游状态 */
        int roaming_allowed = 0;
        int is_roaming = 0;
        if (ofono_get_roaming_status(&roaming_allowed, &is_roaming) == 0) {
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "ok");
            json_add_str(j, "message", "Success");
            json_key_obj_open(j, "data");
            json_add_bool(j, "roaming_allowed", roaming_allowed);
            json_add_bool(j, "is_roaming", is_roaming);
            json_obj_close(j);
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to get roaming status\"}");
        }
    } else if (hm->method.len == 4 && memcmp(hm->method.buf, "POST", 4) == 0) {
        /* POST - 设置漫游允许状态 */
        int allowed = 0;
        int val = 0;
        if (mg_json_get_bool(hm->body, "$.allowed", &val)) {
            allowed = val;
        } else {
            HTTP_ERROR(c, 400, "Invalid request body, 'allowed' field required");
            return;
        }

        if (ofono_set_roaming_allowed(allowed) == 0) {
            /* 读取当前状态确认 */
            int roaming_allowed = 0;
            int is_roaming = 0;
            ofono_get_roaming_status(&roaming_allowed, &is_roaming);
            
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "ok");
            char msg[64];
            snprintf(msg, sizeof(msg), "Roaming %s successfully", allowed ? "enabled" : "disabled");
            json_add_str(j, "message", msg);
            json_key_obj_open(j, "data");
            json_add_bool(j, "roaming_allowed", roaming_allowed);
            json_add_bool(j, "is_roaming", is_roaming);
            json_obj_close(j);
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set roaming\"}");
        }
    } else {
        HTTP_ERROR(c, 405, "Method not allowed");
    }
}


/* ==================== APN 管理 API ==================== */

// /* GET /api/apn - 获取 APN 列表 */
// void handle_apn_list(struct mg_connection *c, struct mg_http_message *hm) {
//     HTTP_CHECK_GET(c, hm);

//     ApnContext contexts[MAX_APN_CONTEXTS];
//     int count = ofono_get_all_apn_contexts(contexts, MAX_APN_CONTEXTS);

//     if (count < 0) {
//         HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to get APN list\"}");
//         return;
//     }

//     /* 构建 JSON 响应 */
//     char json[8192];
//     int offset = 0;
//     offset += snprintf(json + offset, sizeof(json) - offset, 
//         "{\"status\":\"ok\",\"message\":\"Success\",\"data\":{\"contexts\":[");

//     for (int i = 0; i < count; i++) {
//         ApnContext *ctx = &contexts[i];
//         offset += snprintf(json + offset, sizeof(json) - offset,
//             "%s{"
//             "\"path\":\"%s\","
//             "\"name\":\"%s\","
//             "\"active\":%s,"
//             "\"apn\":\"%s\","
//             "\"protocol\":\"%s\","
//             "\"username\":\"%s\","
//             "\"password\":\"%s\","
//             "\"auth_method\":\"%s\","
//             "\"context_type\":\"%s\""
//             "}",
//             i > 0 ? "," : "",
//             ctx->path, ctx->name, ctx->active ? "true" : "false",
//             ctx->apn, ctx->protocol, ctx->username, ctx->password,
//             ctx->auth_method, ctx->context_type);
//     }

//     offset += snprintf(json + offset, sizeof(json) - offset, "]}}");
//     HTTP_OK(c, json);
// }

// /* POST /api/apn - 设置 APN 配置 */
// void handle_apn_set(struct mg_connection *c, struct mg_http_message *hm) {
//     HTTP_CHECK_POST(c, hm);

//     char context_path[128] = {0};
//     char apn[128] = {0};
//     char protocol[32] = {0};
//     char username[128] = {0};
//     char password[128] = {0};
//     char auth_method[32] = {0};

//     /* 解析 JSON 请求体 */
//     char *str;
//     str = mg_json_get_str(hm->body, "$.context_path");
//     if (str) { strncpy(context_path, str, sizeof(context_path) - 1); free(str); }

//     str = mg_json_get_str(hm->body, "$.apn");
//     if (str) { strncpy(apn, str, sizeof(apn) - 1); free(str); }

//     str = mg_json_get_str(hm->body, "$.protocol");
//     if (str) { strncpy(protocol, str, sizeof(protocol) - 1); free(str); }

//     str = mg_json_get_str(hm->body, "$.username");
//     if (str) { strncpy(username, str, sizeof(username) - 1); free(str); }

//     str = mg_json_get_str(hm->body, "$.password");
//     if (str) { strncpy(password, str, sizeof(password) - 1); free(str); }

//     str = mg_json_get_str(hm->body, "$.auth_method");
//     if (str) { strncpy(auth_method, str, sizeof(auth_method) - 1); free(str); }

//     /* 验证必填字段 */
//     if (strlen(context_path) == 0) {
//         HTTP_ERROR(c, 400, "context_path is required");
//         return;
//     }

//     /* 调用设置函数 */
//     int ret = ofono_set_apn_properties(
//         context_path,
//         strlen(apn) > 0 ? apn : NULL,
//         strlen(protocol) > 0 ? protocol : NULL,
//         strlen(username) > 0 ? username : NULL,
//         strlen(password) > 0 ? password : NULL,
//         strlen(auth_method) > 0 ? auth_method : NULL
//     );

//     if (ret == 0) {
//         /* 获取更新后的配置 */
//         ApnContext contexts[MAX_APN_CONTEXTS];
//         int count = ofono_get_all_apn_contexts(contexts, MAX_APN_CONTEXTS);
        
//         /* 查找刚修改的 context */
//         ApnContext *updated = NULL;
//         for (int i = 0; i < count; i++) {
//             if (strcmp(contexts[i].path, context_path) == 0) {
//                 updated = &contexts[i];
//                 break;
//             }
//         }

//         char json[2048];
//         if (updated) {
//             snprintf(json, sizeof(json),
//                 "{\"status\":\"ok\",\"message\":\"APN configuration updated successfully\","
//                 "\"data\":{\"updated_context\":{"
//                 "\"path\":\"%s\",\"name\":\"%s\",\"active\":%s,"
//                 "\"apn\":\"%s\",\"protocol\":\"%s\",\"username\":\"%s\","
//                 "\"password\":\"%s\",\"auth_method\":\"%s\",\"context_type\":\"%s\""
//                 "}}}",
//                 updated->path, updated->name, updated->active ? "true" : "false",
//                 updated->apn, updated->protocol, updated->username,
//                 updated->password, updated->auth_method, updated->context_type);
//         } else {
//             snprintf(json, sizeof(json),
//                 "{\"status\":\"ok\",\"message\":\"APN configuration updated successfully\",\"data\":{}}");
//         }
//         HTTP_OK(c, json);
//     } else {
//         HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set APN configuration\"}");
//     }
// }

/* ==================== 插件管理 API ==================== */
#include "plugin.h"

/* POST /api/shell - 执行Shell命令 */
void handle_shell_execute(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char cmd[1024] = {0};
    char *cmd_str = mg_json_get_str(hm->body, "$.command");
    if (cmd_str) {
        strncpy(cmd, cmd_str, sizeof(cmd) - 1);
        free(cmd_str);
    }

    if (strlen(cmd) == 0) {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"命令不能为空\",\"Data\":null}");
        return;
    }

    char output[8192] = {0};

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (execute_shell(cmd, output, sizeof(output)) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", output);
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "命令执行失败");
        json_add_str(j, "Data", output);
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* GET /api/plugins - 获取插件列表 */
void handle_plugin_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char *json = malloc(512 * 1024);  /* 512KB缓冲区 */
    if (!json) {
        HTTP_ERROR(c, 500, "内存分配失败");
        return;
    }

    int count = get_plugin_list(json, 512 * 1024);
    
    /* 使用JSON Builder构建外层响应 */
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_add_raw(j, "Data", json);  /* 插件列表已是JSON数组 */
    json_add_int(j, "Count", count);
    json_obj_close(j);
    
    HTTP_OK_FREE(c, json_finish(j));
    free(json);
}

/* POST /api/plugins - 上传插件 */
void handle_plugin_upload(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char name[256] = {0};
    char *name_str = mg_json_get_str(hm->body, "$.name");
    if (name_str) {
        strncpy(name, name_str, sizeof(name) - 1);
        free(name_str);
    }

    char *content_str = mg_json_get_str(hm->body, "$.content");
    if (!content_str) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "插件内容不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }

    if (strlen(name) == 0) {
        /* 从内容中提取名称 */
        strcpy(name, "plugin");
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (save_plugin(name, content_str) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "插件上传成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "插件保存失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));

    free(content_str);
}

/* DELETE /api/plugins/:name - 删除指定插件 */
void handle_plugin_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    /* 从URI中提取插件名 */
    const char *uri = hm->uri.buf;
    const char *name_start = strstr(uri, "/api/plugins/");
    if (!name_start) {
        HTTP_ERROR(c, 400, "无效的请求路径");
        return;
    }
    name_start += 13;  /* 跳过 "/api/plugins/" */

    /* 提取名称直到URI结束或遇到? */
    char encoded_name[256] = {0};
    int i = 0;
    while (name_start[i] && name_start[i] != '?' && name_start[i] != ' ' && i < 255) {
        encoded_name[i] = name_start[i];
        i++;
    }
    encoded_name[i] = '\0';

    if (strlen(encoded_name) == 0) {
        HTTP_ERROR(c, 400, "插件名称不能为空");
        return;
    }

    /* URL解码支持中文名称 */
    char name[256] = {0};
    mg_url_decode(encoded_name, strlen(encoded_name), name, sizeof(name), 0);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (delete_plugin(name) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "插件删除成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "插件删除失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* DELETE /api/plugins/all - 删除所有插件 */
void handle_plugin_delete_all(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (delete_all_plugins() == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "所有插件已删除");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "删除失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* ==================== 脚本管理 API ==================== */

#define SCRIPTS_DIR "/home/root/6677/Plugins/scripts"

/* GET /api/scripts - 获取脚本列表 */
void handle_script_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char *json = malloc(256 * 1024);
    if (!json) {
        HTTP_ERROR(c, 500, "内存分配失败");
        return;
    }

    strcpy(json, "[");
    int first = 1;
    int count = 0;

    /* 确保目录存在 */
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", SCRIPTS_DIR);
    system(mkdir_cmd);

    DIR *dir = opendir(SCRIPTS_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG && strstr(entry->d_name, ".sh")) {
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, entry->d_name);
                
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    /* 读取脚本内容 */
                    FILE *f = fopen(filepath, "r");
                    char content[32768] = {0};
                    if (f) {
                        fread(content, 1, sizeof(content) - 1, f);
                        fclose(f);
                    }

                    /* 使用mongoose MG_ESC进行JSON转义 */
                    char escaped[65536];
                    mg_snprintf(escaped, sizeof(escaped), "%m", MG_ESC(content));

                    char item[70000];
                    snprintf(item, sizeof(item),
                        "%s{\"name\":\"%s\",\"size\":%ld,\"mtime\":%ld,\"content\":\"%s\"}",
                        first ? "" : ",", entry->d_name, st.st_size, st.st_mtime, escaped);
                    strcat(json, item);
                    first = 0;
                    count++;
                }
            }
        }
        closedir(dir);
    }

    strcat(json, "]");

    /* 使用JSON Builder构建外层响应 */
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_add_raw(j, "Data", json);
    json_add_int(j, "Count", count);
    json_obj_close(j);
    
    HTTP_OK_FREE(c, json_finish(j));
    free(json);
}

/* POST /api/scripts - 上传脚本 */
void handle_script_upload(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char name[256] = {0};
    char *name_str = mg_json_get_str(hm->body, "$.name");
    if (name_str) {
        strncpy(name, name_str, sizeof(name) - 1);
        free(name_str);
    }

    char *content_str = mg_json_get_str(hm->body, "$.content");
    if (!content_str) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本内容不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }

    if (strlen(name) == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本名称不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        free(content_str);
        return;
    }

    /* 确保目录存在 */
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", SCRIPTS_DIR);
    system(mkdir_cmd);

    /* 保存脚本 */
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, name);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    FILE *f = fopen(filepath, "w");
    if (f) {
        fputs(content_str, f);
        fclose(f);
        /* 添加执行权限 */
        char chmod_cmd[512];
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x %s", filepath);
        system(chmod_cmd);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "脚本上传成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本保存失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));

    free(content_str);
}

/* PUT /api/scripts/:name - 更新脚本 */
void handle_script_update(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_PUT(c, hm);

    /* 从URI中提取脚本名 */
    const char *uri = hm->uri.buf;
    const char *name_start = strstr(uri, "/api/scripts/");
    if (!name_start) {
        HTTP_ERROR(c, 400, "无效的请求路径");
        return;
    }
    name_start += 13;

    char name[256] = {0};
    int i = 0;
    while (name_start[i] && name_start[i] != '?' && name_start[i] != ' ' && i < 255) {
        name[i] = name_start[i];
        i++;
    }
    name[i] = '\0';

    if (strlen(name) == 0) {
        HTTP_ERROR(c, 400, "脚本名称不能为空");
        return;
    }

    char *content_str = mg_json_get_str(hm->body, "$.content");
    if (!content_str) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本内容不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, name);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    FILE *f = fopen(filepath, "w");
    if (f) {
        fputs(content_str, f);
        fclose(f);
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "脚本更新成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本更新失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));

    free(content_str);
}

/* DELETE /api/scripts/:name - 删除脚本 */
void handle_script_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    const char *uri = hm->uri.buf;
    const char *name_start = strstr(uri, "/api/scripts/");
    if (!name_start) {
        HTTP_ERROR(c, 400, "无效的请求路径");
        return;
    }
    name_start += 13;

    char encoded_name[256] = {0};
    int i = 0;
    while (name_start[i] && name_start[i] != '?' && name_start[i] != ' ' && i < 255) {
        encoded_name[i] = name_start[i];
        i++;
    }
    encoded_name[i] = '\0';

    if (strlen(encoded_name) == 0) {
        HTTP_ERROR(c, 400, "脚本名称不能为空");
        return;
    }

    /* URL解码支持中文名称 */
    char name[256] = {0};
    mg_url_decode(encoded_name, strlen(encoded_name), name, sizeof(name), 0);

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, name);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (remove(filepath) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "脚本删除成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "脚本删除失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* ==================== 插件存储 API ==================== */
#include "plugin_storage.h"

/* 从URL提取插件名 /api/plugins/storage/:name */
static int extract_plugin_name_from_url(const char *uri, char *name, size_t size) {
    const char *prefix = "/api/plugins/storage/";
    const char *start = strstr(uri, prefix);
    if (!start) return -1;
    
    start += strlen(prefix);
    char encoded[256] = {0};
    size_t i = 0;
    while (start[i] && start[i] != '?' && start[i] != ' ' && i < sizeof(encoded) - 1) {
        encoded[i] = start[i];
        i++;
    }
    encoded[i] = '\0';
    
    if (i == 0) return -1;
    
    /* URL解码支持中文名称 */
    mg_url_decode(encoded, strlen(encoded), name, size, 0);
    return (strlen(name) > 0) ? 0 : -1;
}

/* GET /api/plugins/storage/:name - 读取插件存储 */
void handle_plugin_storage_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char plugin_name[256] = {0};
    if (extract_plugin_name_from_url(hm->uri.buf, plugin_name, sizeof(plugin_name)) != 0) {
        HTTP_ERROR(c, 400, "无效的插件名称");
        return;
    }

    char storage_content[PLUGIN_STORAGE_MAX_SIZE];
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (plugin_storage_read(plugin_name, storage_content, sizeof(storage_content)) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_raw(j, "Data", storage_content);
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "读取存储失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/plugins/storage/:name - 写入插件存储 */
void handle_plugin_storage_set(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char plugin_name[256] = {0};
    if (extract_plugin_name_from_url(hm->uri.buf, plugin_name, sizeof(plugin_name)) != 0) {
        HTTP_ERROR(c, 400, "无效的插件名称");
        return;
    }

    /* 直接使用请求体作为JSON数据存储 */
    char json_data[PLUGIN_STORAGE_MAX_SIZE];
    size_t len = hm->body.len < sizeof(json_data) - 1 ? hm->body.len : sizeof(json_data) - 1;
    memcpy(json_data, hm->body.buf, len);
    json_data[len] = '\0';

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (plugin_storage_write(plugin_name, json_data) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "存储成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "存储失败，可能超出大小限制(64KB)");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* DELETE /api/plugins/storage/:name - 删除插件存储 */
void handle_plugin_storage_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    char plugin_name[256] = {0};
    if (extract_plugin_name_from_url(hm->uri.buf, plugin_name, sizeof(plugin_name)) != 0) {
        HTTP_ERROR(c, 400, "无效的插件名称");
        return;
    }

    JsonBuilder *j = json_new();
    json_obj_open(j);
    if (plugin_storage_delete(plugin_name) == 0) {
        json_add_int(j, "Code", 0);
        json_add_str(j, "Error", "");
        json_add_str(j, "Data", "删除成功");
    } else {
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "删除失败");
        json_add_null(j, "Data");
    }
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}


/* ==================== 认证 API ==================== */
#include "auth.h"

/* POST /api/auth/login - 用户登录 */
void handle_auth_login(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char password[128] = {0};
    char token[AUTH_TOKEN_SIZE] = {0};
    
    /* 解析密码 */
    char *pwd_str = mg_json_get_str(hm->body, "$.password");
    if (pwd_str) {
        strncpy(password, pwd_str, sizeof(password) - 1);
        free(pwd_str);
    }
    
    if (strlen(password) == 0) {
        HTTP_ERROR(c, 400, "密码不能为空");
        return;
    }
    
    /* 尝试登录 */
    int ret = auth_login(password, token, sizeof(token));
    
    if (ret == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_str(j, "status", "success");
        json_add_str(j, "message", "登录成功");
        json_add_str(j, "token", token);
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    } else if (ret == -1) {
        HTTP_JSON(c, 401, "{\"status\":\"error\",\"message\":\"密码错误\"}");
    } else {
        HTTP_ERROR(c, 500, "登录失败");
    }
}

/* POST /api/auth/logout - 用户登出 */
void handle_auth_logout(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    /* 从Authorization头获取token */
    struct mg_str *auth_header = mg_http_get_header(hm, "Authorization");
    char token[AUTH_TOKEN_SIZE] = {0};
    
    if (auth_header && auth_header->len > 7) {
        /* 格式: "Bearer <token>" */
        if (strncmp(auth_header->buf, "Bearer ", 7) == 0) {
            size_t token_len = auth_header->len - 7;
            if (token_len < sizeof(token)) {
                memcpy(token, auth_header->buf + 7, token_len);
                token[token_len] = '\0';
            }
        }
    }
    
    if (strlen(token) == 0) {
        HTTP_ERROR(c, 400, "未提供Token");
        return;
    }
    
    if (auth_logout(token) == 0) {
        HTTP_SUCCESS(c, "登出成功");
    } else {
        HTTP_ERROR(c, 400, "登出失败");
    }
}

/* POST /api/auth/password - 修改密码 */
void handle_auth_password(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char old_password[128] = {0};
    char new_password[128] = {0};
    
    /* 解析参数 */
    char *old_str = mg_json_get_str(hm->body, "$.old_password");
    char *new_str = mg_json_get_str(hm->body, "$.new_password");
    
    if (old_str) {
        strncpy(old_password, old_str, sizeof(old_password) - 1);
        free(old_str);
    }
    if (new_str) {
        strncpy(new_password, new_str, sizeof(new_password) - 1);
        free(new_str);
    }
    
    if (strlen(old_password) == 0 || strlen(new_password) == 0) {
        HTTP_ERROR(c, 400, "旧密码和新密码不能为空");
        return;
    }
    
    /* 修改密码 */
    int ret = auth_change_password(old_password, new_password);
    
    if (ret == 0) {
        HTTP_SUCCESS(c, "密码修改成功，请重新登录");
    } else if (ret == -1) {
        HTTP_JSON(c, 401, "{\"status\":\"error\",\"message\":\"旧密码错误\"}");
    } else {
        HTTP_ERROR(c, 500, "密码修改失败");
    }
}

/* GET /api/auth/status - 获取登录状态 */
void handle_auth_status(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    int logged_in = 0;
    int required = auth_is_required();
    
    /* 从Authorization头获取token并验证 */
    struct mg_str *auth_header = mg_http_get_header(hm, "Authorization");
    
    if (auth_header && auth_header->len > 7) {
        if (strncmp(auth_header->buf, "Bearer ", 7) == 0) {
            char token[AUTH_TOKEN_SIZE] = {0};
            size_t token_len = auth_header->len - 7;
            if (token_len < sizeof(token)) {
                memcpy(token, auth_header->buf + 7, token_len);
                token[token_len] = '\0';
                
                if (auth_verify_token(token) == 0) {
                    logged_in = 1;
                }
            }
        }
    }
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "logged_in", logged_in);
    json_add_bool(j, "auth_required", required);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* ==================== APN 配置管理 ==================== */

/* GET /api/apn/config - 获取APN配置 */
void handle_apn_config_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);
    
    ApnConfig config;
    ApnTemplateStatus tpl_status;
    
    if (apn_get_config(&config) != 0) {
        HTTP_ERROR(c, 500, "获取配置失败");
        return;
    }
    
    /* 如果是手动模式且有绑定模板，获取模板详情及应用状态 */
    int has_template = 0;
    if (config.mode == 1 && config.template_id > 0) {
        if (apn_template_get_status(config.template_id, &tpl_status) == 0) {
            has_template = 1;
        }
    }
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "status", "ok");
    json_add_str(j, "message", "");
    json_key_obj_open(j, "data");
    json_add_int(j, "mode", config.mode);
    json_add_int(j, "template_id", config.template_id);
    json_add_int(j, "auto_start", config.auto_start);
    
    if (has_template) {
        json_key_obj_open(j, "template");
        json_add_int(j, "id", tpl_status.template.id);
        json_add_str(j, "name", tpl_status.template.name);
        json_add_str(j, "apn", tpl_status.template.apn);
        json_add_str(j, "protocol", tpl_status.template.protocol);
        json_add_str(j, "username", tpl_status.template.username);
        json_add_str(j, "password", tpl_status.template.password);
        json_add_str(j, "auth_method", tpl_status.template.auth_method);
        json_add_int(j, "is_applied", tpl_status.is_applied);
        json_add_int(j, "is_active", tpl_status.is_active);
        json_add_str(j, "applied_context", tpl_status.applied_context);
        json_obj_close(j);
    } else {
        json_add_null(j, "template");
    }
    
    json_obj_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/apn/config - 设置APN配置 */
void handle_apn_config_set(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    /* 使用mg_json_get_long解析，默认值-1表示未提供 */
    long mode = mg_json_get_long(hm->body, "$.mode", -1);
    long template_id = mg_json_get_long(hm->body, "$.template_id", 0);
    long auto_start = mg_json_get_long(hm->body, "$.auto_start", 0);
    
    /* mode必须提供 */
    if (mode < 0) {
        HTTP_ERROR(c, 400, "缺少mode参数");
        return;
    }
    
    if (apn_set_mode((int)mode, (int)template_id, (int)auto_start) == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"配置保存成功\"}");
    } else {
        HTTP_ERROR(c, 400, "配置保存失败");
    }
}

/* GET /api/apn/templates - 获取模板列表 */
void handle_apn_templates_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);
    
    ApnTemplate templates[MAX_APN_TEMPLATES];
    int count = apn_template_list(templates, MAX_APN_TEMPLATES);
    
    if (count < 0) {
        HTTP_ERROR(c, 500, "获取模板列表失败");
        return;
    }
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_str(j, "status", "ok");
    json_add_str(j, "message", "");
    json_arr_open(j, "data");
    
    for (int i = 0; i < count; i++) {
        json_arr_obj_open(j);
        json_add_int(j, "id", templates[i].id);
        json_add_str(j, "name", templates[i].name);
        json_add_str(j, "apn", templates[i].apn);
        json_add_str(j, "protocol", templates[i].protocol);
        json_add_str(j, "username", templates[i].username);
        json_add_str(j, "password", templates[i].password);
        json_add_str(j, "auth_method", templates[i].auth_method);
        json_add_long(j, "created_at", (long)templates[i].created_at);
        json_obj_close(j);
    }
    
    json_arr_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/apn/templates - 创建模板 */
void handle_apn_templates_create(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    ApnTemplate tpl = {0};
    
    /* 解析JSON参数 */
    char *name = mg_json_get_str(hm->body, "$.name");
    char *apn = mg_json_get_str(hm->body, "$.apn");
    char *protocol = mg_json_get_str(hm->body, "$.protocol");
    char *username = mg_json_get_str(hm->body, "$.username");
    char *password = mg_json_get_str(hm->body, "$.password");
    char *auth_method = mg_json_get_str(hm->body, "$.auth_method");
    
    if (name) { strncpy(tpl.name, name, sizeof(tpl.name) - 1); free(name); }
    if (apn) { strncpy(tpl.apn, apn, sizeof(tpl.apn) - 1); free(apn); }
    if (protocol) { strncpy(tpl.protocol, protocol, sizeof(tpl.protocol) - 1); free(protocol); }
    else { strcpy(tpl.protocol, "dual"); }
    if (username) { strncpy(tpl.username, username, sizeof(tpl.username) - 1); free(username); }
    if (password) { strncpy(tpl.password, password, sizeof(tpl.password) - 1); free(password); }
    if (auth_method) { strncpy(tpl.auth_method, auth_method, sizeof(tpl.auth_method) - 1); free(auth_method); }
    else { strcpy(tpl.auth_method, "chap"); }
    
    if (apn_template_create(tpl.name, tpl.apn, tpl.protocol, tpl.username, tpl.password, tpl.auth_method) == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"模板创建成功\"}");
    } else {
        HTTP_ERROR(c, 400, "模板创建失败");
    }
}

/* PUT /api/apn/templates/:id - 更新模板 */
void handle_apn_templates_update(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_ANY(c, hm);
    HTTP_HANDLE_OPTIONS(c, hm);
    
    if (!http_is_method(hm, "PUT")) {
        http_method_error(c);
        return;
    }
    
    /* 从URL提取ID */
    char id_str[16] = {0};
    if (sscanf(hm->uri.buf, "/api/apn/templates/%15s", id_str) != 1) {
        HTTP_ERROR(c, 400, "无效的模板ID");
        return;
    }
    
    ApnTemplate tpl = {0};
    tpl.id = atoi(id_str);
    
    /* 解析JSON参数 */
    char *name = mg_json_get_str(hm->body, "$.name");
    char *apn = mg_json_get_str(hm->body, "$.apn");
    char *protocol = mg_json_get_str(hm->body, "$.protocol");
    char *username = mg_json_get_str(hm->body, "$.username");
    char *password = mg_json_get_str(hm->body, "$.password");
    char *auth_method = mg_json_get_str(hm->body, "$.auth_method");
    
    if (name) { strncpy(tpl.name, name, sizeof(tpl.name) - 1); free(name); }
    if (apn) { strncpy(tpl.apn, apn, sizeof(tpl.apn) - 1); free(apn); }
    if (protocol) { strncpy(tpl.protocol, protocol, sizeof(tpl.protocol) - 1); free(protocol); }
    else { strcpy(tpl.protocol, "dual"); }
    if (username) { strncpy(tpl.username, username, sizeof(tpl.username) - 1); free(username); }
    if (password) { strncpy(tpl.password, password, sizeof(tpl.password) - 1); free(password); }
    if (auth_method) { strncpy(tpl.auth_method, auth_method, sizeof(tpl.auth_method) - 1); free(auth_method); }
    else { strcpy(tpl.auth_method, "chap"); }
    
    if (apn_template_update(tpl.id, tpl.name, tpl.apn, tpl.protocol, tpl.username, tpl.password, tpl.auth_method) == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"模板更新成功\"}");
    } else {
        HTTP_ERROR(c, 400, "模板更新失败");
    }
}

/* DELETE /api/apn/templates/:id - 删除模板 */
void handle_apn_templates_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);
    
    /* 从URL提取ID */
    char id_str[16] = {0};
    if (sscanf(hm->uri.buf, "/api/apn/templates/%15s", id_str) != 1) {
        HTTP_ERROR(c, 400, "无效的模板ID");
        return;
    }
    
    int id = atoi(id_str);
    
    if (apn_template_delete(id) == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"模板删除成功\"}");
    } else {
        HTTP_ERROR(c, 400, "模板删除失败");
    }
}

/* POST /api/apn/apply - 应用模板 */
void handle_apn_apply(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    /* 使用mg_json_get_long，默认值为-1表示未找到 */
    long template_id = mg_json_get_long(hm->body, "$.template_id", -1);
    
    if (template_id <= 0) {
        HTTP_ERROR(c, 400, "缺少或无效的template_id参数");
        return;
    }
    
    if (apn_apply_template((int)template_id) == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"模板应用成功\"}");
    } else {
        HTTP_ERROR(c, 400, "模板应用失败");
    }
}

/* POST /api/apn/clear - 清除APN配置 */
void handle_apn_clear(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    if (apn_clear_all() == 0) {
        HTTP_OK(c, "{\"status\":\"ok\",\"message\":\"APN配置已清除\"}");
    } else {
        HTTP_ERROR(c, 500, "清除APN配置失败");
    }
}
