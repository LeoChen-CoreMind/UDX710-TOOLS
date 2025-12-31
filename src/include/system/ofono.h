/**
 * @file ofono.h
 * @brief ofono D-Bus 接口封装
 */

#ifndef OFONO_H
#define OFONO_H

#include <gio/gio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ofono D-Bus 常量 */
#define OFONO_SERVICE           "org.ofono"
#define OFONO_RADIO_SETTINGS    "org.ofono.RadioSettings"
#define OFONO_TIMEOUT_MS        30000

/**
 * 初始化 D-Bus 连接
 * @return 成功返回非0，失败返回0
 */
int ofono_init(void);

/**
 * 检查 D-Bus 连接是否已初始化
 * @return 已初始化返回1，否则返回0
 */
int ofono_is_initialized(void);

/**
 * 关闭 D-Bus 连接
 */
void ofono_deinit(void);

/**
 * 获取网络模式
 * @param modem_path modem 路径，如 "/ril_0"
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @param timeout_ms 超时时间(毫秒)
 * @return 成功返回0，失败返回错误码
 */
int ofono_network_get_mode_sync(const char* modem_path, char* buffer, int size, int timeout_ms);

/**
 * 获取数据卡路径
 * @return 数据卡路径字符串，需要调用 g_free 释放，失败返回 NULL
 */
char* ofono_get_datacard(void);

/**
 * 设置网络模式
 * @param modem_path modem 路径
 * @param mode 网络模式索引 (0-10)
 * @param timeout_ms 超时时间
 * @return 成功返回0，失败返回错误码
 */
int ofono_network_set_mode_sync(const char* modem_path, int mode, int timeout_ms);

/**
 * 获取网络模式名称
 * @param mode 模式索引
 * @return 模式名称字符串
 */
const char* ofono_get_mode_name(int mode);

/**
 * 获取支持的网络模式数量
 */
int ofono_get_mode_count(void);

/**
 * 设置 modem 在线状态
 * @param modem_path modem 路径
 * @param online 1=在线, 0=离线
 * @param timeout_ms 超时时间
 * @return 成功返回0，失败返回错误码
 */
int ofono_modem_set_online(const char* modem_path, int online, int timeout_ms);

/**
 * 设置数据卡
 * @param modem_path modem 路径
 * @return 成功返回1，失败返回0
 */
int ofono_set_datacard(const char* modem_path);

/**
 * 获取信号强度
 * @param modem_path modem 路径
 * @param strength 输出信号强度百分比 (0-100)
 * @param dbm 输出信号强度 dBm 值
 * @param timeout_ms 超时时间
 * @return 成功返回0，失败返回错误码
 */
int ofono_network_get_signal_strength(const char* modem_path, int* strength, int* dbm, int timeout_ms);

/**
 * 获取数据连接状态
 * @param active 输出数据连接状态 (1=激活, 0=未激活)
 * @return 成功返回0，失败返回错误码
 */
int ofono_get_data_status(int *active);

/**
 * 设置数据连接状态
 * @param active 1=开启数据连接, 0=关闭数据连接
 * @return 成功返回0，失败返回错误码
 */
int ofono_set_data_status(int active);

/**
 * 获取漫游状态
 * @param roaming_allowed 输出漫游允许状态 (1=允许, 0=禁止)
 * @param is_roaming 输出当前是否漫游中 (1=漫游中, 0=非漫游)
 * @return 成功返回0，失败返回错误码
 */
int ofono_get_roaming_status(int *roaming_allowed, int *is_roaming);

/**
 * 设置漫游允许状态
 * @param allowed 1=允许漫游, 0=禁止漫游
 * @return 成功返回0，失败返回错误码
 */
int ofono_set_roaming_allowed(int allowed);

/* ==================== APN 管理 API ==================== */

#define MAX_APN_CONTEXTS 16
#define APN_STRING_SIZE 128

/**
 * APN Context 结构体
 */
typedef struct {
    char path[APN_STRING_SIZE];        /* D-Bus 路径 (如 /ril_0/context2) */
    char name[APN_STRING_SIZE];        /* 名称 */
    int active;                        /* 是否激活 */
    char apn[APN_STRING_SIZE];         /* APN 名称 (如 cbnet, cmnet) */
    char protocol[32];                 /* 协议: ip/ipv6/dual */
    char username[APN_STRING_SIZE];    /* 用户名 */
    char password[APN_STRING_SIZE];    /* 密码 */
    char auth_method[32];              /* 认证方式: none/pap/chap */
    char context_type[32];             /* 类型: internet/mms/ims */
} ApnContext;

/**
 * 获取所有 APN Context 列表
 * @param contexts 输出 context 数组
 * @param max_count 数组最大容量
 * @return 成功返回 context 数量，失败返回负数错误码
 */
int ofono_get_all_apn_contexts(ApnContext *contexts, int max_count);

/**
 * 设置 APN 单个属性
 * @param context_path context 的 D-Bus 路径
 * @param property 属性名
 * @param value 属性值
 * @return 成功返回0，失败返回错误码
 */
int ofono_set_apn_property(const char *context_path, const char *property, const char *value);

/**
 * 批量设置 APN 属性
 * @param context_path context 的 D-Bus 路径
 * @param apn APN 名称 (NULL 表示不修改)
 * @param protocol 协议 (NULL 表示不修改)
 * @param username 用户名 (NULL 表示不修改)
 * @param password 密码 (NULL 表示不修改)
 * @param auth_method 认证方式 (NULL 表示不修改)
 * @return 成功返回0，失败返回错误码
 */
int ofono_set_apn_properties(const char *context_path, 
                             const char *apn,
                             const char *protocol,
                             const char *username,
                             const char *password,
                             const char *auth_method);

/**
 * 获取当前服务小区的网络技术类型
 * 通过 NetworkMonitor.GetServingCellInformation 获取
 * @param tech 输出技术类型字符串 (如 "nr", "lte", "umts", "gsm")
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回错误码
 */
int ofono_get_serving_cell_tech(char *tech, int size);

/**
 * 获取当前服务小区信息（技术类型和频段）
 * 通过 NetworkMonitor.GetServingCellInformation 获取
 * @param tech 输出技术类型字符串 (如 "nr", "lte", "umts", "gsm")
 * @param tech_size 技术类型缓冲区大小
 * @param band 输出频段号 (如 41, 78)
 * @return 成功返回0，失败返回错误码
 */
int ofono_get_serving_cell_info(char *tech, int tech_size, int *band);

/* ==================== 数据连接 Watchdog API ==================== */

/**
 * 获取网络注册状态
 * @param status 输出状态字符串 (如 "registered", "roaming", "searching")
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回错误码
 */
int ofono_get_network_status(char *status, int size);

/**
 * 检查并恢复数据连接
 * 检查当前数据连接状态，如果APN已配置但未激活则自动激活
 * @param result 输出结果描述字符串
 * @param size 缓冲区大小
 * @return 成功返回0，失败返回错误码
 */
int ofono_check_and_restore_data(char *result, int size);

/**
 * 启动数据连接 Watchdog 线程
 * 后台定时检查数据连接状态，断开时自动重连
 * @param interval_secs 检查间隔（秒），默认10秒
 * @return 成功返回0，失败返回-1
 */
int ofono_start_data_watchdog(int interval_secs);

/**
 * 停止数据连接 Watchdog 线程
 */
void ofono_stop_data_watchdog(void);

/**
 * 检查 Watchdog 是否运行中
 * @return 运行中返回1，否则返回0
 * @deprecated 请使用 ofono_is_data_monitor_running()
 */
int ofono_is_watchdog_running(void);

/* ==================== 数据连接监听 API (DBus 信号驱动) ==================== */

/**
 * 启动数据连接监听（基于 DBus 信号）
 * 订阅 ConnectionContext.PropertyChanged 和 NetworkRegistration.PropertyChanged 信号
 * 当数据连接断开或网络注册成功时自动尝试恢复连接
 * @return 成功返回0，失败返回-1
 */
int ofono_start_data_monitor(void);

/**
 * 停止数据连接监听
 * 取消所有信号订阅和服务监控
 */
void ofono_stop_data_monitor(void);

/**
 * 检查数据连接监听是否运行中
 * @return 运行中返回1，否则返回0
 */
int ofono_is_data_monitor_running(void);

#ifdef __cplusplus
}
#endif

#endif /* OFONO_H */
