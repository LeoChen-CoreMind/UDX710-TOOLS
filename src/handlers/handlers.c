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


/* GET /api/info - 获取系统信息 */
void handle_info(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    SystemInfo info;
    char json[4096];

    get_system_info(&info);

    snprintf(json, sizeof(json),
        "{"
        "\"hostname\":\"%s\","
        "\"sysname\":\"%s\","
        "\"release\":\"%s\","
        "\"version\":\"%s\","
        "\"machine\":\"%s\","
        "\"total_ram\":%lu,"
        "\"free_ram\":%lu,"
        "\"cached_ram\":%lu,"
        "\"cpu_usage\":%.2f,"
        "\"uptime\":%.2f,"
        "\"bridge_status\":\"%s\","
        "\"sim_slot\":\"%s\","
        "\"signal_strength\":\"%s\","
        "\"thermal_temp\":%.2f,"
        "\"power_status\":\"%s\","
        "\"battery_health\":\"%s\","
        "\"battery_capacity\":%u,"
        "\"ssid\":\"%s\","
        "\"passwd\":\"%s\","
        "\"select_network_mode\":\"%s\","
        "\"is_activated\":%d,"
        "\"serial\":\"%s\","
        "\"network_mode\":\"%s\","
        "\"airplane_mode\":%s,"
        "\"imei\":\"%s\","
        "\"iccid\":\"%s\","
        "\"imsi\":\"%s\","
        "\"carrier\":\"%s\","
        "\"network_type\":\"%s\","
        "\"network_band\":\"%s\","
        "\"qci\":%d,"
        "\"downlink_rate\":%d,"
        "\"uplink_rate\":%d"
        "}",
        info.hostname, info.sysname, info.release, info.version, info.machine,
        info.total_ram, info.free_ram, info.cached_ram, info.cpu_usage, info.uptime,
        info.bridge_status, info.sim_slot, info.signal_strength, info.thermal_temp,
        info.power_status, info.battery_health, info.battery_capacity,
        info.ssid, info.passwd, info.select_network_mode, info.is_activated,
        info.serial, info.network_mode, info.airplane_mode ? "true" : "false",
        info.imei, info.iccid, info.imsi, info.carrier,
        info.network_type, info.network_band, info.qci, info.downlink_rate, info.uplink_rate
    );

    HTTP_OK(c, json);
}

/* JSON 字符串转义 - 处理特殊字符 */
static void json_escape_string(const char *src, char *dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 2; i++) {
        char c = src[i];
        switch (c) {
            case '"':  if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '"'; } break;
            case '\\': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '\\'; } break;
            case '\n': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default:
                if ((unsigned char)c >= 0x20) {
                    dst[j++] = c;
                }
                break;
        }
    }
    dst[j] = '\0';
}

/* POST /api/at - 执行 AT 命令 */
void handle_execute_at(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char cmd[256] = {0};
    char *result = NULL;
    char response[4096];

    /* 使用mongoose内置JSON解析 */
    char *cmd_str = mg_json_get_str(hm->body, "$.command");
    if (cmd_str) {
        strncpy(cmd, cmd_str, sizeof(cmd) - 1);
        free(cmd_str);
    }

    if (strlen(cmd) == 0) {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"命令不能为空\",\"Data\":null}");
        return;
    }

    /* 自动添加 AT 前缀 */
    if (strncasecmp(cmd, "AT", 2) != 0) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "AT%s", cmd);
        strncpy(cmd, tmp, sizeof(cmd) - 1);
    }

    printf("执行 AT 命令: %s\n", cmd);

    /* 执行 AT 命令 */
    if (execute_at(cmd, &result) == 0) {
        printf("AT 命令执行成功: %s\n", result);
        char escaped[2048];
        json_escape_string(result ? result : "", escaped, sizeof(escaped));
        snprintf(response, sizeof(response),
            "{\"Code\":0,\"Error\":\"\",\"Data\":\"%s\"}", escaped);
        g_free(result);
    } else {
        printf("AT 命令执行失败: %s\n", dbus_get_last_error());
        char escaped_err[512];
        json_escape_string(dbus_get_last_error(), escaped_err, sizeof(escaped_err));
        snprintf(response, sizeof(response),
            "{\"Code\":1,\"Error\":\"%s\",\"Data\":null}", escaped_err);
    }

    HTTP_OK(c, response);
}


/* 简单 JSON 字符串提取 */
static int extract_json_string(const char *json, const char *key, char *value, size_t size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    char *p = strstr(json, pattern);
    if (!p) return -1;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    p++;
    char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = end - p;
    if (len >= size) len = size - 1;
    memcpy(value, p, len);
    value[len] = '\0';
    return 0;
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

    char response[128];
    if (switch_slot(slot) == 0) {
        snprintf(response, sizeof(response), 
            "{\"status\":\"success\",\"message\":\"Slot switched to %s successfully\"}", slot);
    } else {
        snprintf(response, sizeof(response), 
            "{\"status\":\"error\",\"message\":\"Failed to switch slot to %s\"}", slot);
    }
    HTTP_OK(c, response);
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
    char output[2048];
    
    /* 使用 dbus-send 获取网络信息 (与 Go 版本一致) */
    if (run_command(output, sizeof(output), "dbus-send", "--system", "--dest=org.ofono", 
                    "--print-reply", "/ril_0", "org.ofono.NetworkMonitor.GetServingCellInformation", NULL) != 0) {
        printf("D-Bus 查询网络类型失败，默认使用 4G\n");
        return 0;
    }

    /* 判断网络类型 - 检查是否包含 "nr" */
    if (strstr(output, "\"nr\"")) {
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

    snprintf(response, sizeof(response),
        "{\"Code\":0,\"Error\":\"\",\"Data\":{"
        "\"network_type\":\"%s\","
        "\"band\":\"%s\","
        "\"arfcn\":%d,"
        "\"pci\":%d,"
        "\"rsrp\":%.2f,"
        "\"rsrq\":%.2f,"
        "\"sinr\":%.2f"
        "}}",
        net_type, band, arfcn, pci, rsrp, rsrq, sinr);

    HTTP_OK(c, response);
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

    /* 构建JSON数组 */
    char json[65536];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, "[");
    
    for (int i = 0; i < count; i++) {
        char escaped_content[2048];
        json_escape_string(messages[i].content, escaped_content, sizeof(escaped_content));
        
        char time_str[32];
        struct tm *tm_info = localtime(&messages[i].timestamp);
        strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
        
        offset += snprintf(json + offset, sizeof(json) - offset,
            "%s{\"id\":%d,\"sender\":\"%s\",\"content\":\"%s\",\"timestamp\":\"%s\",\"read\":%s}",
            i > 0 ? "," : "",
            messages[i].id, messages[i].sender, escaped_content, time_str,
            messages[i].is_read ? "true" : "false");
    }
    
    offset += snprintf(json + offset, sizeof(json) - offset, "]");

    HTTP_OK(c, json);
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
    char response[512];
    if (sms_send(recipient, content, result_path, sizeof(result_path)) == 0) {
        snprintf(response, sizeof(response),
            "{\"status\":\"success\",\"message\":\"短信发送成功\",\"path\":\"%s\"}", result_path);
        HTTP_OK(c, response);
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

    char escaped_body[4096];
    char escaped_headers[1024];
    json_escape_string(config.body, escaped_body, sizeof(escaped_body));
    json_escape_string(config.headers, escaped_headers, sizeof(escaped_headers));

    char json[8192];
    snprintf(json, sizeof(json),
        "{\"enabled\":%s,\"platform\":\"%s\",\"url\":\"%s\",\"body\":\"%s\",\"headers\":\"%s\"}",
        config.enabled ? "true" : "false", config.platform, config.url, escaped_body, escaped_headers);

    HTTP_OK(c, json);
}

/* 智能解析JSON字符串值 - 正确处理转义字符 */
static int parse_json_string_field(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;  /* 跳过开始引号 */
    
    size_t i = 0;
    while (*p && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 'r': out[i++] = '\r'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"'; break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/'; break;
                case 'u':
                    /* 跳过Unicode \uXXXX */
                    if (*(p+1) && *(p+2) && *(p+3) && *(p+4)) {
                        p += 4;
                        out[i++] = '?';
                    }
                    break;
                default: out[i++] = *p; break;
            }
            p++;
        } else if (*p == '"') {
            break;  /* 未转义的引号表示字符串结束 */
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 0;
}

/* POST /api/sms/webhook - 保存Webhook配置 */
void handle_sms_webhook_save(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    WebhookConfig config = {0};
    
    /* 复制JSON到临时缓冲区确保以null结尾 */
    char json_buf[8192];
    size_t json_len = hm->body.len < sizeof(json_buf) - 1 ? hm->body.len : sizeof(json_buf) - 1;
    memcpy(json_buf, hm->body.buf, json_len);
    json_buf[json_len] = '\0';
    
    /* 解析 enabled */
    if (strstr(json_buf, "\"enabled\":true") || strstr(json_buf, "\"enabled\": true")) {
        config.enabled = 1;
    }
    
    /* 使用智能解析函数解析字符串字段 */
    parse_json_string_field(json_buf, "platform", config.platform, sizeof(config.platform));
    parse_json_string_field(json_buf, "url", config.url, sizeof(config.url));
    parse_json_string_field(json_buf, "body", config.body, sizeof(config.body));
    parse_json_string_field(json_buf, "headers", config.headers, sizeof(config.headers));

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

    char json[65536];
    int offset = snprintf(json, sizeof(json), "[");
    
    for (int i = 0; i < count; i++) {
        char escaped_content[2048];
        size_t j = 0;
        for (size_t k = 0; messages[i].content[k] && j < sizeof(escaped_content) - 2; k++) {
            char ch = messages[i].content[k];
            if (ch == '"' || ch == '\\') {
                escaped_content[j++] = '\\';
            } else if (ch == '\n') {
                escaped_content[j++] = '\\';
                ch = 'n';
            } else if (ch == '\r') {
                escaped_content[j++] = '\\';
                ch = 'r';
            }
            escaped_content[j++] = ch;
        }
        escaped_content[j] = '\0';
        
        offset += snprintf(json + offset, sizeof(json) - offset,
            "%s{\"id\":%d,\"recipient\":\"%s\",\"content\":\"%s\",\"timestamp\":%ld,\"status\":\"%s\"}",
            i > 0 ? "," : "",
            messages[i].id, messages[i].recipient, escaped_content,
            (long)messages[i].timestamp, messages[i].status);
    }
    
    snprintf(json + offset, sizeof(json) - offset, "]");
    
    HTTP_OK(c, json);
}

/* GET /api/sms/config - 获取短信配置 */
void handle_sms_config_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    int max_count = sms_get_max_count();
    int max_sent_count = sms_get_max_sent_count();
    
    char json[128];
    snprintf(json, sizeof(json), "{\"max_count\":%d,\"max_sent_count\":%d}", max_count, max_sent_count);
    HTTP_OK(c, json);
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
    
    char json[128];
    snprintf(json, sizeof(json), "{\"status\":\"success\",\"max_count\":%d,\"max_sent_count\":%d}", max_count, max_sent_count);
    HTTP_OK(c, json);
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
    char json[64];
    snprintf(json, sizeof(json), "{\"enabled\":%s}", enabled ? "true" : "false");
    HTTP_OK(c, json);
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
        char json[128];
        snprintf(json, sizeof(json), "{\"status\":\"success\",\"enabled\":%s,\"message\":\"%s\"}", 
            enabled ? "true" : "false",
            enabled ? "短信接收修复已开启" : "短信接收修复已关闭");
        HTTP_OK(c, json);
    } else {
        HTTP_ERROR(c, 500, "设置失败，AT命令执行错误");
    }
}

/* ==================== OTA更新 API ==================== */
#include "update.h"

/* GET /api/update/version - 获取当前版本 */
void handle_update_version(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char json[128];
    snprintf(json, sizeof(json), "{\"version\":\"%s\"}", update_get_version());
    HTTP_OK(c, json);
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
            char json[128];
            snprintf(json, sizeof(json), "{\"status\":\"success\",\"message\":\"上传成功\",\"size\":%lu}", (unsigned long)part.body.len);
            HTTP_OK(c, json);
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
        char escaped[1024];
        json_escape_string(output, escaped, sizeof(escaped));
        char json[2048];
        snprintf(json, sizeof(json), "{\"status\":\"success\",\"message\":\"安装成功，正在重启...\",\"output\":\"%s\"}", escaped);
        HTTP_OK(c, json);
        c->is_draining = 1;
        sleep(2);
        device_reboot();
    } else {
        char escaped[1024];
        json_escape_string(output, escaped, sizeof(escaped));
        char json[2048];
        snprintf(json, sizeof(json), "{\"error\":\"安装失败\",\"output\":\"%s\"}", escaped);
        HTTP_JSON(c, 500, json);
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
        
        char escaped_changelog[2048];
        json_escape_string(info.changelog, escaped_changelog, sizeof(escaped_changelog));
        
        char json[4096];
        snprintf(json, sizeof(json),
            "{\"current_version\":\"%s\",\"latest_version\":\"%s\",\"has_update\":%s,"
            "\"url\":\"%s\",\"changelog\":\"%s\",\"size\":%lu,\"required\":%s}",
            current, info.version, has_update ? "true" : "false",
            info.url, escaped_changelog, (unsigned long)info.size, info.required ? "true" : "false");
        HTTP_OK(c, json);
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
    
    char json[256];
    snprintf(json, sizeof(json),
        "{\"Code\":0,\"Data\":{\"datetime\":\"%s\",\"date\":\"%s\",\"time\":\"%s\",\"timestamp\":%ld}}",
        datetime, date, time_str, (long)now);
    HTTP_OK(c, json);
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
    
    if (success) {
        run_command(output, sizeof(output), "hwclock", "-w", NULL);
        char json[128];
        snprintf(json, sizeof(json), "{\"Code\":0,\"Data\":\"NTP同步成功\",\"server\":\"%s\"}", used_server);
        HTTP_OK(c, json);
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"所有NTP服务器同步失败\"}");
    }
}

/* ==================== 数据连接和漫游 API ==================== */
#include "ofono.h"

/* GET/POST /api/data - 数据连接开关 */
void handle_data_status(struct mg_connection *c, struct mg_http_message *hm) {
    char response[256];

    if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        /* GET - 查询数据连接状态 */
        int active = 0;
        if (ofono_get_data_status(&active) == 0) {
            snprintf(response, sizeof(response),
                "{\"status\":\"ok\",\"message\":\"Success\",\"data\":{\"active\":%s}}",
                active ? "true" : "false");
            HTTP_OK(c, response);
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
            snprintf(response, sizeof(response),
                "{\"status\":\"ok\",\"message\":\"Data connection %s successfully\",\"data\":{\"active\":%s}}",
                active ? "enabled" : "disabled",
                active ? "true" : "false");
            HTTP_OK(c, response);
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set data connection\"}");
        }
    } else {
        HTTP_ERROR(c, 405, "Method not allowed");
    }
}

/* GET/POST /api/roaming - 漫游开关 */
void handle_roaming_status(struct mg_connection *c, struct mg_http_message *hm) {
    char response[256];

    if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        /* GET - 查询漫游状态 */
        int roaming_allowed = 0;
        int is_roaming = 0;
        if (ofono_get_roaming_status(&roaming_allowed, &is_roaming) == 0) {
            snprintf(response, sizeof(response),
                "{\"status\":\"ok\",\"message\":\"Success\",\"data\":{\"roaming_allowed\":%s,\"is_roaming\":%s}}",
                roaming_allowed ? "true" : "false",
                is_roaming ? "true" : "false");
            HTTP_OK(c, response);
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
            
            snprintf(response, sizeof(response),
                "{\"status\":\"ok\",\"message\":\"Roaming %s successfully\",\"data\":{\"roaming_allowed\":%s,\"is_roaming\":%s}}",
                allowed ? "enabled" : "disabled",
                roaming_allowed ? "true" : "false",
                is_roaming ? "true" : "false");
            HTTP_OK(c, response);
        } else {
            HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set roaming\"}");
        }
    } else {
        HTTP_ERROR(c, 405, "Method not allowed");
    }
}


/* ==================== APN 管理 API ==================== */

/* GET /api/apn - 获取 APN 列表 */
void handle_apn_list(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    ApnContext contexts[MAX_APN_CONTEXTS];
    int count = ofono_get_all_apn_contexts(contexts, MAX_APN_CONTEXTS);

    if (count < 0) {
        HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to get APN list\"}");
        return;
    }

    /* 构建 JSON 响应 */
    char json[8192];
    int offset = 0;
    offset += snprintf(json + offset, sizeof(json) - offset, 
        "{\"status\":\"ok\",\"message\":\"Success\",\"data\":{\"contexts\":[");

    for (int i = 0; i < count; i++) {
        ApnContext *ctx = &contexts[i];
        offset += snprintf(json + offset, sizeof(json) - offset,
            "%s{"
            "\"path\":\"%s\","
            "\"name\":\"%s\","
            "\"active\":%s,"
            "\"apn\":\"%s\","
            "\"protocol\":\"%s\","
            "\"username\":\"%s\","
            "\"password\":\"%s\","
            "\"auth_method\":\"%s\","
            "\"context_type\":\"%s\""
            "}",
            i > 0 ? "," : "",
            ctx->path, ctx->name, ctx->active ? "true" : "false",
            ctx->apn, ctx->protocol, ctx->username, ctx->password,
            ctx->auth_method, ctx->context_type);
    }

    offset += snprintf(json + offset, sizeof(json) - offset, "]}}");
    HTTP_OK(c, json);
}

/* POST /api/apn - 设置 APN 配置 */
void handle_apn_set(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char context_path[128] = {0};
    char apn[128] = {0};
    char protocol[32] = {0};
    char username[128] = {0};
    char password[128] = {0};
    char auth_method[32] = {0};

    /* 解析 JSON 请求体 */
    char *str;
    str = mg_json_get_str(hm->body, "$.context_path");
    if (str) { strncpy(context_path, str, sizeof(context_path) - 1); free(str); }

    str = mg_json_get_str(hm->body, "$.apn");
    if (str) { strncpy(apn, str, sizeof(apn) - 1); free(str); }

    str = mg_json_get_str(hm->body, "$.protocol");
    if (str) { strncpy(protocol, str, sizeof(protocol) - 1); free(str); }

    str = mg_json_get_str(hm->body, "$.username");
    if (str) { strncpy(username, str, sizeof(username) - 1); free(str); }

    str = mg_json_get_str(hm->body, "$.password");
    if (str) { strncpy(password, str, sizeof(password) - 1); free(str); }

    str = mg_json_get_str(hm->body, "$.auth_method");
    if (str) { strncpy(auth_method, str, sizeof(auth_method) - 1); free(str); }

    /* 验证必填字段 */
    if (strlen(context_path) == 0) {
        HTTP_ERROR(c, 400, "context_path is required");
        return;
    }

    /* 调用设置函数 */
    int ret = ofono_set_apn_properties(
        context_path,
        strlen(apn) > 0 ? apn : NULL,
        strlen(protocol) > 0 ? protocol : NULL,
        strlen(username) > 0 ? username : NULL,
        strlen(password) > 0 ? password : NULL,
        strlen(auth_method) > 0 ? auth_method : NULL
    );

    if (ret == 0) {
        /* 获取更新后的配置 */
        ApnContext contexts[MAX_APN_CONTEXTS];
        int count = ofono_get_all_apn_contexts(contexts, MAX_APN_CONTEXTS);
        
        /* 查找刚修改的 context */
        ApnContext *updated = NULL;
        for (int i = 0; i < count; i++) {
            if (strcmp(contexts[i].path, context_path) == 0) {
                updated = &contexts[i];
                break;
            }
        }

        char json[2048];
        if (updated) {
            snprintf(json, sizeof(json),
                "{\"status\":\"ok\",\"message\":\"APN configuration updated successfully\","
                "\"data\":{\"updated_context\":{"
                "\"path\":\"%s\",\"name\":\"%s\",\"active\":%s,"
                "\"apn\":\"%s\",\"protocol\":\"%s\",\"username\":\"%s\","
                "\"password\":\"%s\",\"auth_method\":\"%s\",\"context_type\":\"%s\""
                "}}}",
                updated->path, updated->name, updated->active ? "true" : "false",
                updated->apn, updated->protocol, updated->username,
                updated->password, updated->auth_method, updated->context_type);
        } else {
            snprintf(json, sizeof(json),
                "{\"status\":\"ok\",\"message\":\"APN configuration updated successfully\",\"data\":{}}");
        }
        HTTP_OK(c, json);
    } else {
        HTTP_OK(c, "{\"status\":\"error\",\"message\":\"Failed to set APN configuration\"}");
    }
}

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
    char response[16384];

    if (execute_shell(cmd, output, sizeof(output)) == 0) {
        /* 转义输出 */
        char escaped[8192];
        size_t j = 0;
        for (size_t i = 0; output[i] && j < sizeof(escaped) - 2; i++) {
            char ch = output[i];
            if (ch == '"' || ch == '\\') {
                escaped[j++] = '\\';
            } else if (ch == '\n') {
                escaped[j++] = '\\';
                ch = 'n';
            } else if (ch == '\r') {
                escaped[j++] = '\\';
                ch = 'r';
            } else if (ch == '\t') {
                escaped[j++] = '\\';
                ch = 't';
            }
            if ((unsigned char)ch >= 0x20 || ch == 'n' || ch == 'r' || ch == 't') {
                escaped[j++] = ch;
            }
        }
        escaped[j] = '\0';

        snprintf(response, sizeof(response),
            "{\"Code\":0,\"Error\":\"\",\"Data\":\"%s\"}", escaped);
    } else {
        snprintf(response, sizeof(response),
            "{\"Code\":1,\"Error\":\"命令执行失败\",\"Data\":\"%s\"}", output);
    }

    HTTP_OK(c, response);
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
    
    char response[512 * 1024 + 128];
    snprintf(response, sizeof(response),
        "{\"Code\":0,\"Error\":\"\",\"Data\":%s,\"Count\":%d}", json, count);
    
    HTTP_OK(c, response);
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
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"插件内容不能为空\",\"Data\":null}");
        return;
    }

    if (strlen(name) == 0) {
        /* 从内容中提取名称 */
        strcpy(name, "plugin");
    }

    if (save_plugin(name, content_str) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"插件上传成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"插件保存失败\",\"Data\":null}");
    }

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
    char name[256] = {0};
    int i = 0;
    while (name_start[i] && name_start[i] != '?' && name_start[i] != ' ' && i < 255) {
        name[i] = name_start[i];
        i++;
    }
    name[i] = '\0';

    if (strlen(name) == 0) {
        HTTP_ERROR(c, 400, "插件名称不能为空");
        return;
    }

    if (delete_plugin(name) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"插件删除成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"插件删除失败\",\"Data\":null}");
    }
}

/* DELETE /api/plugins/all - 删除所有插件 */
void handle_plugin_delete_all(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    if (delete_all_plugins() == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"所有插件已删除\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"删除失败\",\"Data\":null}");
    }
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

                    /* 转义内容 */
                    char escaped[65536];
                    size_t j = 0;
                    for (size_t i = 0; content[i] && j < sizeof(escaped) - 2; i++) {
                        char ch = content[i];
                        if (ch == '"' || ch == '\\') { escaped[j++] = '\\'; }
                        else if (ch == '\n') { escaped[j++] = '\\'; ch = 'n'; }
                        else if (ch == '\r') { escaped[j++] = '\\'; ch = 'r'; }
                        else if (ch == '\t') { escaped[j++] = '\\'; ch = 't'; }
                        if ((unsigned char)ch >= 0x20 || ch == 'n' || ch == 'r' || ch == 't') {
                            escaped[j++] = ch;
                        }
                    }
                    escaped[j] = '\0';

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

    char response[256 * 1024 + 128];
    snprintf(response, sizeof(response),
        "{\"Code\":0,\"Error\":\"\",\"Data\":%s,\"Count\":%d}", json, count);
    
    HTTP_OK(c, response);
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
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本内容不能为空\",\"Data\":null}");
        return;
    }

    if (strlen(name) == 0) {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本名称不能为空\",\"Data\":null}");
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
    
    FILE *f = fopen(filepath, "w");
    if (f) {
        fputs(content_str, f);
        fclose(f);
        /* 添加执行权限 */
        char chmod_cmd[512];
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x %s", filepath);
        system(chmod_cmd);
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"脚本上传成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本保存失败\",\"Data\":null}");
    }

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
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本内容不能为空\",\"Data\":null}");
        return;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, name);
    
    FILE *f = fopen(filepath, "w");
    if (f) {
        fputs(content_str, f);
        fclose(f);
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"脚本更新成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本更新失败\",\"Data\":null}");
    }

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

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTS_DIR, name);
    
    if (remove(filepath) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"脚本删除成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"脚本删除失败\",\"Data\":null}");
    }
}

/* ==================== 插件存储 API ==================== */
#include "plugin_storage.h"

/* 从URL提取插件名 /api/plugins/storage/:name */
static int extract_plugin_name_from_url(const char *uri, char *name, size_t size) {
    const char *prefix = "/api/plugins/storage/";
    const char *start = strstr(uri, prefix);
    if (!start) return -1;
    
    start += strlen(prefix);
    size_t i = 0;
    while (start[i] && start[i] != '?' && start[i] != ' ' && i < size - 1) {
        name[i] = start[i];
        i++;
    }
    name[i] = '\0';
    return (i > 0) ? 0 : -1;
}

/* GET /api/plugins/storage/:name - 读取插件存储 */
void handle_plugin_storage_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char plugin_name[256] = {0};
    if (extract_plugin_name_from_url(hm->uri.buf, plugin_name, sizeof(plugin_name)) != 0) {
        HTTP_ERROR(c, 400, "无效的插件名称");
        return;
    }

    char json_data[PLUGIN_STORAGE_MAX_SIZE + 256];
    char storage_content[PLUGIN_STORAGE_MAX_SIZE];
    
    if (plugin_storage_read(plugin_name, storage_content, sizeof(storage_content)) == 0) {
        snprintf(json_data, sizeof(json_data), 
            "{\"Code\":0,\"Error\":\"\",\"Data\":%s}", storage_content);
        HTTP_OK(c, json_data);
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"读取存储失败\",\"Data\":null}");
    }
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

    if (plugin_storage_write(plugin_name, json_data) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"存储成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"存储失败，可能超出大小限制(64KB)\",\"Data\":null}");
    }
}

/* DELETE /api/plugins/storage/:name - 删除插件存储 */
void handle_plugin_storage_delete(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_DELETE(c, hm);

    char plugin_name[256] = {0};
    if (extract_plugin_name_from_url(hm->uri.buf, plugin_name, sizeof(plugin_name)) != 0) {
        HTTP_ERROR(c, 400, "无效的插件名称");
        return;
    }

    if (plugin_storage_delete(plugin_name) == 0) {
        HTTP_OK(c, "{\"Code\":0,\"Error\":\"\",\"Data\":\"删除成功\"}");
    } else {
        HTTP_OK(c, "{\"Code\":1,\"Error\":\"删除失败\",\"Data\":null}");
    }
}
