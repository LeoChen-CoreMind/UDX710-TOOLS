/**
 * @file json_builder.h
 * @brief JSON Builder工具库 - 基于mongoose mg_iobuf的动态JSON生成
 * 
 * 使用示例:
 *   JsonBuilder *j = json_new();
 *   json_obj_open(j);
 *   json_add_str(j, "name", "test");
 *   json_add_int(j, "code", 200);
 *   json_obj_close(j);
 *   HTTP_OK(c, json_finish(j));
 */

#ifndef JSON_BUILDER_H
#define JSON_BUILDER_H

#include "mongoose.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* JSON Builder最大嵌套深度 */
#define JSON_MAX_DEPTH 8

/* JSON Builder结构体 */
typedef struct {
    struct mg_iobuf buf;    /* mongoose动态缓冲区 */
    int depth;              /* 当前嵌套深度 */
    int first[JSON_MAX_DEPTH]; /* 每层是否是第一个元素 */
} JsonBuilder;

/* ==================== 生命周期管理 ==================== */

/**
 * 创建新的JsonBuilder
 * @return JsonBuilder指针，失败返回NULL
 */
JsonBuilder *json_new(void);

/**
 * 获取JSON字符串并释放JsonBuilder
 * @param j JsonBuilder指针
 * @return JSON字符串（调用者需要free），失败返回NULL
 */
char *json_finish(JsonBuilder *j);

/**
 * 释放JsonBuilder（不返回字符串）
 * @param j JsonBuilder指针
 */
void json_free(JsonBuilder *j);

/* ==================== 对象操作 ==================== */

/**
 * 开始JSON对象 {
 * @param j JsonBuilder指针
 */
void json_obj_open(JsonBuilder *j);

/**
 * 结束JSON对象 }
 * @param j JsonBuilder指针
 */
void json_obj_close(JsonBuilder *j);

/**
 * 开始带key的嵌套对象 "key":{
 * @param j JsonBuilder指针
 * @param key 键名
 */
void json_key_obj_open(JsonBuilder *j, const char *key);

/* ==================== 数组操作 ==================== */

/**
 * 开始JSON数组 "key":[
 * @param j JsonBuilder指针
 * @param key 键名（可为NULL表示匿名数组）
 */
void json_arr_open(JsonBuilder *j, const char *key);

/**
 * 结束JSON数组 ]
 * @param j JsonBuilder指针
 */
void json_arr_close(JsonBuilder *j);

/**
 * 在数组中开始新对象 {
 * @param j JsonBuilder指针
 */
void json_arr_obj_open(JsonBuilder *j);

/* ==================== 值添加函数 ==================== */

/**
 * 添加字符串值（自动转义特殊字符）
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 字符串值（NULL会输出空字符串）
 */
void json_add_str(JsonBuilder *j, const char *key, const char *val);

/**
 * 添加整数值
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 整数值
 */
void json_add_int(JsonBuilder *j, const char *key, int val);

/**
 * 添加长整数值
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 长整数值
 */
void json_add_long(JsonBuilder *j, const char *key, long long val);

/**
 * 添加无符号长整数值
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 无符号长整数值
 */
void json_add_ulong(JsonBuilder *j, const char *key, unsigned long val);

/**
 * 添加浮点数值
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 浮点数值
 */
void json_add_double(JsonBuilder *j, const char *key, double val);

/**
 * 添加布尔值
 * @param j JsonBuilder指针
 * @param key 键名
 * @param val 布尔值（0=false, 非0=true）
 */
void json_add_bool(JsonBuilder *j, const char *key, int val);

/**
 * 添加null值
 * @param j JsonBuilder指针
 * @param key 键名
 */
void json_add_null(JsonBuilder *j, const char *key);

/**
 * 添加原始JSON（不转义）
 * @param j JsonBuilder指针
 * @param key 键名（可为NULL）
 * @param val 原始JSON字符串
 */
void json_add_raw(JsonBuilder *j, const char *key, const char *val);

/* ==================== 数组元素添加（无key） ==================== */

/**
 * 向数组添加字符串元素
 * @param j JsonBuilder指针
 * @param val 字符串值
 */
void json_arr_add_str(JsonBuilder *j, const char *val);

/**
 * 向数组添加整数元素
 * @param j JsonBuilder指针
 * @param val 整数值
 */
void json_arr_add_int(JsonBuilder *j, int val);

/**
 * 向数组添加布尔元素
 * @param j JsonBuilder指针
 * @param val 布尔值
 */
void json_arr_add_bool(JsonBuilder *j, int val);

#ifdef __cplusplus
}
#endif

#endif /* JSON_BUILDER_H */
