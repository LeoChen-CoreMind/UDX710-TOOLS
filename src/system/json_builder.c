/**
 * @file json_builder.c
 * @brief JSON Builder工具库实现 - 基于mongoose mg_iobuf的动态JSON生成
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_builder.h"

/* 初始缓冲区大小 - 增大以减少 realloc 次数 */
#define JSON_INIT_SIZE 4096

/* ==================== 内部辅助函数 ==================== */

/* 添加逗号分隔符（如果不是第一个元素） */
static void json_comma(JsonBuilder *j) {
    if (!j || j->depth < 0 || j->depth >= JSON_MAX_DEPTH) return;
    if (!j->first[j->depth]) {
        mg_iobuf_add(&j->buf, j->buf.len, ",", 1);
    }
    j->first[j->depth] = 0;
}

/* 添加字符串到缓冲区 */
static void json_append(JsonBuilder *j, const char *s, size_t len) {
    if (!j || !s) return;
    mg_iobuf_add(&j->buf, j->buf.len, s, len);
}

/* 添加格式化字符串到缓冲区 */
static void json_appendf(JsonBuilder *j, const char *fmt, ...) {
    if (!j || !fmt) return;
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    size_t n = (size_t)vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0 && n < sizeof(tmp)) {
        mg_iobuf_add(&j->buf, j->buf.len, tmp, n);
    }
}

/* ==================== 生命周期管理 ==================== */

JsonBuilder *json_new(void) {
    JsonBuilder *j = (JsonBuilder *)calloc(1, sizeof(JsonBuilder));
    if (!j) return NULL;
    
    mg_iobuf_init(&j->buf, JSON_INIT_SIZE, 64);
    j->depth = 0;
    for (int i = 0; i < JSON_MAX_DEPTH; i++) {
        j->first[i] = 1;
    }
    return j;
}

char *json_finish(JsonBuilder *j) {
    if (!j) return NULL;
    
    /* 确保字符串以null结尾 */
    mg_iobuf_add(&j->buf, j->buf.len, "", 1);
    
    /* 复制结果 */
    char *result = (char *)malloc(j->buf.len);
    if (result) {
        memcpy(result, j->buf.buf, j->buf.len);
    }
    
    /* 释放资源 */
    mg_iobuf_free(&j->buf);
    free(j);
    
    return result;
}

void json_free(JsonBuilder *j) {
    if (!j) return;
    mg_iobuf_free(&j->buf);
    free(j);
}

/* ==================== 对象操作 ==================== */

void json_obj_open(JsonBuilder *j) {
    if (!j) return;
    json_comma(j);
    json_append(j, "{", 1);
    if (j->depth < JSON_MAX_DEPTH - 1) {
        j->depth++;
        j->first[j->depth] = 1;
    }
}

void json_obj_close(JsonBuilder *j) {
    if (!j) return;
    json_append(j, "}", 1);
    if (j->depth > 0) {
        j->depth--;
    }
}

void json_key_obj_open(JsonBuilder *j, const char *key) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":{", key);
    if (j->depth < JSON_MAX_DEPTH - 1) {
        j->depth++;
        j->first[j->depth] = 1;
    }
}

/* ==================== 数组操作 ==================== */

void json_arr_open(JsonBuilder *j, const char *key) {
    if (!j) return;
    json_comma(j);
    if (key && strlen(key) > 0) {
        json_appendf(j, "\"%s\":[", key);
    } else {
        json_append(j, "[", 1);
    }
    if (j->depth < JSON_MAX_DEPTH - 1) {
        j->depth++;
        j->first[j->depth] = 1;
    }
}

void json_arr_close(JsonBuilder *j) {
    if (!j) return;
    json_append(j, "]", 1);
    if (j->depth > 0) {
        j->depth--;
    }
}

void json_arr_obj_open(JsonBuilder *j) {
    if (!j) return;
    json_comma(j);
    json_append(j, "{", 1);
    if (j->depth < JSON_MAX_DEPTH - 1) {
        j->depth++;
        j->first[j->depth] = 1;
    }
}

/* ==================== 值添加函数 ==================== */

void json_add_str(JsonBuilder *j, const char *key, const char *val) {
    if (!j || !key) return;
    json_comma(j);
    
    /* 使用mg_snprintf和MG_ESC进行转义 */
    char tmp[4096];
    size_t n = mg_snprintf(tmp, sizeof(tmp), "\"%s\":%m", key, MG_ESC(val ? val : ""));
    if (n > 0 && n < sizeof(tmp)) {
        mg_iobuf_add(&j->buf, j->buf.len, tmp, n);
    }
}

void json_add_int(JsonBuilder *j, const char *key, int val) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":%d", key, val);
}

void json_add_long(JsonBuilder *j, const char *key, long long val) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":%lld", key, val);
}

void json_add_ulong(JsonBuilder *j, const char *key, unsigned long val) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":%lu", key, val);
}

void json_add_double(JsonBuilder *j, const char *key, double val) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":%.2f", key, val);
}

void json_add_bool(JsonBuilder *j, const char *key, int val) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":%s", key, val ? "true" : "false");
}

void json_add_null(JsonBuilder *j, const char *key) {
    if (!j || !key) return;
    json_comma(j);
    json_appendf(j, "\"%s\":null", key);
}

void json_add_raw(JsonBuilder *j, const char *key, const char *val) {
    if (!j || !val) return;
    json_comma(j);
    if (key && strlen(key) > 0) {
        json_appendf(j, "\"%s\":%s", key, val);
    } else {
        json_append(j, val, strlen(val));
    }
}

/* ==================== 数组元素添加 ==================== */

void json_arr_add_str(JsonBuilder *j, const char *val) {
    if (!j) return;
    json_comma(j);
    
    char tmp[4096];
    size_t n = mg_snprintf(tmp, sizeof(tmp), "%m", MG_ESC(val ? val : ""));
    if (n > 0 && n < sizeof(tmp)) {
        mg_iobuf_add(&j->buf, j->buf.len, tmp, n);
    }
}

void json_arr_add_int(JsonBuilder *j, int val) {
    if (!j) return;
    json_comma(j);
    json_appendf(j, "%d", val);
}

void json_arr_add_bool(JsonBuilder *j, int val) {
    if (!j) return;
    json_comma(j);
    json_append(j, val ? "true" : "false", val ? 4 : 5);
}
