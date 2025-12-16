/**
 * @file plugin_storage.h
 * @brief 插件持久化存储接口定义
 */

#ifndef PLUGIN_STORAGE_H
#define PLUGIN_STORAGE_H

#include <stddef.h>

/* 存储目录和限制 */
#define PLUGIN_DATA_DIR "/home/root/6677/Plugins/data"
#define PLUGIN_STORAGE_MAX_SIZE 65536  /* 64KB */

/**
 * 确保数据存储目录存在
 * @return 0成功，-1失败
 */
int ensure_plugin_data_dir(void);

/**
 * 读取插件存储数据
 * @param plugin_name 插件名称（不含.js后缀）
 * @param json_output 输出缓冲区
 * @param size 缓冲区大小
 * @return 0成功，-1失败
 */
int plugin_storage_read(const char *plugin_name, char *json_output, size_t size);

/**
 * 写入插件存储数据
 * @param plugin_name 插件名称（不含.js后缀）
 * @param json_data JSON格式数据
 * @return 0成功，-1失败
 */
int plugin_storage_write(const char *plugin_name, const char *json_data);

/**
 * 删除插件存储数据
 * @param plugin_name 插件名称
 * @return 0成功，-1失败
 */
int plugin_storage_delete(const char *plugin_name);

#endif /* PLUGIN_STORAGE_H */
