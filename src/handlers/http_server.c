/**
 * @file http_server.c
 * @brief HTTP 服务器实现 (对应 Go: main.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <glib.h>
#include "mongoose.h"
#include "http_server.h"
#include "dbus_core.h"
#include "handlers.h"
#include "advanced.h"
#include "traffic.h"
#include "reboot.h"
#include "charge.h"
#include "sms.h"
#include "usb_mode.h"
#include "http_utils.h"

/* 嵌入式文件系统声明 (packed_fs.c) */
extern int serve_packed_file(struct mg_connection *c, struct mg_http_message *hm);

/* 全局变量 */
static struct mg_mgr g_mgr;
static volatile int g_running = 0;

/* 信号处理 */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}


/* HTTP 事件处理函数 */
static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        char uri[256] = {0};
        size_t uri_len = hm->uri.len < sizeof(uri) - 1 ? hm->uri.len : sizeof(uri) - 1;
        memcpy(uri, hm->uri.buf, uri_len);

        /* 静态文件处理 */
        if (hm->uri.len < 5 || memcmp(hm->uri.buf, "/api/", 5) != 0) {
            if (serve_packed_file(c, hm)) {
                return;  /* 静态文件已处理 */
            }
        }

        /* API 路由 */
        if (mg_match(hm->uri, mg_str("/api/info"), NULL)) {
            handle_info(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/at"), NULL)) {
            handle_execute_at(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/set_network"), NULL)) {
            handle_set_network(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/switch"), NULL)) {
            handle_switch(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/airplane_mode"), NULL)) {
            handle_airplane_mode(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/device_control"), NULL)) {
            handle_device_control(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/clear_cache"), NULL)) {
            handle_clear_cache(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/current_band"), NULL)) {
            handle_get_current_band(c, hm);
        }
        /* 高级网络 API */
        else if (mg_match(hm->uri, mg_str("/api/bands"), NULL)) {
            handle_get_bands(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/lock_bands"), NULL)) {
            handle_lock_bands(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/unlock_bands"), NULL)) {
            handle_unlock_bands(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/cells"), NULL)) {
            handle_get_cells(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/lock_cell"), NULL)) {
            handle_lock_cell(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/unlock_cell"), NULL)) {
            handle_unlock_cell(c, hm);
        }
        /* 流量统计 API */
        else if (mg_match(hm->uri, mg_str("/api/get/Total"), NULL)) {
            handle_get_traffic_total(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/get/set"), NULL)) {
            handle_get_traffic_config(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/set/total"), NULL)) {
            handle_set_traffic_limit(c, hm);
        }
        /* 系统时间 API */
        else if (mg_match(hm->uri, mg_str("/api/get/time"), NULL)) {
            handle_get_system_time(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/set/time"), NULL)) {
            handle_set_system_time(c, hm);
        }
        /* 定时重启 API */
        else if (mg_match(hm->uri, mg_str("/api/get/first-reboot"), NULL)) {
            handle_get_first_reboot(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/set/reboot"), NULL)) {
            handle_set_reboot(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/claen/cron"), NULL)) {
            handle_clear_cron(c, hm);
        }
        /* 充电控制 API */
        else if (mg_match(hm->uri, mg_str("/api/charge/config"), NULL)) {
            handle_charge_config(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/charge/on"), NULL)) {
            handle_charge_on(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/charge/off"), NULL)) {
            handle_charge_off(c, hm);
        }
        /* 短信 API */
        else if (mg_match(hm->uri, mg_str("/api/sms"), NULL)) {
            handle_sms_list(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/send"), NULL)) {
            handle_sms_send(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/sent"), NULL)) {
            handle_sms_sent_list(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/sent/*"), NULL)) {
            handle_sms_sent_delete(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/config"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_sms_config_get(c, hm);
            } else {
                handle_sms_config_save(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/webhook"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_sms_webhook_get(c, hm);
            } else {
                handle_sms_webhook_save(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/webhook/test"), NULL)) {
            handle_sms_webhook_test(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/fix"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_sms_fix_get(c, hm);
            } else {
                handle_sms_fix_set(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/sms/*"), NULL)) {
            handle_sms_delete(c, hm);
        }
        /* OTA更新 API */
        else if (mg_match(hm->uri, mg_str("/api/update/version"), NULL)) {
            handle_update_version(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update/upload"), NULL)) {
            handle_update_upload(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update/download"), NULL)) {
            handle_update_download(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update/extract"), NULL)) {
            handle_update_extract(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update/install"), NULL)) {
            handle_update_install(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update/check"), NULL)) {
            handle_update_check(c, hm);
        }
        /* USB模式切换 API */
        else if (mg_match(hm->uri, mg_str("/api/usb/mode"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_usb_mode_get(c, hm);
            } else {
                handle_usb_mode_set(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/usb-advance"), NULL)) {
            handle_usb_advance(c, hm);
        }
        /* 数据连接和漫游 API */
        else if (mg_match(hm->uri, mg_str("/api/data"), NULL)) {
            handle_data_status(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/roaming"), NULL)) {
            handle_roaming_status(c, hm);
        }
        /* APN 管理 API */
        else if (mg_match(hm->uri, mg_str("/api/apn"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_apn_list(c, hm);
            } else {
                handle_apn_set(c, hm);
            }
        }
        /* 插件管理 API */
        else if (mg_match(hm->uri, mg_str("/api/shell"), NULL)) {
            handle_shell_execute(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/plugins/all"), NULL)) {
            handle_plugin_delete_all(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/plugins"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_plugin_list(c, hm);
            } else {
                handle_plugin_upload(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/plugins/*"), NULL)) {
            handle_plugin_delete(c, hm);
        }
        /* 脚本管理 API */
        else if (mg_match(hm->uri, mg_str("/api/scripts"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_script_list(c, hm);
            } else {
                handle_script_upload(c, hm);
            }
        }
        else if (mg_match(hm->uri, mg_str("/api/scripts/*"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "PUT", 3) == 0) {
                handle_script_update(c, hm);
            } else {
                handle_script_delete(c, hm);
            }
        }
        /* 插件存储 API */
        else if (mg_match(hm->uri, mg_str("/api/plugins/storage/*"), NULL)) {
            if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
                handle_plugin_storage_get(c, hm);
            } else if (hm->method.len == 4 && memcmp(hm->method.buf, "POST", 4) == 0) {
                handle_plugin_storage_set(c, hm);
            } else if (hm->method.len == 6 && memcmp(hm->method.buf, "DELETE", 6) == 0) {
                handle_plugin_storage_delete(c, hm);
            } else {
                HTTP_ERROR(c, 405, "Method not allowed");
            }
        }
        /* 未知 API 路由 */
        else {
            HTTP_ERROR(c, 404, "Endpoint not found");
        }
    }
}


int http_server_start(const char *port) {
    char listen_addr[64];

    /* 初始化 D-Bus */
    if (init_dbus() != 0) {
        printf("警告: D-Bus 初始化失败 (高级网络功能将不可用)\n");
    }

    /* 初始化流量统计 */
    init_traffic();

    /* 初始化充电控制 */
    init_charge();

    /* 初始化短信模块 */
    if (sms_init("6677.db") != 0) {
        printf("警告: 短信模块初始化失败\n");
    }

    /* 初始化 mongoose */
    mg_mgr_init(&g_mgr);

    /* 构建监听地址 */
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%s", port);

    /* 创建 HTTP 监听器 */
    if (mg_http_listen(&g_mgr, listen_addr, http_handler, NULL) == NULL) {
        printf("无法监听端口 %s\n", port);
        mg_mgr_free(&g_mgr);
        return -1;
    }

    printf("Server starting on :%s\n", port);
    g_running = 1;

    /* 设置信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    return 0;
}

void http_server_stop(void) {
    g_running = 0;
    mg_mgr_free(&g_mgr);
    sms_deinit();
    close_dbus();
    printf("服务器已停止\n");
}

void http_server_run(void) {
    GMainContext *context = g_main_context_default();
    static int maintenance_counter = 0;
    
    while (g_running) {
        /* 处理GLib/D-Bus事件 - 优先处理，确保信号不丢失 */
        while (g_main_context_pending(context)) {
            g_main_context_iteration(context, FALSE);
        }
        
        /* 处理mongoose事件 - 减少超时时间以更快响应D-Bus信号 */
        mg_mgr_poll(&g_mgr, 10);  /* 10ms超时 */
        
        /* 每30秒执行一次短信模块维护（检查D-Bus连接） */
        if (++maintenance_counter >= 3000) {  /* 3000 * 10ms = 30秒 */
            maintenance_counter = 0;
            sms_maintenance();
        }
    }
}
