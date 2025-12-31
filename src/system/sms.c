/**
 * @file sms.c
 * @brief 短信管理模块实现 - D-Bus监控、发送、Webhook转发
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <gio/gio.h>
#include "sms.h"
#include "database.h"
#include "exec_utils.h"

/* 短信模块专用互斥锁 */
static pthread_mutex_t g_sms_mutex = PTHREAD_MUTEX_INITIALIZER;
static GDBusConnection *g_sms_dbus_conn = NULL;
static guint g_signal_subscription_id = 0;
static guint g_name_watch_id = 0;
static int g_sms_initialized = 0;
static int g_ofono_available = 0;

/* Webhook配置 */
static WebhookConfig g_webhook_config = {0};

/* 最大短信存储数量 */
#define DEFAULT_MAX_SMS_COUNT 50
#define DEFAULT_MAX_SENT_COUNT 10
static int g_max_sms_count = DEFAULT_MAX_SMS_COUNT;
static int g_max_sent_count = DEFAULT_MAX_SENT_COUNT;

/* 前向声明 */
static void on_incoming_message(GDBusConnection *conn, const gchar *sender_name,
    const gchar *object_path, const gchar *interface_name, const gchar *signal_name,
    GVariant *parameters, gpointer user_data);
static int save_sms_to_db(const char *sender, const char *content, time_t timestamp);
static int save_sent_sms_to_db(const char *recipient, const char *content, time_t timestamp, const char *status);
static void send_webhook_notification(const SmsMessage *msg);
static void load_sms_config(void);
static void subscribe_sms_signal(void);
static void unsubscribe_sms_signal(void);
static void on_ofono_appeared(GDBusConnection *conn, const gchar *name, const gchar *name_owner, gpointer user_data);
static void on_ofono_vanished(GDBusConnection *conn, const gchar *name, gpointer user_data);
static void apply_sms_fix_on_init(void);

/* Hex解码函数 - 将hex字符串解码为原始字节 */
static void hex_decode(const char *hex, char *out, size_t out_size) {
    size_t i = 0, j = 0;
    size_t hex_len = strlen(hex);
    
    while (i < hex_len && j < out_size - 1) {
        unsigned int byte;
        if (sscanf(hex + i, "%2x", &byte) == 1) {
            out[j++] = (char)byte;
            i += 2;
        } else {
            break;
        }
    }
    out[j] = '\0';
}

/* 保存短信到数据库 */
static int save_sms_to_db(const char *sender, const char *content, time_t timestamp) {
    char sql[2048];
    char escaped_content[1024];
    
    /* 转义单引号 */
    size_t j = 0;
    for (size_t i = 0; content[i] && j < sizeof(escaped_content) - 2; i++) {
        if (content[i] == '\'') {
            escaped_content[j++] = '\'';
            escaped_content[j++] = '\'';
        } else {
            escaped_content[j++] = content[i];
        }
    }
    escaped_content[j] = '\0';
    
    snprintf(sql, sizeof(sql),
        "INSERT INTO sms (sender, content, timestamp, is_read) VALUES ('%s', '%s', %ld, 0);",
        sender, escaped_content, (long)timestamp);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    /* 清理超出限制的旧短信 */
    if (ret == 0) {
        printf("[SMS] 短信保存成功，当前最大限制: %d\n", g_max_sms_count);
        snprintf(sql, sizeof(sql),
            "DELETE FROM sms WHERE id NOT IN (SELECT id FROM sms ORDER BY id DESC LIMIT %d);",
            g_max_sms_count);
        pthread_mutex_lock(&g_sms_mutex);
        db_execute(sql);
        pthread_mutex_unlock(&g_sms_mutex);
    } else {
        printf("[SMS] 短信保存失败!\n");
    }
    
    return ret;
}

/* 订阅短信信号 */
static void subscribe_sms_signal(void) {
    if (!g_sms_dbus_conn) {
        printf("[SMS] D-Bus未连接，无法订阅信号\n");
        return;
    }
    
    /* 先取消旧的订阅 */
    unsubscribe_sms_signal();
    
    /* 添加D-Bus match规则 - 与Go版本保持一致 */
    GError *error = NULL;
    GVariant *result = g_dbus_connection_call_sync(
        g_sms_dbus_conn,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "AddMatch",
        g_variant_new("(s)", "type='signal',interface='org.ofono.MessageManager',member='IncomingMessage'"),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1, NULL, &error
    );
    
    if (error) {
        printf("[SMS] 添加D-Bus match规则失败: %s\n", error->message);
        g_error_free(error);
    } else {
        printf("[SMS] D-Bus match规则添加成功\n");
        if (result) g_variant_unref(result);
    }
    
    /* 订阅短信信号 */
    g_signal_subscription_id = g_dbus_connection_signal_subscribe(
        g_sms_dbus_conn,
        "org.ofono",
        "org.ofono.MessageManager",
        "IncomingMessage",
        NULL, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_incoming_message,
        NULL, NULL
    );
    
    printf("[SMS] 短信信号订阅ID: %u\n", g_signal_subscription_id);
}

/* 取消短信信号订阅 */
static void unsubscribe_sms_signal(void) {
    if (g_signal_subscription_id > 0 && g_sms_dbus_conn) {
        g_dbus_connection_signal_unsubscribe(g_sms_dbus_conn, g_signal_subscription_id);
        printf("[SMS] 已取消信号订阅ID: %u\n", g_signal_subscription_id);
    }
    g_signal_subscription_id = 0;
}

/* oFono服务出现回调 */
static void on_ofono_appeared(GDBusConnection *conn, const gchar *name, 
    const gchar *name_owner, gpointer user_data) {
    (void)conn; (void)user_data;
    
    printf("[SMS] oFono服务已启动: %s (owner: %s)\n", name, name_owner);
    g_ofono_available = 1;
    
    /* 重新订阅短信信号 */
    subscribe_sms_signal();
}

/* oFono服务消失回调 */
static void on_ofono_vanished(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn; (void)user_data;
    
    printf("[SMS] oFono服务已停止: %s\n", name);
    g_ofono_available = 0;
    
    /* 取消信号订阅 */
    unsubscribe_sms_signal();
}

/* D-Bus连接关闭回调 */
static void on_dbus_connection_closed(GDBusConnection *conn, gboolean remote_peer_vanished,
    GError *error, gpointer user_data) {
    (void)conn; (void)user_data;
    
    printf("[SMS] D-Bus连接已关闭! remote_peer_vanished=%d, error=%s\n", 
           remote_peer_vanished, error ? error->message : "无");
    
    /* 清理状态 */
    g_signal_subscription_id = 0;
    g_name_watch_id = 0;
    g_ofono_available = 0;
    g_sms_dbus_conn = NULL;
}

/* D-Bus信号处理 - 接收新短信 */
static void on_incoming_message(GDBusConnection *conn, const gchar *sender_name,
    const gchar *object_path, const gchar *interface_name, const gchar *signal_name,
    GVariant *parameters, gpointer user_data) {
    
    (void)conn; (void)sender_name; (void)object_path;
    (void)interface_name; (void)signal_name; (void)user_data;
    
    printf("[SMS] 收到新短信信号! path=%s\n", object_path);
    
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sa{sv})"))) {
        printf("[SMS] 短信信号参数类型不匹配\n");
        return;
    }
    
    const gchar *content = NULL;
    GVariant *props = NULL;
    g_variant_get(parameters, "(&s@a{sv})", &content, &props);
    
    if (!content || !props) {
        printf("[SMS] 解析短信内容失败\n");
        if (props) g_variant_unref(props);
        return;
    }
    
    /* 提取发送者 */
    char sender[64] = "未知";
    GVariant *sender_var = g_variant_lookup_value(props, "Sender", G_VARIANT_TYPE_STRING);
    if (sender_var) {
        const gchar *s = g_variant_get_string(sender_var, NULL);
        if (s) strncpy(sender, s, sizeof(sender) - 1);
        g_variant_unref(sender_var);
    }
    
    printf("[SMS] 新短信 - 发件人: %s, 内容: %s\n", sender, content);
    
    /* 保存到数据库 */
    time_t now = time(NULL);
    if (save_sms_to_db(sender, content, now) == 0) {
        printf("[SMS] 短信已保存到数据库\n");
        
        /* 发送Webhook通知 */
        if (g_webhook_config.enabled && strlen(g_webhook_config.url) > 0) {
            SmsMessage msg = {0};
            strncpy(msg.sender, sender, sizeof(msg.sender) - 1);
            strncpy(msg.content, content, sizeof(msg.content) - 1);
            msg.timestamp = now;
            send_webhook_notification(&msg);
        }
    }
    
    g_variant_unref(props);
}

/* 发送Webhook通知 */
static void send_webhook_notification(const SmsMessage *msg) {
    if (!g_webhook_config.enabled || strlen(g_webhook_config.url) == 0) {
        return;
    }
    
    printf("[SMS] 发送Webhook通知到: %s\n", g_webhook_config.url);
    
    /* 替换变量 */
    char body[4096];
    strncpy(body, g_webhook_config.body, sizeof(body) - 1);
    body[sizeof(body) - 1] = '\0';
    
    /* 简单的变量替换 */
    char *p;
    char temp[4096];
    
    /* 替换 #{sender} */
    while ((p = strstr(body, "#{sender}")) != NULL) {
        *p = '\0';
        snprintf(temp, sizeof(temp), "%s%s%s", body, msg->sender, p + 9);
        strncpy(body, temp, sizeof(body) - 1);
    }
    
    /* 替换 #{content} */
    while ((p = strstr(body, "#{content}")) != NULL) {
        *p = '\0';
        snprintf(temp, sizeof(temp), "%s%s%s", body, msg->content, p + 10);
        strncpy(body, temp, sizeof(body) - 1);
    }
    
    /* 替换 #{time} */
    char time_str[32];
    struct tm *tm_info = localtime(&msg->timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    while ((p = strstr(body, "#{time}")) != NULL) {
        *p = '\0';
        snprintf(temp, sizeof(temp), "%s%s%s", body, time_str, p + 7);
        strncpy(body, temp, sizeof(body) - 1);
    }
    
    /* 将body写入临时文件，避免shell转义问题 */
    const char *tmp_file = "/tmp/webhook_body.json";
    FILE *fp = fopen(tmp_file, "w");
    if (fp) {
        fputs(body, fp);
        fclose(fp);
    } else {
        printf("[SMS] 无法创建临时文件\n");
        return;
    }
    
    /* 构建curl命令 */
    char cmd[8192];
    char headers_part[1024] = "";
    
    /* 解析自定义headers（格式：Header1: Value1\nHeader2: Value2） */
    if (strlen(g_webhook_config.headers) > 0) {
        char headers_copy[512];
        strncpy(headers_copy, g_webhook_config.headers, sizeof(headers_copy) - 1);
        headers_copy[sizeof(headers_copy) - 1] = '\0';
        
        char *line = strtok(headers_copy, "\n");
        while (line) {
            /* 跳过空行 */
            while (*line == ' ' || *line == '\r') line++;
            if (strlen(line) > 0 && strchr(line, ':')) {
                char header_arg[256];
                /* 去除行尾的\r */
                char *cr = strchr(line, '\r');
                if (cr) *cr = '\0';
                snprintf(header_arg, sizeof(header_arg), " -H '%s'", line);
                strncat(headers_part, header_arg, sizeof(headers_part) - strlen(headers_part) - 1);
            }
            line = strtok(NULL, "\n");
        }
    }
    
    /* 默认添加Content-Type（如果用户没有指定） */
    /* 使用 sh -c 包装命令，确保 curl 完成后删除临时文件 */
    if (strstr(headers_part, "Content-Type") == NULL) {
        snprintf(cmd, sizeof(cmd),
            "sh -c \"curl -s -X POST '%s' -H 'Content-Type: application/json'%s -d @%s; rm -f %s\" &",
            g_webhook_config.url, headers_part, tmp_file, tmp_file);
    } else {
        snprintf(cmd, sizeof(cmd),
            "sh -c \"curl -s -X POST '%s'%s -d @%s; rm -f %s\" &",
            g_webhook_config.url, headers_part, tmp_file, tmp_file);
    }
    
    printf("[SMS] 执行: %s\n", cmd);
    system(cmd);
}

/* 初始化短信模块 */
int sms_init(const char *db_path) {
    GError *error = NULL;
    
    if (g_sms_initialized) {
        return 0;
    }
    
    printf("[SMS] 初始化短信模块\n");
    
    /* 初始化数据库模块 */
    if (db_init(db_path) != 0) {
        printf("[SMS] 数据库初始化失败\n");
        return -1;
    }
    
    printf("[SMS] 数据库路径: %s\n", db_get_path());
    
    /* 加载配置 */
    load_sms_config();
    sms_get_webhook_config(&g_webhook_config);
    
    /* 连接D-Bus */
    g_sms_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!g_sms_dbus_conn) {
        printf("[SMS] D-Bus连接失败: %s\n", error ? error->message : "未知错误");
        if (error) g_error_free(error);
        return -1;
    }
    
    /* 监听D-Bus连接关闭事件 */
    g_signal_connect(g_sms_dbus_conn, "closed", G_CALLBACK(on_dbus_connection_closed), NULL);
    
    /* 监控oFono服务状态 - 当服务重启时自动重新订阅 */
    g_name_watch_id = g_bus_watch_name_on_connection(
        g_sms_dbus_conn,
        "org.ofono",
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_ofono_appeared,
        on_ofono_vanished,
        NULL, NULL
    );
    
    printf("[SMS] oFono服务监控ID: %u\n", g_name_watch_id);
    
    /* 应用短信修复设置 - 在D-Bus连接后执行 */
    apply_sms_fix_on_init();
    
    /* 立即尝试订阅信号（如果oFono已经运行） */
    subscribe_sms_signal();
    g_ofono_available = 1;  /* 假设oFono可用，后续会通过监控更新 */
    
    printf("[SMS] 短信模块初始化成功\n");
    g_sms_initialized = 1;
    return 0;
}

/* 关闭短信模块 */
void sms_deinit(void) {
    if (!g_sms_initialized) return;
    
    /* 取消信号订阅 */
    unsubscribe_sms_signal();
    
    /* 取消oFono服务监控 */
    if (g_name_watch_id > 0) {
        g_bus_unwatch_name(g_name_watch_id);
        g_name_watch_id = 0;
    }
    
    if (g_sms_dbus_conn) {
        g_object_unref(g_sms_dbus_conn);
        g_sms_dbus_conn = NULL;
    }
    
    g_ofono_available = 0;
    g_sms_initialized = 0;
    printf("[SMS] 短信模块已关闭\n");
}


/* 发送短信 */
int sms_send(const char *recipient, const char *content, char *result_path, size_t path_size) {
    GError *error = NULL;
    GVariant *result = NULL;
    
    if (!recipient || !content || strlen(recipient) == 0 || strlen(content) == 0) {
        printf("发送短信参数无效\n");
        return -1;
    }
    
    if (!g_sms_dbus_conn || !g_ofono_available) {
        printf("[SMS] D-Bus未连接或oFono服务不可用\n");
        return -1;
    }
    
    printf("[SMS] 发送短信到 %s: %s\n", recipient, content);
    
    /* 调用 org.ofono.MessageManager.SendMessage */
    result = g_dbus_connection_call_sync(
        g_sms_dbus_conn,
        "org.ofono",
        "/ril_0",
        "org.ofono.MessageManager",
        "SendMessage",
        g_variant_new("(ss)", recipient, content),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        15000,  /* 15秒超时 */
        NULL,
        &error
    );
    
    if (!result) {
        printf("[SMS] 发送短信失败: %s\n", error ? error->message : "未知错误");
        if (error) g_error_free(error);
        return -1;
    }
    
    /* 获取返回的消息路径 */
    const gchar *path = NULL;
    g_variant_get(result, "(&o)", &path);
    
    if (result_path && path_size > 0 && path) {
        strncpy(result_path, path, path_size - 1);
        result_path[path_size - 1] = '\0';
    }
    
    printf("[SMS] 短信发送成功，路径: %s\n", path ? path : "N/A");
    g_variant_unref(result);
    
    /* 保存发送记录到数据库 */
    save_sent_sms_to_db(recipient, content, time(NULL), "sent");
    
    return 0;
}

/* 获取短信列表 - 使用hex编码避免特殊字符问题，兼容无JSON扩展的SQLite */
int sms_get_list(SmsMessage *messages, int max_count) {
    char sql[512];
    char *output = NULL;
    
    if (!messages || max_count <= 0) return -1;
    
    /* 分配大缓冲区 */
    output = (char *)malloc(256 * 1024);
    if (!output) return -1;
    
    /* 使用hex编码content字段，用|分隔，每行一条记录 */
    snprintf(sql, sizeof(sql),
        "SELECT id || '|' || sender || '|' || hex(content) || '|' || timestamp || '|' || is_read FROM sms ORDER BY id DESC LIMIT %d;",
        max_count);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_query_string(sql, output, 256 * 1024);
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret != 0 || strlen(output) == 0) {
        printf("[SMS] 获取短信列表失败或为空\n");
        free(output);
        return 0;
    }
    
    /* 解析输出 - 格式: id|sender|hex_content|timestamp|is_read\n */
    int count = 0;
    char *line = output;
    char *next_line;
    
    while (line && *line && count < max_count) {
        /* 找到行尾 */
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }
        
        /* 跳过空行 */
        if (strlen(line) == 0) {
            line = next_line;
            continue;
        }
        
        /* 解析字段: id|sender|hex_content|timestamp|is_read */
        char *fields[5] = {NULL};
        int field_count = 0;
        char *p = line;
        char *field_start = p;
        
        while (*p && field_count < 5) {
            if (*p == '|') {
                *p = '\0';
                fields[field_count++] = field_start;
                field_start = p + 1;
            }
            p++;
        }
        if (field_count < 5 && field_start) {
            fields[field_count++] = field_start;
        }
        
        if (field_count >= 5) {
            messages[count].id = atoi(fields[0]);
            strncpy(messages[count].sender, fields[1], sizeof(messages[count].sender) - 1);
            messages[count].sender[sizeof(messages[count].sender) - 1] = '\0';
            
            /* hex解码content */
            hex_decode(fields[2], messages[count].content, sizeof(messages[count].content));
            
            messages[count].timestamp = (time_t)atol(fields[3]);
            messages[count].is_read = atoi(fields[4]);
            count++;
        }
        
        line = next_line;
    }
    
    free(output);
    printf("[SMS] 获取到 %d 条短信\n", count);
    return count;
}

/* 获取短信总数 */
int sms_get_count(void) {
    const char *sql = "SELECT COUNT(*) FROM sms;";
    return db_query_int(sql, -1);
}

/* 删除短信 */
int sms_delete(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM sms WHERE id = %d;", id);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    return ret;
}

/* 清空所有短信 */
int sms_clear_all(void) {
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute("DELETE FROM sms;");
    pthread_mutex_unlock(&g_sms_mutex);
    return ret;
}

/* 获取Webhook配置 */
int sms_get_webhook_config(WebhookConfig *config) {
    char output[4096];
    
    if (!config) return -1;
    
    memset(config, 0, sizeof(WebhookConfig));
    
    const char *sql = "SELECT enabled, platform, url, body, headers FROM webhook_config WHERE id = 1;";
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_query_rows(sql, "|", output, sizeof(output));
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret != 0 || strlen(output) == 0) {
        /* 使用默认配置 */
        config->enabled = 0;
        strcpy(config->platform, "pushplus");
        return 0;
    }
    
    /* 解析输出 */
    char *fields[5] = {NULL};
    int field_count = 0;
    char *p = output;
    char *start = p;
    
    while (*p && field_count < 5) {
        if (*p == '|') {
            *p = '\0';
            fields[field_count++] = start;
            start = p + 1;
        }
        p++;
    }
    if (field_count < 5 && start) {
        fields[field_count++] = start;
    }
    
    if (field_count >= 5) {
        config->enabled = atoi(fields[0]);
        strncpy(config->platform, fields[1], sizeof(config->platform) - 1);
        strncpy(config->url, fields[2], sizeof(config->url) - 1);
        strncpy(config->body, fields[3], sizeof(config->body) - 1);
        strncpy(config->headers, fields[4], sizeof(config->headers) - 1);
        
        /* 去除末尾换行符 */
        char *nl = strchr(config->headers, '\n');
        if (nl) *nl = '\0';
        
        /* 反转义特殊字符 */
        db_unescape_string(config->url);
        db_unescape_string(config->body);
        db_unescape_string(config->headers);
    }
    
    return 0;
}

/* 保存Webhook配置 */
int sms_save_webhook_config(const WebhookConfig *config) {
    char sql[8192];
    char escaped_body[4096];
    char escaped_headers[1024];
    char escaped_url[1024];
    
    if (!config) return -1;
    
    /* 转义特殊字符 */
    db_escape_string(config->body, escaped_body, sizeof(escaped_body));
    db_escape_string(config->headers, escaped_headers, sizeof(escaped_headers));
    db_escape_string(config->url, escaped_url, sizeof(escaped_url));
    
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO webhook_config (id, enabled, platform, url, body, headers) "
        "VALUES (1, %d, '%s', '%s', '%s', '%s');",
        config->enabled, config->platform, escaped_url, escaped_body, escaped_headers);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret == 0) {
        /* 更新内存中的配置 */
        memcpy(&g_webhook_config, config, sizeof(WebhookConfig));
        printf("[SMS] Webhook配置保存成功\n");
    } else {
        printf("[SMS] Webhook配置保存失败\n");
    }
    
    return ret;
}

/* 测试Webhook */
int sms_test_webhook(void) {
    SmsMessage test_msg = {
        .id = 0,
        .sender = "+8613800138000",
        .content = "这是一条测试短信",
        .timestamp = time(NULL),
        .is_read = 0
    };
    
    if (!g_webhook_config.enabled || strlen(g_webhook_config.url) == 0) {
        printf("[SMS] Webhook未启用或URL为空\n");
        return -1;
    }
    
    send_webhook_notification(&test_msg);
    return 0;
}

/* 检查短信模块状态 */
int sms_check_status(void) {
    printf("[SMS] 状态检查 - 初始化: %d, D-Bus连接: %p, oFono可用: %d, 信号订阅: %u\n",
           g_sms_initialized, (void*)g_sms_dbus_conn, g_ofono_available, g_signal_subscription_id);
    return g_sms_initialized && g_sms_dbus_conn && g_ofono_available && g_signal_subscription_id > 0;
}

/* 定期维护短信模块 - 增强版 */
void sms_maintenance(void) {
    static int check_count = 0;
    check_count++;
    
    /* 每10次检查输出一次状态（约10秒一次，假设1秒调用一次） */
    if (check_count % 10 == 0) {
        printf("[SMS] 维护检查 #%d - D-Bus: %p, oFono: %d, 订阅ID: %u\n",
               check_count, (void*)g_sms_dbus_conn, g_ofono_available, g_signal_subscription_id);
    }
    
    if (!g_sms_initialized) {
        return;
    }
    
    /* 检查D-Bus连接是否有效 */
    if (!g_sms_dbus_conn || g_dbus_connection_is_closed(g_sms_dbus_conn)) {
        printf("[SMS] D-Bus连接无效，尝试重新连接...\n");
        GError *error = NULL;
        g_sms_dbus_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
        if (g_sms_dbus_conn) {
            printf("[SMS] D-Bus重新连接成功\n");
            g_signal_connect(g_sms_dbus_conn, "closed", G_CALLBACK(on_dbus_connection_closed), NULL);
            subscribe_sms_signal();
        } else {
            printf("[SMS] D-Bus重新连接失败: %s\n", error ? error->message : "未知错误");
            if (error) g_error_free(error);
        }
        return;
    }
    
    /* 检查信号订阅是否丢失 */
    if (g_signal_subscription_id == 0) {
        printf("[SMS] 检测到信号订阅丢失，重新订阅...\n");
        subscribe_sms_signal();
    }
}

/* 获取短信接收修复开关状态 */
int sms_get_fix_enabled(void) {
    const char *sql = "SELECT sms_fix_enabled FROM sms_config WHERE id = 1;";
    return db_query_int(sql, 0);  /* 默认关闭 */
}

/* 设置短信接收修复开关 */
int sms_set_fix_enabled(int enabled) {
    extern int execute_at(const char *command, char **result);
    char sql[256];
    char *at_result = NULL;
    
    /* 发送AT命令 */
    const char *at_cmd = enabled ? "AT+CNMI=3,2,0,1,0" : "AT+CNMI=3,1,0,1,0";
    printf("[SMS] 发送AT命令: %s\n", at_cmd);
    
    if (execute_at(at_cmd, &at_result) != 0) {
        printf("[SMS] AT命令执行失败\n");
        if (at_result) g_free(at_result);
        return -1;
    }
    
    printf("[SMS] AT命令执行成功: %s\n", at_result ? at_result : "OK");
    if (at_result) g_free(at_result);
    
    /* 保存到数据库 */
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO sms_config (id, max_count, max_sent_count, sms_fix_enabled) "
        "VALUES (1, %d, %d, %d);", 
        g_max_sms_count, g_max_sent_count, enabled ? 1 : 0);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    return ret;
}

/* 初始化时应用短信修复设置 */
static void apply_sms_fix_on_init(void) {
    extern int execute_at(const char *command, char **result);
    
    int enabled = sms_get_fix_enabled();
    printf("[SMS] 短信接收修复开关状态: %s\n", enabled ? "开启" : "关闭");
    
    if (enabled) {
        char *at_result = NULL;
        printf("[SMS] 开机应用短信修复AT命令: AT+CNMI=3,2,0,1,0\n");
        
        if (execute_at("AT+CNMI=3,2,0,1,0", &at_result) == 0) {
            printf("[SMS] AT命令执行成功\n");
        } else {
            printf("[SMS] AT命令执行失败\n");
        }
        
        if (at_result) g_free(at_result);
    }
}

/* 保存发送记录到数据库 */
static int save_sent_sms_to_db(const char *recipient, const char *content, time_t timestamp, const char *status) {
    char sql[2048];
    char escaped_content[1024];
    
    size_t j = 0;
    for (size_t i = 0; content[i] && j < sizeof(escaped_content) - 2; i++) {
        if (content[i] == '\'') {
            escaped_content[j++] = '\'';
            escaped_content[j++] = '\'';
        } else {
            escaped_content[j++] = content[i];
        }
    }
    escaped_content[j] = '\0';
    
    snprintf(sql, sizeof(sql),
        "INSERT INTO sent_sms (recipient, content, timestamp, status) VALUES ('%s', '%s', %ld, '%s');",
        recipient, escaped_content, (long)timestamp, status);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    /* 清理超出限制的旧发送记录 */
    if (ret == 0) {
        snprintf(sql, sizeof(sql),
            "DELETE FROM sent_sms WHERE id NOT IN (SELECT id FROM sent_sms ORDER BY id DESC LIMIT %d);",
            g_max_sent_count);
        pthread_mutex_lock(&g_sms_mutex);
        db_execute(sql);
        pthread_mutex_unlock(&g_sms_mutex);
    }
    
    return ret;
}

/* 删除发送记录 */
int sms_delete_sent(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM sent_sms WHERE id = %d;", id);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    return ret;
}

/* 获取发送记录列表 - 使用hex编码避免特殊字符问题，兼容无JSON扩展的SQLite */
int sms_get_sent_list(SentSmsMessage *messages, int max_count) {
    char sql[512];
    char *output = NULL;
    
    if (!messages || max_count <= 0) return -1;
    
    /* 分配大缓冲区 */
    output = (char *)malloc(256 * 1024);
    if (!output) return -1;
    
    /* 使用hex编码content字段，用|分隔，每行一条记录 */
    snprintf(sql, sizeof(sql),
        "SELECT id || '|' || recipient || '|' || hex(content) || '|' || timestamp || '|' || status FROM sent_sms ORDER BY id DESC LIMIT %d;",
        max_count);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_query_string(sql, output, 256 * 1024);
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret != 0 || strlen(output) == 0) {
        printf("[SMS] 获取发送记录列表失败或为空\n");
        free(output);
        return 0;
    }
    
    /* 解析输出 - 格式: id|recipient|hex_content|timestamp|status\n */
    int count = 0;
    char *line = output;
    char *next_line;
    
    while (line && *line && count < max_count) {
        /* 找到行尾 */
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }
        
        /* 跳过空行 */
        if (strlen(line) == 0) {
            line = next_line;
            continue;
        }
        
        /* 解析字段: id|recipient|hex_content|timestamp|status */
        char *fields[5] = {NULL};
        int field_count = 0;
        char *p = line;
        char *field_start = p;
        
        while (*p && field_count < 5) {
            if (*p == '|') {
                *p = '\0';
                fields[field_count++] = field_start;
                field_start = p + 1;
            }
            p++;
        }
        if (field_count < 5 && field_start) {
            fields[field_count++] = field_start;
        }
        
        if (field_count >= 5) {
            messages[count].id = atoi(fields[0]);
            strncpy(messages[count].recipient, fields[1], sizeof(messages[count].recipient) - 1);
            messages[count].recipient[sizeof(messages[count].recipient) - 1] = '\0';
            
            /* hex解码content */
            hex_decode(fields[2], messages[count].content, sizeof(messages[count].content));
            
            messages[count].timestamp = (time_t)atol(fields[3]);
            strncpy(messages[count].status, fields[4], sizeof(messages[count].status) - 1);
            messages[count].status[sizeof(messages[count].status) - 1] = '\0';
            count++;
        }
        
        line = next_line;
    }
    
    free(output);
    printf("[SMS] 获取到 %d 条发送记录\n", count);
    return count;
}

/* 获取最大存储数量 */
int sms_get_max_count(void) {
    return g_max_sms_count;
}

/* 获取发送记录最大存储数量 */
int sms_get_max_sent_count(void) {
    return g_max_sent_count;
}

/* 加载配置 */
static void load_sms_config(void) {
    char output[128];
    const char *sql = "SELECT max_count || '|' || max_sent_count FROM sms_config WHERE id = 1;";
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_query_string(sql, output, sizeof(output));
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret == 0 && strlen(output) > 0) {
        char *sep = strchr(output, '|');
        if (sep) {
            *sep = '\0';
            int mc = atoi(output);
            int msc = atoi(sep + 1);
            if (mc > 0) g_max_sms_count = mc;
            if (msc > 0) g_max_sent_count = msc;
        }
    }
    
    printf("[SMS] 配置加载完成: 收件箱最大=%d, 发件箱最大=%d\n", g_max_sms_count, g_max_sent_count);
}

/* 设置最大存储数量 */
int sms_set_max_count(int count) {
    if (count < 10 || count > 150) {
        printf("最大存储数量必须在10-150之间\n");
        return -1;
    }
    
    char sql[256];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO sms_config (id, max_count, max_sent_count) VALUES (1, %d, %d);", 
        count, g_max_sent_count);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret == 0) {
        g_max_sms_count = count;
    }
    
    return ret;
}

/* 设置发送记录最大存储数量 */
int sms_set_max_sent_count(int count) {
    if (count < 1 || count > 50) {
        printf("发送记录最大存储数量必须在1-50之间\n");
        return -1;
    }
    
    char sql[256];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO sms_config (id, max_count, max_sent_count) VALUES (1, %d, %d);", 
        g_max_sms_count, count);
    
    pthread_mutex_lock(&g_sms_mutex);
    int ret = db_execute(sql);
    pthread_mutex_unlock(&g_sms_mutex);
    
    if (ret == 0) {
        g_max_sent_count = count;
    }
    
    return ret;
}

