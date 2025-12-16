/**
 * @file plugin_storage.c
 * @brief 插件持久化存储实现
 * 
 * 使用flock文件锁保证并发安全
 * 存储路径: /home/root/6677/Plugins/data/<插件名>.json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include "plugin_storage.h"

/* 验证插件名称安全性 */
static int is_valid_plugin_name(const char *name) {
    if (!name || strlen(name) == 0 || strlen(name) > 128) {
        return 0;
    }
    /* 禁止路径遍历 */
    if (strstr(name, "..") || strstr(name, "/") || strstr(name, "\\")) {
        return 0;
    }
    return 1;
}

/* 构建存储文件路径 */
static int build_storage_path(const char *plugin_name, char *path, size_t size) {
    if (!is_valid_plugin_name(plugin_name)) {
        return -1;
    }
    snprintf(path, size, "%s/%s.json", PLUGIN_DATA_DIR, plugin_name);
    return 0;
}

/* 确保数据存储目录存在 */
int ensure_plugin_data_dir(void) {
    struct stat st = {0};
    if (stat(PLUGIN_DATA_DIR, &st) == -1) {
        /* 递归创建目录 */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", PLUGIN_DATA_DIR);
        if (system(cmd) != 0) {
            return -1;
        }
    }
    return 0;
}

/* 读取插件存储数据 */
int plugin_storage_read(const char *plugin_name, char *json_output, size_t size) {
    if (!json_output || size == 0) {
        return -1;
    }
    
    char filepath[512];
    if (build_storage_path(plugin_name, filepath, sizeof(filepath)) != 0) {
        snprintf(json_output, size, "{}");
        return 0;
    }
    
    /* 检查文件是否存在 */
    struct stat st;
    if (stat(filepath, &st) == -1) {
        /* 文件不存在，返回空对象 */
        snprintf(json_output, size, "{}");
        return 0;
    }
    
    /* 打开文件 */
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        snprintf(json_output, size, "{}");
        return 0;
    }
    
    /* 获取共享锁（读锁） */
    if (flock(fd, LOCK_SH) == -1) {
        close(fd);
        snprintf(json_output, size, "{}");
        return -1;
    }
    
    /* 读取文件内容 */
    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        flock(fd, LOCK_UN);
        close(fd);
        snprintf(json_output, size, "{}");
        return -1;
    }
    
    size_t total = 0;
    char buffer[1024];
    json_output[0] = '\0';
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL && total < size - 1) {
        size_t len = strlen(buffer);
        if (total + len >= size - 1) {
            len = size - 1 - total;
        }
        memcpy(json_output + total, buffer, len);
        total += len;
    }
    json_output[total] = '\0';
    
    /* 释放锁并关闭文件 */
    flock(fd, LOCK_UN);
    fclose(fp);  /* fclose会自动close(fd) */
    
    /* 如果读取为空，返回空对象 */
    if (total == 0) {
        snprintf(json_output, size, "{}");
    }
    
    return 0;
}


/* 写入插件存储数据 */
int plugin_storage_write(const char *plugin_name, const char *json_data) {
    if (!json_data) {
        return -1;
    }
    
    /* 检查数据大小 */
    size_t data_len = strlen(json_data);
    if (data_len > PLUGIN_STORAGE_MAX_SIZE) {
        fprintf(stderr, "Plugin storage: data too large (%zu > %d)\n", 
                data_len, PLUGIN_STORAGE_MAX_SIZE);
        return -1;
    }
    
    /* 确保目录存在 */
    if (ensure_plugin_data_dir() != 0) {
        return -1;
    }
    
    char filepath[512];
    if (build_storage_path(plugin_name, filepath, sizeof(filepath)) != 0) {
        return -1;
    }
    
    /* 打开文件（创建或截断） */
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        fprintf(stderr, "Plugin storage: failed to open %s: %s\n", 
                filepath, strerror(errno));
        return -1;
    }
    
    /* 获取排他锁（写锁） */
    if (flock(fd, LOCK_EX) == -1) {
        close(fd);
        return -1;
    }
    
    /* 写入数据 */
    ssize_t written = write(fd, json_data, data_len);
    
    /* 释放锁并关闭文件 */
    flock(fd, LOCK_UN);
    close(fd);
    
    if (written != (ssize_t)data_len) {
        fprintf(stderr, "Plugin storage: write incomplete\n");
        return -1;
    }
    
    return 0;
}

/* 删除插件存储数据 */
int plugin_storage_delete(const char *plugin_name) {
    char filepath[512];
    if (build_storage_path(plugin_name, filepath, sizeof(filepath)) != 0) {
        return -1;
    }
    
    /* 检查文件是否存在 */
    if (access(filepath, F_OK) == -1) {
        /* 文件不存在，视为成功 */
        return 0;
    }
    
    return unlink(filepath) == 0 ? 0 : -1;
}
