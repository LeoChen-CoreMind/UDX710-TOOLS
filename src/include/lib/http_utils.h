/**
 * @file http_utils.h
 * @brief HTTP工具库 - 封装CORS、OPTIONS预检、方法检查和JSON响应
 */

#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#include "mongoose.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== CORS响应头 ==================== */

#define HTTP_CORS_HEADERS \
    "Content-Type: application/json\r\n" \
    "Access-Control-Allow-Origin: *\r\n"

#define HTTP_OPTIONS_HEADERS \
    "Access-Control-Allow-Origin: *\r\n" \
    "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type\r\n"

/* ==================== OPTIONS预检处理 ==================== */

#define HTTP_HANDLE_OPTIONS(c, hm) \
    if ((hm)->method.len == 7 && memcmp((hm)->method.buf, "OPTIONS", 7) == 0) { \
        mg_http_reply((c), 200, HTTP_OPTIONS_HEADERS, ""); \
        return; \
    }

/* ==================== 辅助函数 ==================== */

/* 检查HTTP方法 */
static inline int http_is_method(struct mg_http_message *hm, const char *method) {
    size_t len = strlen(method);
    return hm->method.len == len && memcmp(hm->method.buf, method, len) == 0;
}

/* 发送方法不允许错误 */
static inline void http_method_error(struct mg_connection *c) {
    mg_http_reply(c, 405, HTTP_CORS_HEADERS, "{\"error\":\"Method not allowed\"}");
}

/* ==================== 方法检查宏 ==================== */

#define HTTP_CHECK_GET(c, hm) \
    do { \
        HTTP_HANDLE_OPTIONS(c, hm); \
        if (!http_is_method(hm, "GET")) { http_method_error(c); return; } \
    } while(0)

#define HTTP_CHECK_POST(c, hm) \
    do { \
        HTTP_HANDLE_OPTIONS(c, hm); \
        if (!http_is_method(hm, "POST")) { http_method_error(c); return; } \
    } while(0)


#define HTTP_CHECK_DELETE(c, hm) \
    do { \
        HTTP_HANDLE_OPTIONS(c, hm); \
        if (!http_is_method(hm, "DELETE")) { http_method_error(c); return; } \
    } while(0)

#define HTTP_CHECK_PUT(c, hm) \
    do { \
        HTTP_HANDLE_OPTIONS(c, hm); \
        if (!http_is_method(hm, "PUT")) { http_method_error(c); return; } \
    } while(0)

/* 仅处理OPTIONS，不检查方法（用于支持多方法的handler） */
#define HTTP_CHECK_ANY(c, hm) \
    do { \
        HTTP_HANDLE_OPTIONS(c, hm); \
    } while(0)

/* ==================== JSON响应宏 ==================== */

/* 200 OK响应 */
#define HTTP_OK(c, json) \
    mg_http_reply((c), 200, HTTP_CORS_HEADERS, "%s", (json))

/* 错误响应 */
#define HTTP_ERROR(c, code, msg) \
    mg_http_reply((c), (code), HTTP_CORS_HEADERS, "{\"error\":\"%s\"}", (msg))

/* 成功响应 */
#define HTTP_SUCCESS(c, msg) \
    mg_http_reply((c), 200, HTTP_CORS_HEADERS, "{\"status\":\"success\",\"message\":\"%s\"}", (msg))

/* 带状态码的JSON响应 */
#define HTTP_JSON(c, code, json) \
    mg_http_reply((c), (code), HTTP_CORS_HEADERS, "%s", (json))

/* 200 OK响应并释放json字符串 */
#define HTTP_OK_FREE(c, json) do { \
    char *_json = (json); \
    mg_http_reply((c), 200, HTTP_CORS_HEADERS, "%s", _json); \
    free(_json); \
} while(0)

/* 带状态码的JSON响应并释放 */
#define HTTP_JSON_FREE(c, code, json) do { \
    char *_json = (json); \
    mg_http_reply((c), (code), HTTP_CORS_HEADERS, "%s", _json); \
    free(_json); \
} while(0)

/* ==================== JSON解析辅助宏 ==================== */

/* 
 * 使用mongoose内置JSON函数:
 * - mg_json_get_str(body, "$.key")     获取字符串(需free)
 * - mg_json_get_num(body, "$.key", &v) 获取数值
 * - mg_json_get_bool(body, "$.key", &v) 获取布尔值
 * - mg_json_get_long(body, "$.key", default) 获取长整型
 */

#ifdef __cplusplus
}
#endif

#endif /* HTTP_UTILS_H */
