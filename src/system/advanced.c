/**
 * @file advanced.c
 * @brief 高级网络功能实现 (Go: handlers/advanced.go)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include "mongoose.h"
#include "advanced.h"
#include "dbus_core.h"
#include "exec_utils.h"
#include "http_utils.h"
#include "ofono.h"
#include "json_builder.h"

/* 频段映射结构 */
typedef struct {
    const char *name;
    const char *mode;  /* "4G" or "5G" */
    const char *type;  /* "TDD" or "FDD" */
    int value;
} BandMapping;

/* 频段映射表 */
static const BandMapping band_map[] = {
    {"TDD_34", "4G", "TDD", 2},
    {"TDD_38", "4G", "TDD", 32},
    {"TDD_39", "4G", "TDD", 64},
    {"TDD_40", "4G", "TDD", 128},
    {"TDD_41", "4G", "TDD", 256},
    {"FDD_01", "4G", "FDD", 1},
    {"FDD_03", "4G", "FDD", 4},
    {"FDD_05", "4G", "FDD", 16},
    {"FDD_08", "4G", "FDD", 128},
    {"N01", "5G", "FDD", 1},
    {"N08", "5G", "FDD", 128},
    {"N28", "5G", "FDD", 512},
    {"N41", "5G", "TDD", 16},
    {"N77", "5G", "TDD", 128},
    {"N78", "5G", "TDD", 256},
    {"N79", "5G", "TDD", 512},
    {NULL, NULL, NULL, 0}
};

/* 解析频段锁定状态 */
static void parse_bands_info(const char *output4G, const char *output5G, int *bands) {
    /* 初始化所有频段为未锁定 */
    memset(bands, 0, 16 * sizeof(int));

    /* 解析4G频段: +SPLBAND: 0,tdd,0,fdd,0 */
    if (output4G && strlen(output4G) > 0) {
        int tdd = 0, fdd = 0;
        char *p = strstr(output4G, "+SPLBAND:");
        if (p) {
            sscanf(p, "+SPLBAND: 0,%d,0,%d,0", &tdd, &fdd);
            /* 4G TDD */
            if (tdd & 2) bands[0] = 1;    /* TDD_34 */
            if (tdd & 32) bands[1] = 1;   /* TDD_38 */
            if (tdd & 64) bands[2] = 1;   /* TDD_39 */
            if (tdd & 128) bands[3] = 1;  /* TDD_40 */
            if (tdd & 256) bands[4] = 1;  /* TDD_41 */
            /* 4G FDD */
            if (fdd & 1) bands[5] = 1;    /* FDD_01 */
            if (fdd & 4) bands[6] = 1;    /* FDD_03 */
            if (fdd & 16) bands[7] = 1;   /* FDD_05 */
            if (fdd & 128) bands[8] = 1;  /* FDD_08 */
        }
    }

    /* 解析5G频段: +SPLBAND: fdd,0,tdd,0 */
    if (output5G && strlen(output5G) > 0) {
        int fdd = 0, tdd = 0;
        char *p = strstr(output5G, "+SPLBAND:");
        if (p) {
            sscanf(p, "+SPLBAND: %d,0,%d,0", &fdd, &tdd);
            /* 5G FDD */
            if (fdd & 1) bands[9] = 1;     /* N01 */
            if (fdd & 128) bands[10] = 1;  /* N08 */
            if (fdd & 512) bands[11] = 1;  /* N28 */
            /* 5G TDD */
            if (tdd & 16) bands[12] = 1;   /* N41 */
            if (tdd & 128) bands[13] = 1;  /* N77 */
            if (tdd & 256) bands[14] = 1;  /* N78 */
            if (tdd & 512) bands[15] = 1;  /* N79 */
        }
    }
}

/* GET /api/bands - 获取频段状态 */
void handle_get_bands(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    char *result4G = NULL, *result5G = NULL;
    int bands[16] = {0};

    printf("开始获取频段锁定状态...\n");

    /* 查询4G频段 */
    if (execute_at("AT+SPLBAND=0", &result4G) == 0) {
        printf("4G频段查询结果: %s\n", result4G);
    }

    /* 查询5G频段 */
    if (execute_at("AT+SPLBAND=3", &result5G) == 0) {
        printf("5G频段查询结果: %s\n", result5G);
    }

    parse_bands_info(result4G, result5G, bands);

    if (result4G) g_free(result4G);
    if (result5G) g_free(result5G);

    /* 使用JSON Builder构建响应 */
    JsonBuilder *j = json_new();
    json_obj_open(j);
    
    /* 4G TDD */
    json_arr_open(j, "4G_TDD");
    json_arr_obj_open(j); json_add_str(j, "name", "TDD_34"); json_add_str(j, "label", "B34"); json_add_bool(j, "locked", bands[0]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "TDD_38"); json_add_str(j, "label", "B38"); json_add_bool(j, "locked", bands[1]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "TDD_39"); json_add_str(j, "label", "B39"); json_add_bool(j, "locked", bands[2]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "TDD_40"); json_add_str(j, "label", "B40"); json_add_bool(j, "locked", bands[3]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "TDD_41"); json_add_str(j, "label", "B41"); json_add_bool(j, "locked", bands[4]); json_obj_close(j);
    json_arr_close(j);
    
    /* 4G FDD */
    json_arr_open(j, "4G_FDD");
    json_arr_obj_open(j); json_add_str(j, "name", "FDD_01"); json_add_str(j, "label", "B1"); json_add_bool(j, "locked", bands[5]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "FDD_03"); json_add_str(j, "label", "B3"); json_add_bool(j, "locked", bands[6]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "FDD_05"); json_add_str(j, "label", "B5"); json_add_bool(j, "locked", bands[7]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "FDD_08"); json_add_str(j, "label", "B8"); json_add_bool(j, "locked", bands[8]); json_obj_close(j);
    json_arr_close(j);
    
    /* 5G */
    json_arr_open(j, "5G");
    json_arr_obj_open(j); json_add_str(j, "name", "N01"); json_add_str(j, "label", "N1"); json_add_bool(j, "locked", bands[9]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N08"); json_add_str(j, "label", "N8"); json_add_bool(j, "locked", bands[10]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N28"); json_add_str(j, "label", "N28"); json_add_bool(j, "locked", bands[11]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N41"); json_add_str(j, "label", "N41"); json_add_bool(j, "locked", bands[12]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N77"); json_add_str(j, "label", "N77"); json_add_bool(j, "locked", bands[13]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N78"); json_add_str(j, "label", "N78"); json_add_bool(j, "locked", bands[14]); json_obj_close(j);
    json_arr_obj_open(j); json_add_str(j, "name", "N79"); json_add_str(j, "label", "N79"); json_add_bool(j, "locked", bands[15]); json_obj_close(j);
    json_arr_close(j);
    
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}


/* 查找频段映射 */
static const BandMapping *find_band(const char *name) {
    for (int i = 0; band_map[i].name; i++) {
        if (strcmp(band_map[i].name, name) == 0) {
            return &band_map[i];
        }
    }
    return NULL;
}

/* 从 JSON 数组中提取频段名称 - 使用mongoose JSON API */
static int parse_bands_array(const char *json_str, char bands[][32], int max_bands) {
    int count = 0;
    struct mg_str json = mg_str(json_str);
    
    /* 获取bands数组 */
    struct mg_str bands_arr = mg_json_get_tok(json, "$.bands");
    if (bands_arr.buf == NULL) return 0;
    
    /* 遍历数组元素 */
    struct mg_str key, val;
    size_t ofs = 0;
    while ((ofs = mg_json_next(bands_arr, ofs, &key, &val)) > 0 && count < max_bands) {
        /* val 包含引号，如 "TDD_34" */
        if (val.len > 2 && val.buf[0] == '"') {
            size_t len = val.len - 2;
            if (len < 32) {
                memcpy(bands[count], val.buf + 1, len);
                bands[count][len] = '\0';
                count++;
            }
        }
    }
    return count;
}

/* POST /api/lock_bands - 锁定频段 */
void handle_lock_bands(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char bands[32][32] = {{0}};
    int band_count = parse_bands_array(hm->body.buf, bands, 32);

    printf("收到锁频请求，要锁定的频段数量: %d\n", band_count);

    /* 计算位掩码 */
    int tdd4G = 0, fdd4G = 0, fdd5G = 0, tdd5G = 0;
    for (int i = 0; i < band_count; i++) {
        const BandMapping *bm = find_band(bands[i]);
        if (bm) {
            printf("处理频段 %s: 模式=%s, 类型=%s, 值=%d\n", bm->name, bm->mode, bm->type, bm->value);
            if (strcmp(bm->mode, "4G") == 0 && strcmp(bm->type, "TDD") == 0) {
                tdd4G |= bm->value;
            } else if (strcmp(bm->mode, "4G") == 0 && strcmp(bm->type, "FDD") == 0) {
                fdd4G |= bm->value;
            } else if (strcmp(bm->mode, "5G") == 0 && strcmp(bm->type, "TDD") == 0) {
                tdd5G |= bm->value;
            } else if (strcmp(bm->mode, "5G") == 0 && strcmp(bm->type, "FDD") == 0) {
                fdd5G |= bm->value;
            }
        }
    }

    printf("计算结果: 4G TDD=%d, 4G FDD=%d, 5G FDD=%d, 5G TDD=%d\n", tdd4G, fdd4G, fdd5G, tdd5G);

    char *result = NULL;
    char cmd[128];

    /* 执行命令序列 */
    /* 1. 关闭设备 */
    if (execute_at("AT+SFUN=5", &result) != 0) {
        HTTP_ERROR(c, 500, "关闭设备失败");
        if (result) g_free(result);
        return;
    }
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 2. 解锁5G频段 */
    execute_at("AT+SPLBAND=2,0,0,0,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 3. 锁定4G频段 */
    if (tdd4G != 0 || fdd4G != 0) {
        snprintf(cmd, sizeof(cmd), "AT+SPLBAND=1,0,%d,0,%d,0", tdd4G, fdd4G);
        execute_at(cmd, &result);
        if (result) { g_free(result); result = NULL; }
        usleep(300000);
    }

    /* 4. 锁定5G频段 */
    if (fdd5G != 0 || tdd5G != 0) {
        snprintf(cmd, sizeof(cmd), "AT+SPLBAND=2,%d,0,%d,0", fdd5G, tdd5G);
        execute_at(cmd, &result);
        if (result) { g_free(result); result = NULL; }
        usleep(300000);
    }

    /* 5. 开启设备 */
    execute_at("AT+SFUN=4", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 6. 激活网络 */
    execute_at("AT+CGACT=0,1", &result);
    if (result) g_free(result);

    printf("频段锁定成功\n");
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "success", 1);
    json_add_str(j, "message", "频段锁定成功");
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}


/* POST /api/unlock_bands - 解锁所有频段 */
void handle_unlock_bands(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    printf("开始解锁所有频段...\n");
    char *result = NULL;

    /* 1. 关闭设备 */
    if (execute_at("AT+SFUN=5", &result) != 0) {
        HTTP_ERROR(c, 500, "关闭设备失败");
        if (result) g_free(result);
        return;
    }
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 2. 解锁4G频段 */
    execute_at("AT+SPLBAND=1,0,0,0,0,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 3. 解锁5G频段 */
    execute_at("AT+SPLBAND=2,0,0,0,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 4. 开启设备 */
    execute_at("AT+SFUN=4", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 5. 激活网络 */
    execute_at("AT+CGACT=0,1", &result);
    if (result) g_free(result);

    printf("频段解锁成功\n");
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_bool(j, "success", 1);
    json_add_str(j, "message", "频段解锁成功");
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* 解析小区数据 (复用 handlers.c 中的函数) */
extern int parse_cell_to_vec(const char *input, char data[64][16][32]);

/**
 * 根据 NR ARFCN 推算 5G 频段
 * 参考 3GPP TS 38.104
 * @param arfcn NR 绝对频点号
 * @return 频段号字符串（不含N前缀），如 "41", "78"，未知返回空字符串
 */
static const char* arfcn_to_nr_band(int arfcn) {
    if (arfcn >= 422000 && arfcn <= 434000) return "1";   /* N1 (2100 MHz FDD) */
    if (arfcn >= 361000 && arfcn <= 376000) return "3";   /* N3 (1800 MHz FDD) */
    if (arfcn >= 185000 && arfcn <= 192000) return "8";   /* N8 (900 MHz FDD) */
    if (arfcn >= 151600 && arfcn <= 160600) return "28";  /* N28 (700 MHz FDD) */
    if (arfcn >= 499200 && arfcn <= 537999) return "41";  /* N41 (2600 MHz TDD) */
    if (arfcn >= 620000 && arfcn <= 680000) return "78";  /* N77/N78 (3700 MHz TDD) */
    if (arfcn >= 693334 && arfcn <= 733333) return "79";  /* N79 (4700 MHz TDD) */
    return "";
}

/**
 * 根据 LTE EARFCN 推算 4G 频段
 * 参考 3GPP TS 36.101
 * @param earfcn LTE 绝对频点号
 * @return 频段号字符串（不含B前缀），如 "3", "41"，未知返回空字符串
 */
static const char* earfcn_to_lte_band(int earfcn) {
    if (earfcn >= 0 && earfcn <= 599) return "1";         /* B1 (2100 MHz FDD) */
    if (earfcn >= 1200 && earfcn <= 1949) return "3";     /* B3 (1800 MHz FDD) */
    if (earfcn >= 2400 && earfcn <= 2649) return "5";     /* B5 (850 MHz FDD) */
    if (earfcn >= 2750 && earfcn <= 3449) return "7";     /* B7 (2600 MHz FDD) */
    if (earfcn >= 3450 && earfcn <= 3799) return "8";     /* B8 (900 MHz FDD) */
    if (earfcn >= 6150 && earfcn <= 6449) return "20";    /* B20 (800 MHz FDD) */
    if (earfcn >= 9210 && earfcn <= 9659) return "28";    /* B28 (700 MHz FDD) */
    if (earfcn >= 37750 && earfcn <= 38249) return "38";  /* B38 (2600 MHz TDD) */
    if (earfcn >= 38250 && earfcn <= 38649) return "39";  /* B39 (1900 MHz TDD) */
    if (earfcn >= 38650 && earfcn <= 39649) return "40";  /* B40 (2300 MHz TDD) */
    if (earfcn >= 39650 && earfcn <= 41589) return "41";  /* B41 (2500 MHz TDD) */
    return "";
}

/**
 * 判断当前网络是否为 5G
 * 通过 D-Bus 查询 oFono NetworkMonitor 获取网络类型
 * @return 1=5G, 0=4G/其他
 */
static int is_5g_network(void) {
    char tech[32] = {0};
    
    /* 使用 C 原生 D-Bus 调用获取网络类型 */
    if (ofono_get_serving_cell_tech(tech, sizeof(tech)) != 0) {
        printf("D-Bus 查询网络类型失败，默认使用 4G\n");
        return 0;
    }

    /* 判断网络类型 - 检查是否为 "nr" */
    if (strcmp(tech, "nr") == 0) {
        return 1; /* 5G */
    }
    
    return 0; /* 4G 或其他 */
}

/* 辅助函数：添加小区对象到JSON Builder */
static void add_cell_to_json(JsonBuilder *j, const char *rat, const char *band_prefix, 
                              const char *band, int arfcn, int pci,
                              double rsrp, double rsrq, double sinr, int is_serving) {
    char band_str[32];
    snprintf(band_str, sizeof(band_str), "%s%s", band_prefix, band);
    
    json_arr_obj_open(j);
    json_add_str(j, "rat", rat);
    json_add_str(j, "band", band_str);
    json_add_int(j, "arfcn", arfcn);
    json_add_int(j, "pci", pci);
    json_add_double(j, "rsrp", rsrp);
    json_add_double(j, "rsrq", rsrq);
    json_add_double(j, "sinr", sinr);
    json_add_bool(j, "isServing", is_serving);
    json_obj_close(j);
}

/* GET /api/cells - 获取小区信息 */
void handle_get_cells(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_GET(c, hm);

    printf("开始获取小区信息...\n");
    char *result = NULL;
    
    /* 通过 D-Bus 判断网络类型 (与 Go 版本一致) */
    int is_5g = is_5g_network();
    printf("检测到%s网络\n", is_5g ? "5G" : "4G");

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_arr_open(j, "Data");
    
    int cell_count = 0;

    if (is_5g) {
        /* 5G 主小区 */
        if (execute_at("AT+SPENGMD=0,14,1", &result) == 0 && result) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            if (rows > 15) {
                add_cell_to_json(j, "5G", "N", data[0][0],
                    atoi(data[1][0]), atoi(data[2][0]),
                    atof(data[3][0]) / 100.0, atof(data[4][0]) / 100.0,
                    atof(data[15][0]) / 100.0, 1);
                cell_count++;
            }
            g_free(result);
            result = NULL;
        }

        /* 5G 邻小区 */
        if (execute_at("AT+SPENGMD=0,14,2", &result) == 0 && result) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            if (rows > 5) {
                int col_count = 0;
                for (int i = 0; i < 16 && data[0][i][0]; i++) col_count++;
                for (int i = 0; i < col_count; i++) {
                    int arfcn = atoi(data[1][i]);
                    int pci = atoi(data[2][i]);
                    if (arfcn == 0 || pci == 0) continue;
                    
                    /* 频段处理：如果为空或"0"，通过 ARFCN 推算 */
                    const char *band_str = data[0][i];
                    if (strlen(band_str) == 0 || strcmp(band_str, "0") == 0) {
                        band_str = arfcn_to_nr_band(arfcn);
                    }
                    
                    add_cell_to_json(j, "5G", "N", band_str,
                        arfcn, pci,
                        atof(data[3][i]) / 100.0, atof(data[4][i]) / 100.0,
                        atof(data[5][i]) / 100.0, 0);
                    cell_count++;
                }
            }
            g_free(result);
        }
    } else {
        /* 4G 主小区 */
        if (execute_at("AT+SPENGMD=0,6,0", &result) == 0 && result) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            if (rows > 33) {
                add_cell_to_json(j, "4G", "B", data[0][0],
                    atoi(data[1][0]), atoi(data[2][0]),
                    atof(data[3][0]) / 100.0, atof(data[4][0]) / 100.0,
                    atof(data[33][0]) / 100.0, 1);
                cell_count++;
            }
            g_free(result);
            result = NULL;
        }

        /* 4G 邻小区 */
        if (execute_at("AT+SPENGMD=0,6,6", &result) == 0 && result) {
            char data[64][16][32] = {{{0}}};
            int rows = parse_cell_to_vec(result, data);
            for (int i = 0; i < rows; i++) {
                int arfcn = atoi(data[i][0]);
                int pci = atoi(data[i][1]);
                if (arfcn == 0 || pci == 0) continue;
                
                /* 频段处理：如果为空或"0"，通过 EARFCN 推算 */
                const char *band = data[i][12];
                if (strlen(band) == 0 || strcmp(band, "0") == 0) {
                    band = earfcn_to_lte_band(arfcn);
                    if (strlen(band) == 0) band = "0";  /* 未知频段默认显示0 */
                }
                
                add_cell_to_json(j, "4G", "B", band,
                    arfcn, pci,
                    atof(data[i][2]) / 100.0, atof(data[i][3]) / 100.0,
                    atof(data[i][6]) / 100.0, 0);
                cell_count++;
            }
            g_free(result);
        }
    }

    json_arr_close(j);
    json_obj_close(j);
    printf("小区信息获取完成，共 %d 个小区\n", cell_count);

    HTTP_OK_FREE(c, json_finish(j));
}


/* POST /api/lock_cell - 锁定小区 */
void handle_lock_cell(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char technology[32] = {0}, arfcn[32] = {0}, pci[32] = {0};

    /* 使用mongoose JSON API解析 */
    char *tech_str = mg_json_get_str(hm->body, "$.technology");
    char *arfcn_str = mg_json_get_str(hm->body, "$.arfcn");
    char *pci_str = mg_json_get_str(hm->body, "$.pci");
    
    if (tech_str) { strncpy(technology, tech_str, sizeof(technology) - 1); free(tech_str); }
    if (arfcn_str) { strncpy(arfcn, arfcn_str, sizeof(arfcn) - 1); free(arfcn_str); }
    if (pci_str) { strncpy(pci, pci_str, sizeof(pci) - 1); free(pci_str); }

    printf("收到锁小区请求: Technology=%s, ARFCN=%s, PCI=%s\n", technology, arfcn, pci);

    /* 确定 band 参数 */
    const char *band = "12"; /* 4G */
    if (strstr(technology, "5G") || strstr(technology, "NR") ||
        strstr(technology, "5g") || strstr(technology, "nr")) {
        band = "16"; /* 5G */
    }

    char *result = NULL;
    char cmd[128];

    /* 1. 关闭射频 */
    execute_at("AT+SFUN=5", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 2. 解锁4G */
    execute_at("AT+SPFORCEFRQ=12,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 3. 解锁5G */
    execute_at("AT+SPFORCEFRQ=16,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 4. 锁定小区 */
    snprintf(cmd, sizeof(cmd), "AT+SPFORCEFRQ=%s,2,%s,%s", band, arfcn, pci);
    execute_at(cmd, &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 5. 打开射频 */
    execute_at("AT+SFUN=4", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 6. 激活网络 */
    execute_at("AT+CGACT=0,1", &result);
    if (result) g_free(result);

    printf("小区锁定成功\n");
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_bool(j, "success", 1);
    json_add_str(j, "message", "小区锁定成功");
    json_obj_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}

/* POST /api/unlock_cell - 解锁小区 */
void handle_unlock_cell(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    printf("开始解锁小区...\n");
    char *result = NULL;

    /* 1. 关闭射频 */
    execute_at("AT+SFUN=5", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 2. 解锁4G */
    execute_at("AT+SPFORCEFRQ=12,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 3. 解锁5G */
    execute_at("AT+SPFORCEFRQ=16,0", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 4. 打开射频 */
    execute_at("AT+SFUN=4", &result);
    if (result) { g_free(result); result = NULL; }
    usleep(300000);

    /* 5. 激活网络 */
    execute_at("AT+CGACT=0,1", &result);
    if (result) g_free(result);

    printf("小区解锁成功\n");
    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_add_int(j, "Code", 0);
    json_add_str(j, "Error", "");
    json_key_obj_open(j, "Data");
    json_add_bool(j, "success", 1);
    json_add_str(j, "message", "小区解锁成功");
    json_obj_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));
}
