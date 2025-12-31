/**
 * @file usb_mode.c
 * @brief USB模式切换实现
 * 
 * 支持三种USB网络模式：CDC-NCM, CDC-ECM, RNDIS
 * 临时模式写入 /mnt/data/mode_tmp.cfg
 * 永久模式写入 /mnt/data/mode.cfg 并删除临时文件
 * 
 * 热切换功能通过 configfs 实现，无需重启
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "mongoose.h"
#include "usb_mode.h"
#include "http_utils.h"
#include "json_builder.h"

/* USB 模式配置结构 */
typedef struct {
    const char *vid;
    const char *pid;
    const char *configuration;
    const char *pamu3_protocol;  /* NULL 表示不设置 */
    const char *functions;
    const char *bcd_device;
    int usb_share_enable;
} UsbModeConfig;

/* 模式配置表 */
static const UsbModeConfig usb_mode_configs[] = {
    {0}, /* 占位，索引从1开始 */
    /* 1: NCM */
    { "0x1782", "0x4040", "ncm", "NCM", "ncm.gs0", "0x0404", 0 },
    /* 2: ECM */
    { "0x1782", "0x4039", "ecm", NULL, "ecm.gs0", "0x0404", 0 },
    /* 3: RNDIS */
    { "0x1782", "0x4038", "rndis", "RNDIS", "rndis.gs4", "0x0404", 1 },
};

/* 获取模式名称 */
const char* usb_mode_name(int mode) {
    switch (mode) {
        case USB_MODE_CDC_NCM: return "cdc_ncm";
        case USB_MODE_CDC_ECM: return "cdc_ecm";
        case USB_MODE_RNDIS:   return "rndis";
        default: return "unknown";
    }
}

/* 模式名称转数值 */
static int usb_mode_from_name(const char *name) {
    if (strcmp(name, "cdc_ncm") == 0) return USB_MODE_CDC_NCM;
    if (strcmp(name, "cdc_ecm") == 0) return USB_MODE_CDC_ECM;
    if (strcmp(name, "rndis") == 0)   return USB_MODE_RNDIS;
    return -1;
}

/* 读取配置文件中的模式值 */
static int read_mode_from_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    int mode = -1;
    if (fscanf(f, "%d", &mode) != 1) {
        mode = -1;
    }
    fclose(f);
    return mode;
}

/* 写入模式值到配置文件 */
static int write_mode_to_file(const char *path, int mode) {
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[usb_mode] 无法写入文件: %s\n", path);
        return -1;
    }
    fprintf(f, "%d", mode);
    fclose(f);
    return 0;
}

/* 获取当前USB模式 */
int usb_mode_get(void) {
    /* 优先读取临时配置 */
    int mode = read_mode_from_file(USB_MODE_TMP_CFG_PATH);
    if (mode > 0) return mode;
    
    /* 读取永久配置 */
    mode = read_mode_from_file(USB_MODE_CFG_PATH);
    return mode;
}

/* 设置USB模式 */
int usb_mode_set(int mode, int permanent) {
    if (mode < USB_MODE_CDC_NCM || mode > USB_MODE_RNDIS) {
        printf("[usb_mode] 无效的模式值: %d\n", mode);
        return -1;
    }
    
    if (permanent) {
        /* 永久模式：写入永久配置，删除临时配置 */
        if (write_mode_to_file(USB_MODE_CFG_PATH, mode) != 0) {
            return -1;
        }
        unlink(USB_MODE_TMP_CFG_PATH);
        printf("[usb_mode] 永久设置模式: %s (%d)\n", usb_mode_name(mode), mode);
    } else {
        /* 临时模式：写入临时配置 */
        if (write_mode_to_file(USB_MODE_TMP_CFG_PATH, mode) != 0) {
            return -1;
        }
        printf("[usb_mode] 临时设置模式: %s (%d)\n", usb_mode_name(mode), mode);
    }
    
    return 0;
}

/* GET /api/usb/mode - 获取当前USB模式 */
void handle_usb_mode_get(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);
    
    /* 优先从配置文件读取 */
    int mode = usb_mode_get();
    
    /* 如果配置文件不存在，尝试从硬件读取 */
    if (mode <= 0) {
        mode = usb_mode_get_current_hardware();
    }
    
    /* 如果仍然无法获取，默认为 RNDIS (3) */
    if (mode <= 0) {
        mode = USB_MODE_RNDIS;
    }
    
    const char *mode_name = usb_mode_name(mode);
    
    /* 检查是否有临时配置 */
    int is_temporary = (access(USB_MODE_TMP_CFG_PATH, F_OK) == 0);
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_str(j, "mode", mode_name);
    json_add_int(j, "mode_value", mode);
    json_add_bool(j, "is_temporary", is_temporary);
    json_obj_close(j);
    json_obj_close(j);
    
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/usb/mode - 设置USB模式 */
void handle_usb_mode_set(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    char mode_str[32] = {0};
    int permanent = 0;
    int bval = 0;
    
    /* 解析JSON参数 */
    char *mode_val = mg_json_get_str(hm->body, "$.mode");
    if (mode_val) {
        strncpy(mode_str, mode_val, sizeof(mode_str) - 1);
        free(mode_val);
    }
    
    if (mg_json_get_bool(hm->body, "$.permanent", &bval)) {
        permanent = bval;
    }
    
    /* 验证模式 */
    if (strlen(mode_str) == 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "mode参数不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }
    
    int mode = usb_mode_from_name(mode_str);
    if (mode < 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "无效的模式，支持: cdc_ncm, cdc_ecm, rndis");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }
    
    /* 设置模式 */
    if (usb_mode_set(mode, permanent) != 0) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "设置模式失败");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }
    
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_str(j, "mode", mode_str);
    json_add_bool(j, "permanent", permanent);
    json_add_str(j, "message", "设置成功，重启后生效");
    json_obj_close(j);
    json_obj_close(j);
    
    HTTP_OK_FREE(c, json_finish(j));
}

/* ==================== USB 热切换实现 ==================== */

/* 写入 sysfs 文件 */
static int write_sysfs(const char *path, const char *value) {
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[usb_mode] 无法写入 %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(f, "%s", value);
    fclose(f);
    return 0;
}

/* 读取 sysfs 文件 */
static int read_sysfs(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (fgets(buf, size, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    /* 去除换行符 */
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

/* 执行系统命令 */
static int run_cmd(const char *cmd) {
    printf("[usb_mode] 执行: %s\n", cmd);
    return system(cmd);
}

/* 停止 adbd 服务 */
static void stop_adbd(void) {
    run_cmd("killall adbd 2>/dev/null");
    usleep(100000); /* 100ms */
}

/* 启动 adbd 服务 */
static void start_adbd(void) {
    run_cmd("/usr/bin/adbd-init &");
    usleep(500000); /* 500ms 等待 functionfs 挂载 */
}

/* 获取 UDC 名称 */
static const char* get_udc_name(void) {
    static char udc_name[64] = {0};
    DIR *dir = opendir("/sys/class/udc");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                strncpy(udc_name, entry->d_name, sizeof(udc_name) - 1);
                break;
            }
        }
        closedir(dir);
    }
    return strlen(udc_name) > 0 ? udc_name : DEFAULT_UDC;
}

/* 删除 configs/b.1 下的所有符号链接 */
static void remove_function_links(void) {
    DIR *dir = opendir(USB_CONFIG_PATH);
    if (!dir) return;
    
    struct dirent *entry;
    char path[256];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", USB_CONFIG_PATH, entry->d_name);
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) {
            unlink(path);
            printf("[usb_mode] 删除链接: %s\n", path);
        }
    }
    closedir(dir);
}

/* 删除 functions 目录下的功能 */
static void remove_cdc_functions(void) {
    const char *funcs[] = {
        "ncm.gs0", "ncm.gs1", "ncm.gs2", "ncm.gs3",
        "ecm.gs0", "ecm.gs1", "ecm.gs2", "ecm.gs3",
        "rndis.gs4", "mbim.gs0", NULL
    };
    char path[256];
    for (int i = 0; funcs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", USB_FUNCTIONS_PATH, funcs[i]);
        rmdir(path); /* 只能删除空目录 */
    }
}

/* 创建 gser/vser 功能目录 */
static void create_gser_functions(void) {
    const char *gsers[] = {
        "vser.gs0", "ffs.adb",
        "gser.gs0", "gser.gs1", "gser.gs2", "gser.gs3",
        "gser.gs4", "gser.gs5", "gser.gs6", "gser.gs7", NULL
    };
    char path[256];
    for (int i = 0; gsers[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", USB_FUNCTIONS_PATH, gsers[i]);
        if (access(path, F_OK) != 0) {
            mkdir(path, 0755);
        }
    }
}

/* 等待 functionfs 挂载完成 */
static int wait_for_functionfs(void) {
    const char *ffs_ep0 = "/dev/usb-ffs/adb/ep0";
    for (int i = 0; i < 50; i++) {  /* 最多等待 5 秒 */
        if (access(ffs_ep0, F_OK) == 0) {
            usleep(200000);  /* 额外等待 adbd 打开 ep0 */
            return 0;
        }
        usleep(100000);  /* 100ms */
    }
    printf("[usb_mode] 警告: functionfs 挂载超时，继续执行\n");
    return -1;  /* 超时但继续 */
}

/* 启用 SFP 硬件转发加速 */
static void enable_sfp_acceleration(void) {
    write_sysfs("/proc/net/sfp/enable", "1");
    write_sysfs("/proc/net/sfp/tether_scheme", "1");
}

/* 创建功能目录 */
static int create_function_dir(const char *func_name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", USB_FUNCTIONS_PATH, func_name);
    if (access(path, F_OK) != 0) {
        if (mkdir(path, 0755) != 0) {
            printf("[usb_mode] 创建功能目录失败: %s\n", path);
            return -1;
        }
    }
    return 0;
}

/* 创建符号链接 */
static int create_function_link(const char *func_name, const char *link_name) {
    char target[256], link_path[256];
    snprintf(target, sizeof(target), "%s/%s", USB_FUNCTIONS_PATH, func_name);
    snprintf(link_path, sizeof(link_path), "%s/%s", USB_CONFIG_PATH, link_name);
    
    if (symlink(target, link_path) != 0) {
        printf("[usb_mode] 创建链接失败: %s -> %s\n", link_path, target);
        return -1;
    }
    printf("[usb_mode] 创建链接: %s -> %s\n", link_path, target);
    return 0;
}

/* 配置 USB 网络接口 */
static void configure_usb_network(void) {
    /* 等待接口出现 */
    usleep(500000);
    
    /* 1. 配置 connman gadget tethering */
    run_cmd("connmanctl tether gadget off 2>/dev/null");
    usleep(100000);
    run_cmd("connmanctl disable gadget 2>/dev/null");
    usleep(200000);
    run_cmd("connmanctl enable gadget 2>/dev/null");
    usleep(100000);
    run_cmd("connmanctl tether gadget on 2>/dev/null");
    usleep(300000);
    
    /* 2. 查找并配置 USB 网络接口 */
    const char *ifaces[] = {"usb0", "rndis0", NULL};
    char cmd[256];
    
    for (int retry = 0; retry < 5; retry++) {
        for (int i = 0; ifaces[i]; i++) {
            char path[128];
            snprintf(path, sizeof(path), "/sys/class/net/%s", ifaces[i]);
            if (access(path, F_OK) == 0) {
                /* 设置 IP 地址和子网掩码 */
                snprintf(cmd, sizeof(cmd), "ifconfig %s %s netmask 255.255.255.0", 
                         ifaces[i], USB_INTERFACE_IP);
                run_cmd(cmd);
                
                /* 设置 MAC 地址 */
                snprintf(cmd, sizeof(cmd), "ifconfig %s hw ether %s", 
                         ifaces[i], USB_INTERFACE_MAC);
                run_cmd(cmd);
                
                /* 启动接口 */
                snprintf(cmd, sizeof(cmd), "ip link set dev %s up", ifaces[i]);
                run_cmd(cmd);
                
                /* 配置 iptables NAT */
                run_cmd("iptables -t nat -A POSTROUTING -o rmnet_data0 -j MASQUERADE 2>/dev/null");
                snprintf(cmd, sizeof(cmd), "iptables -A FORWARD -i %s -j ACCEPT 2>/dev/null", ifaces[i]);
                run_cmd(cmd);
                
                goto network_done;
            }
        }
        usleep(1000000);  /* 等待 1 秒后重试 */
    }
    
network_done:
    /* 3. 关闭 sipa_usb0 接口（避免冲突） */
    run_cmd("ifconfig sipa_usb0 down 2>/dev/null");
    
    /* 4. 启用 SFP 硬件转发加速 */
    enable_sfp_acceleration();
    
    /* 5. 标记配置完成 */
    run_cmd("touch /tmp/sipa_usb0_ok");
}

/* 创建多功能模式的符号链接 (f1-f9) */
static int create_multi_function_links(const UsbModeConfig *cfg) {
    /* f1: 主网络功能 (ncm/ecm/rndis) */
    if (create_function_link(cfg->functions, "f1") != 0) return -1;
    
    /* f2: gser.gs2 (AT 指令通道) */
    create_function_link("gser.gs2", "f2");
    
    /* f3: gser.gs0 (诊断通道) */
    create_function_link("gser.gs0", "f3");
    
    /* f4: vser.gs0 (虚拟串口/IQ 日志) */
    create_function_link("vser.gs0", "f4");
    
    /* f5: gser.gs3 */
    create_function_link("gser.gs3", "f5");
    
    /* f6: ffs.adb (Android Debug Bridge) */
    create_function_link("ffs.adb", "f6");
    
    /* f7-f9: 更多串口通道 */
    create_function_link("gser.gs4", "f7");
    create_function_link("gser.gs5", "f8");
    create_function_link("gser.gs6", "f9");
    
    return 0;
}

/* USB 模式热切换 */
int usb_mode_switch_advanced(int mode) {
    if (mode < 1 || mode > 3) {
        printf("[usb_mode] 无效模式: %d\n", mode);
        return -1;
    }
    
    const UsbModeConfig *cfg = &usb_mode_configs[mode];
    /* 提前缓存 UDC 名称，避免禁用后读取为空 */
    const char *udc_name = get_udc_name();
    char path[256];
    
    printf("[usb_mode] 开始热切换到模式 %d (%s)\n", mode, cfg->configuration);
    
    /* 1. 停止 adbd */
    stop_adbd();
    
    /* 2. 禁用 UDC */
    write_sysfs(USB_UDC_PATH, "none");
    usleep(100000);
    
    /* 3. 删除所有链接和功能 */
    remove_function_links();
    remove_cdc_functions();
    
    /* 4. 设置 IPA 协议 */
    if (cfg->pamu3_protocol && access(PAMU3_PROTOCOL_PATH, F_OK) == 0) {
        write_sysfs(PAMU3_PROTOCOL_PATH, cfg->pamu3_protocol);
    }
    
    /* 5. 设置 max_dl_pkts (下行包批量数) */
    write_sysfs("/sys/devices/platform/soc/soc:ipa/2b300000.pamu3/max_dl_pkts", "7");
    
    /* 6. 设置 VID/PID */
    snprintf(path, sizeof(path), "%s/idVendor", USB_GADGET_PATH);
    write_sysfs(path, cfg->vid);
    snprintf(path, sizeof(path), "%s/idProduct", USB_GADGET_PATH);
    write_sysfs(path, cfg->pid);
    snprintf(path, sizeof(path), "%s/bcdDevice", USB_GADGET_PATH);
    write_sysfs(path, cfg->bcd_device);
    snprintf(path, sizeof(path), "%s/bDeviceClass", USB_GADGET_PATH);
    write_sysfs(path, "0");
    
    /* 7. 设置配置描述符 */
    snprintf(path, sizeof(path), "%s/strings/0x409/configuration", USB_CONFIG_PATH);
    write_sysfs(path, cfg->configuration);
    snprintf(path, sizeof(path), "%s/MaxPower", USB_CONFIG_PATH);
    write_sysfs(path, "500");
    snprintf(path, sizeof(path), "%s/bmAttributes", USB_CONFIG_PATH);
    write_sysfs(path, "0xc0");
    
    /* 8. 创建主功能目录 */
    if (create_function_dir(cfg->functions) != 0) {
        return -2;
    }
    
    /* 9. 创建 gser/vser 功能目录 */
    create_gser_functions();
    
    /* 10. 设置 MAC 地址 */
    snprintf(path, sizeof(path), "%s/%s/dev_addr", USB_FUNCTIONS_PATH, cfg->functions);
    if (access(path, F_OK) == 0) {
        write_sysfs(path, "cc:e8:ac:c0:00:00");
    }
    snprintf(path, sizeof(path), "%s/%s/host_addr", USB_FUNCTIONS_PATH, cfg->functions);
    if (access(path, F_OK) == 0) {
        write_sysfs(path, "cc:e8:ac:c0:00:01");
    }
    
    /* 11. 创建多功能链接 (f1-f9) */
    if (create_multi_function_links(cfg) != 0) {
        return -3;
    }
    
    /* 12. 启动 adbd */
    start_adbd();
    
    /* 13. 等待 functionfs 挂载完成 */
    wait_for_functionfs();
    
    /* 14. 设置日志传输 */
    write_sysfs("/sys/module/slog_bridge/parameters/log_transport", "1");
    
    /* 15. 启用 UDC */
    write_sysfs(USB_UDC_PATH, udc_name);
    
    /* 16. 等待 USB 枚举 */
    usleep(1000000); /* 1s */
    
    /* 17. 配置网络接口 */
    configure_usb_network();
    
    printf("[usb_mode] 热切换完成: %s\n", cfg->configuration);
    return 0;
}

/* 获取当前硬件 USB 模式 */
int usb_mode_get_current_hardware(void) {
    char vid[32] = {0}, pid[32] = {0};
    char path[256];
    
    snprintf(path, sizeof(path), "%s/idVendor", USB_GADGET_PATH);
    if (read_sysfs(path, vid, sizeof(vid)) != 0) return -1;
    
    snprintf(path, sizeof(path), "%s/idProduct", USB_GADGET_PATH);
    if (read_sysfs(path, pid, sizeof(pid)) != 0) return -1;
    
    /* 根据 VID:PID 判断模式 */
    if (strcmp(vid, "0x1782") == 0) {
        if (strcmp(pid, "0x4040") == 0) return 1; /* NCM */
        if (strcmp(pid, "0x4039") == 0) return 2; /* ECM */
        if (strcmp(pid, "0x4038") == 0) return 3; /* RNDIS */
    }
    return -1;
}

/* POST /api/usb-advance - USB 热切换 */
void handle_usb_advance(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);
    
    double mode_val = 0;
    if (!mg_json_get_num(hm->body, "$.mode", &mode_val)) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "mode参数不能为空");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }
    
    int mode = (int)mode_val;
    if (mode < 1 || mode > 3) {
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_int(j, "Code", 1);
        json_add_str(j, "Error", "无效模式，支持: 1=NCM, 2=ECM, 3=RNDIS");
        json_add_null(j, "Data");
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
        return;
    }
    
    /* 先发送成功响应，再执行USB切换
     * 因为USB切换过程中会断开USB连接，如果先切换再响应，
     * HTTP响应无法发送到客户端，前端会显示失败
     */
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_str(j, "mode", usb_mode_name(mode));
    json_add_int(j, "mode_value", mode);
    json_add_str(j, "message", "USB模式切换中，请稍候...");
    json_obj_close(j);
    json_obj_close(j);
    
    HTTP_OK_FREE(c, json_finish(j));
    c->is_draining = 1;  /* 标记连接即将关闭，确保响应发送完成 */
    
    /* 等待响应发送完成 */
    usleep(200000);  /* 200ms */
    
    /* 执行USB热切换 */
    int ret = usb_mode_switch_advanced(mode);
    if (ret != 0) {
        printf("[usb_mode] 热切换失败: %d\n", ret);
    }
}
