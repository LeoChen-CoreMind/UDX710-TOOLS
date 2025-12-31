/**
 * @file plugin.c
 * @brief 插件管理和Shell执行模块实现
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "mongoose.h"
#include "plugin.h"
#include "lib/json_builder.h"

/* 危险命令黑名单 */
static const char *dangerous_commands[] = {
    "rm -rf /",
    "rm -rf /*",
    "mkfs",
    "dd if=",
    ":(){:|:&};:",
    "chmod -R 777 /",
    "chown -R",
    "> /dev/sda",
    "mv /* ",
    NULL
};

/* 检查命令是否安全 */
static int is_command_safe(const char *cmd) {
    for (int i = 0; dangerous_commands[i] != NULL; i++) {
        if (strstr(cmd, dangerous_commands[i]) != NULL) {
            return 0;
        }
    }
    return 1;
}

/* 确保插件目录存在 */
int ensure_plugin_dir(void) {
    struct stat st = {0};
    if (stat(PLUGIN_DIR, &st) == -1) {
        if (mkdir(PLUGIN_DIR, 0755) == -1) {
            /* 尝试递归创建 */
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "mkdir -p %s", PLUGIN_DIR);
            return system(cmd) == 0 ? 0 : -1;
        }
    }
    return 0;
}

/* 执行Shell命令 */
int execute_shell(const char *cmd, char *output, size_t size) {
    if (!cmd || !output || size == 0) {
        return -1;
    }

    /* 安全检查 */
    if (!is_command_safe(cmd)) {
        snprintf(output, size, "Error: Command blocked for security reasons");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(output, size, "Error: Failed to execute command");
        return -1;
    }

    size_t total = 0;
    char buffer[256];
    output[0] = '\0';

    while (fgets(buffer, sizeof(buffer), fp) != NULL && total < size - 1) {
        size_t len = strlen(buffer);
        if (total + len >= size - 1) {
            len = size - 1 - total;
        }
        memcpy(output + total, buffer, len);
        total += len;
    }
    output[total] = '\0';

    int status = pclose(fp);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}


/* 从插件内容中提取元信息 */
static int extract_plugin_meta(const char *content, char *name, char *version, 
                                char *author, char *description, char *icon, char *color) {
    /* 默认值 */
    strcpy(name, "未命名插件");
    strcpy(version, "1.0.0");
    strcpy(author, "未知");
    strcpy(description, "");
    strcpy(icon, "fa-puzzle-piece");
    strcpy(color, "from-blue-500 to-cyan-400");

    /* 简单解析 name: 'xxx' 或 name: "xxx" */
    const char *patterns[][2] = {
        {"name:", name},
        {"version:", version},
        {"author:", author},
        {"description:", description},
        {"icon:", icon},
        {"color:", color}
    };

    for (int i = 0; i < 6; i++) {
        const char *p = strstr(content, patterns[i][0]);
        if (p) {
            p += strlen(patterns[i][0]);
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                char *dst = (char *)patterns[i][1];
                int j = 0;
                while (*p && *p != quote && j < 127) {
                    dst[j++] = *p++;
                }
                dst[j] = '\0';
            }
        }
    }
    return 0;
}

/* 获取插件列表 */
int get_plugin_list(char *json_output, size_t size) {
    ensure_plugin_dir();

    DIR *dir = opendir(PLUGIN_DIR);
    if (!dir) {
        snprintf(json_output, size, "[]");
        return 0;
    }

    JsonBuilder *j = json_new();
    if (!j) {
        closedir(dir);
        snprintf(json_output, size, "[]");
        return 0;
    }

    json_arr_open(j, NULL);

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < PLUGIN_MAX_COUNT) {
        /* 只处理.js文件 */
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".js") != 0) continue;

        /* 读取文件内容 */
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", PLUGIN_DIR, entry->d_name);

        FILE *fp = fopen(filepath, "r");
        if (!fp) continue;

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsize > PLUGIN_MAX_SIZE) {
            fclose(fp);
            continue;
        }

        char *content = malloc(fsize + 1);
        if (!content) {
            fclose(fp);
            continue;
        }

        fread(content, 1, fsize, fp);
        content[fsize] = '\0';
        fclose(fp);

        /* 提取元信息 */
        char name[128], version[32], author[64], description[256], icon[64], color[128];
        extract_plugin_meta(content, name, version, author, description, icon, color);

        /* 使用JsonBuilder构建插件对象 */
        json_arr_obj_open(j);
        json_add_str(j, "filename", entry->d_name);
        json_add_str(j, "name", name);
        json_add_str(j, "version", version);
        json_add_str(j, "author", author);
        json_add_str(j, "description", description);
        json_add_str(j, "icon", icon);
        json_add_str(j, "color", color);
        json_add_str(j, "content", content);
        json_obj_close(j);

        free(content);
        count++;
    }

    closedir(dir);
    json_arr_close(j);

    char *result = json_finish(j);
    if (result) {
        strncpy(json_output, result, size - 1);
        json_output[size - 1] = '\0';
        free(result);
    } else {
        snprintf(json_output, size, "[]");
    }

    return count;
}


/* 保存插件 */
int save_plugin(const char *name, const char *content) {
    if (!name || !content) return -1;

    ensure_plugin_dir();

    /* 验证文件名 */
    if (strstr(name, "..") || strstr(name, "/")) {
        return -1;
    }

    /* 确保以.js结尾 */
    char filename[256];
    if (strstr(name, ".js")) {
        snprintf(filename, sizeof(filename), "%s/%s", PLUGIN_DIR, name);
    } else {
        snprintf(filename, sizeof(filename), "%s/%s.js", PLUGIN_DIR, name);
    }

    /* 检查大小 */
    if (strlen(content) > PLUGIN_MAX_SIZE) {
        return -1;
    }

    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;

    fprintf(fp, "%s", content);
    fclose(fp);

    return 0;
}

/* 删除插件 */
int delete_plugin(const char *name) {
    if (!name) return -1;

    /* 验证文件名 */
    if (strstr(name, "..") || strstr(name, "/")) {
        return -1;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", PLUGIN_DIR, name);

    /* 确保文件存在且是.js文件 */
    const char *ext = strrchr(name, '.');
    if (!ext || strcmp(ext, ".js") != 0) {
        return -1;
    }

    return unlink(filepath) == 0 ? 0 : -1;
}

/* 删除所有插件 */
int delete_all_plugins(void) {
    DIR *dir = opendir(PLUGIN_DIR);
    if (!dir) return 0;

    struct dirent *entry;
    int deleted = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* 只删除.js文件，保留默认插件 */
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".js") != 0) continue;

        /* 跳过默认插件 */
        if (strncmp(entry->d_name, "_default", 8) == 0) continue;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", PLUGIN_DIR, entry->d_name);

        if (unlink(filepath) == 0) {
            deleted++;
        }
    }

    closedir(dir);
    return 0;
}
