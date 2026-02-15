/**
 * @file netif.c
 * @brief 网络接口监控模块实现
 */

#include "netif.h"
#include "database.h"
#include "exec_utils.h"
#include "http_utils.h"
#include "json_builder.h"
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#define VNSTAT_PATH "/home/root/6677/vnstat"

/* 全局监听器数组 */
static NetifMonitor g_monitors[MAX_NET_INTERFACES];
static pthread_mutex_t g_monitors_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * 解析ifconfig输出获取接口列表
 */
static int parse_ifconfig_output(const char *output, NetInterface *interfaces,
                                 int max_count) {
  int count = 0;
  const char *p = output;
  NetInterface *iface = NULL;

  while (*p && count < max_count) {
    /* 检测接口名称行（不以空格开头） */
    if (!isspace(*p)) {
      /* 新接口开始 */
      iface = &interfaces[count];
      memset(iface, 0, sizeof(NetInterface));

      /* 提取接口名称 */
      const char *name_end = p;
      while (*name_end && !isspace(*name_end))
        name_end++;
      size_t name_len = name_end - p;
      if (name_len >= sizeof(iface->name))
        name_len = sizeof(iface->name) - 1;
      strncpy(iface->name, p, name_len);
      iface->name[name_len] = '\0';

      count++;
    }

    if (iface) {
      /* 解析HWaddr */
      const char *hw = strstr(p, "HWaddr ");
      if (hw && (hw - p) < 200) {
        hw += 7;
        const char *hw_end = hw;
        while (*hw_end && !isspace(*hw_end) && *hw_end != '\n')
          hw_end++;
        size_t hw_len = hw_end - hw;
        if (hw_len < sizeof(iface->hwaddr)) {
          strncpy(iface->hwaddr, hw, hw_len);
          iface->hwaddr[hw_len] = '\0';
        }
      }

      /* 解析inet addr */
      const char *inet = strstr(p, "inet addr:");
      if (inet && (inet - p) < 200) {
        inet += 10;
        const char *inet_end = inet;
        while (*inet_end && !isspace(*inet_end))
          inet_end++;
        size_t inet_len = inet_end - inet;
        if (inet_len < sizeof(iface->inet_addr)) {
          strncpy(iface->inet_addr, inet, inet_len);
          iface->inet_addr[inet_len] = '\0';
        }
      }

      /* 解析Mask */
      const char *mask = strstr(p, "Mask:");
      if (mask && (mask - p) < 200) {
        mask += 5;
        const char *mask_end = mask;
        while (*mask_end && !isspace(*mask_end) && *mask_end != '\n')
          mask_end++;
        size_t mask_len = mask_end - mask;
        if (mask_len < sizeof(iface->mask)) {
          strncpy(iface->mask, mask, mask_len);
          iface->mask[mask_len] = '\0';
        }
      }

      /* 解析inet6 addr */
      const char *inet6 = strstr(p, "inet6 addr:");
      if (inet6 && (inet6 - p) < 200) {
        inet6 += 12;
        while (*inet6 == ' ')
          inet6++;
        const char *inet6_end = inet6;
        while (*inet6_end && !isspace(*inet6_end))
          inet6_end++;
        size_t inet6_len = inet6_end - inet6;
        if (inet6_len < sizeof(iface->inet6_addr)) {
          strncpy(iface->inet6_addr, inet6, inet6_len);
          iface->inet6_addr[inet6_len] = '\0';
        }
      }

      /* 检查UP状态 */
      if (strstr(p, "UP ") || strstr(p, "UP\n")) {
        iface->is_up = 1;
      }
    }

    /* 移动到下一行 */
    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      p++;
  }

  return count;
}

/**
 * 初始化网络接口模块
 */
void init_netif(void) {
  printf("[NETIF] 网络接口模块初始化\n");

  /* 初始化所有监听器 */
  pthread_mutex_lock(&g_monitors_lock);
  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    memset(&g_monitors[i], 0, sizeof(NetifMonitor));
    pthread_mutex_init(&g_monitors[i].lock, NULL);
  }
  pthread_mutex_unlock(&g_monitors_lock);

  /* 自动恢复已启用的监听 */
  NetInterface interfaces[MAX_NET_INTERFACES];
  int count = netif_get_list(interfaces, MAX_NET_INTERFACES);

  for (int i = 0; i < count; i++) {
    char key[64];
    snprintf(key, sizeof(key), "netif_monitor_%s", interfaces[i].name);
    int enabled = config_get_int(key, 0);

    if (enabled) {
      printf("[NETIF] 自动启动 %s 的监听\n", interfaces[i].name);
      if (netif_start_monitor_process(interfaces[i].name) == 0) {
        printf("[NETIF] %s 监听已恢复\n", interfaces[i].name);
      } else {
        printf("[NETIF] %s 监听恢复失败\n", interfaces[i].name);
      }
    }
  }
}

/**
 * 获取所有网络接口列表
 */
int netif_get_list(NetInterface *interfaces, int max_count) {
  char output[8192] = {0};

  if (run_command(output, sizeof(output), "ifconfig", NULL) != 0) {
    printf("[NETIF] 执行ifconfig失败\n");
    return 0;
  }

  int count = parse_ifconfig_output(output, interfaces, max_count);

  /* 加载监听状态 */
  for (int i = 0; i < count; i++) {
    interfaces[i].monitoring = netif_get_monitor_status(interfaces[i].name);
  }

  return count;
}

/**
 * vnstat 输出读取线程
 */
static void *vnstat_reader_thread(void *arg) {
  NetifMonitor *mon = (NetifMonitor *)arg;
  FILE *fp = fdopen(mon->pipefd, "r");
  if (!fp) {
    return NULL;
  }

  char line[4096];

  /* 跳过第1行元数据 */
  if (fgets(line, sizeof(line), fp) == NULL) {
    fclose(fp);
    return NULL;
  }

  /* 持续读取统计数据 */
  while (mon->running && fgets(line, sizeof(line), fp)) {
    /* 去除换行符 */
    line[strcspn(line, "\r\n")] = 0;

    /* 解析 JSON */
    struct mg_str json = mg_str(line);
    NetifStats stats;
    memset(&stats, 0, sizeof(stats));

    stats.index = (int)mg_json_get_long(json, "$.index", 0);
    if (stats.index == 0)
      continue; /* 跳过无效数据 */

    stats.seconds = (int)mg_json_get_long(json, "$.seconds", 0);

    /* RX数据 */
    char *ratestr = mg_json_get_str(json, "$.rx.ratestring");
    if (ratestr) {
      strncpy(stats.rx.ratestring, ratestr, sizeof(stats.rx.ratestring) - 1);
      free(ratestr);
    }
    stats.rx.bytespersecond = mg_json_get_long(json, "$.rx.bytespersecond", 0);
    stats.rx.packetspersecond =
        mg_json_get_long(json, "$.rx.packetspersecond", 0);
    stats.rx.bytes = mg_json_get_long(json, "$.rx.bytes", 0);
    stats.rx.packets = mg_json_get_long(json, "$.rx.packets", 0);
    stats.rx.totalbytes = mg_json_get_long(json, "$.rx.totalbytes", 0);
    stats.rx.totalpackets = mg_json_get_long(json, "$.rx.totalpackets", 0);

    /* TX数据 */
    ratestr = mg_json_get_str(json, "$.tx.ratestring");
    if (ratestr) {
      strncpy(stats.tx.ratestring, ratestr, sizeof(stats.tx.ratestring) - 1);
      free(ratestr);
    }
    stats.tx.bytespersecond = mg_json_get_long(json, "$.tx.bytespersecond", 0);
    stats.tx.packetspersecond =
        mg_json_get_long(json, "$.tx.packetspersecond", 0);
    stats.tx.bytes = mg_json_get_long(json, "$.tx.bytes", 0);
    stats.tx.packets = mg_json_get_long(json, "$.tx.packets", 0);
    stats.tx.totalbytes = mg_json_get_long(json, "$.tx.totalbytes", 0);
    stats.tx.totalpackets = mg_json_get_long(json, "$.tx.totalpackets", 0);

    /* 更新缓存 */
    pthread_mutex_lock(&mon->lock);
    memcpy(&mon->latest_stats, &stats, sizeof(NetifStats));
    mon->last_update = time(NULL);
    pthread_mutex_unlock(&mon->lock);
  }

  fclose(fp);
  return NULL;
}

/**
 * 启动接口监听进程
 */
int netif_start_monitor_process(const char *ifname) {
  pthread_mutex_lock(&g_monitors_lock);

  /* 先检查是否已经在监听 */
  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    if (g_monitors[i].vnstat_pid > 0 &&
        strcmp(g_monitors[i].ifname, ifname) == 0) {
      pthread_mutex_unlock(&g_monitors_lock);
      printf("[NETIF] %s 已在监听中\n", ifname);
      return 0; /* 已在监听,直接返回成功 */
    }
  }

  /* 查找空闲槽位 */
  NetifMonitor *mon = NULL;
  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    if (g_monitors[i].vnstat_pid == 0) {
      mon = &g_monitors[i];
      break;
    }
  }

  if (!mon) {
    pthread_mutex_unlock(&g_monitors_lock);
    printf("[NETIF] 无可用监听槽位\n");
    return -1;
  }

  /* 创建管道 */
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    pthread_mutex_unlock(&g_monitors_lock);
    return -1;
  }

  /* fork vnstat -l 进程 */
  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    pthread_mutex_unlock(&g_monitors_lock);
    return -1;
  }

  if (pid == 0) {
    /* 子进程 */
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    execl(VNSTAT_PATH, "vnstat", "-l", "-i", ifname, "--json", NULL);
    _exit(127);
  }

  /* 父进程: 初始化监听器 */
  close(pipefd[1]);
  strncpy(mon->ifname, ifname, sizeof(mon->ifname) - 1);
  mon->vnstat_pid = pid;
  mon->pipefd = pipefd[0];
  mon->running = 1;
  memset(&mon->latest_stats, 0, sizeof(NetifStats));
  mon->last_update = 0;

  /* 创建读取线程 */
  if (pthread_create(&mon->reader_thread, NULL, vnstat_reader_thread, mon) !=
      0) {
    close(pipefd[0]);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    memset(mon, 0, sizeof(NetifMonitor));
    pthread_mutex_unlock(&g_monitors_lock);
    return -1;
  }

  pthread_detach(mon->reader_thread);
  pthread_mutex_unlock(&g_monitors_lock);

  printf("[NETIF] 启动监听: %s (PID=%d)\n", ifname, pid);
  return 0;
}

/**
 * 停止接口监听进程
 */
int netif_stop_monitor_process(const char *ifname) {
  pthread_mutex_lock(&g_monitors_lock);

  /* 查找监听器 */
  NetifMonitor *mon = NULL;
  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    if (strcmp(g_monitors[i].ifname, ifname) == 0 &&
        g_monitors[i].vnstat_pid > 0) {
      mon = &g_monitors[i];
      break;
    }
  }

  if (!mon) {
    pthread_mutex_unlock(&g_monitors_lock);
    printf("[NETIF] %s 未在监听中,无需停止\n", ifname);
    return 0; /* 幂等操作: 已经没有监听,直接返回成功 */
  }

  printf("[NETIF] 停止监听: %s (PID=%d)\n", ifname, mon->vnstat_pid);

  /* 停止线程 */
  mon->running = 0;

  /* 终止 vnstat 进程 */
  if (mon->vnstat_pid > 0) {
    kill(mon->vnstat_pid, SIGTERM);
    waitpid(mon->vnstat_pid, NULL, 0);
  }

  /* 关闭管道 */
  if (mon->pipefd > 0) {
    close(mon->pipefd);
  }

  /* 清理 (保留并重新初始化锁) */
  pthread_mutex_t temp_lock = mon->lock;
  pthread_mutex_destroy(&temp_lock); /* 先销毁再重新初始化，避免资源泄露 */
  memset(mon, 0, sizeof(NetifMonitor));
  pthread_mutex_init(&mon->lock, NULL);
  pthread_mutex_unlock(&g_monitors_lock);

  return 0;
}

/**
 * 清理所有监听进程
 */
void netif_cleanup_all_monitors(void) {
  pthread_mutex_lock(&g_monitors_lock);

  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    if (g_monitors[i].vnstat_pid > 0) {
      g_monitors[i].running = 0;
      kill(g_monitors[i].vnstat_pid, SIGTERM);
      waitpid(g_monitors[i].vnstat_pid, NULL, 0);
      if (g_monitors[i].pipefd > 0) {
        close(g_monitors[i].pipefd);
      }
    }
  }

  pthread_mutex_unlock(&g_monitors_lock);
  printf("[NETIF] 所有监听已清理\n");
}

/**
 * 获取指定接口的实时流量统计
 */
int netif_get_stats(const char *ifname, NetifStats *stats) {
  memset(stats, 0, sizeof(NetifStats));

  pthread_mutex_lock(&g_monitors_lock);

  /* 查找监听器 */
  NetifMonitor *mon = NULL;
  int mon_idx = -1;
  for (int i = 0; i < MAX_NET_INTERFACES; i++) {
    if (strcmp(g_monitors[i].ifname, ifname) == 0 &&
        g_monitors[i].vnstat_pid > 0) {
      mon = &g_monitors[i];
      mon_idx = i;
      break;
    }
  }

  if (!mon) {
    pthread_mutex_unlock(&g_monitors_lock);
    return -1; /* 监听未启动 */
  }

  /* 检查 vnstat 进程是否仍在运行 */
  int wstatus;
  pid_t result = waitpid(mon->vnstat_pid, &wstatus, WNOHANG);
  if (result == mon->vnstat_pid || result == -1) {
    /* 进程已退出，清理并尝试重启 */
    printf("[NETIF] %s 的 vnstat 进程已退出，正在重启...\n", ifname);
    mon->running = 0;
    if (mon->pipefd > 0) {
      close(mon->pipefd);
      mon->pipefd = 0;
    }
    /* 清理监听器槽位 */
    pthread_mutex_t temp_lock = mon->lock;
    memset(mon, 0, sizeof(NetifMonitor));
    mon->lock = temp_lock;
    pthread_mutex_unlock(&g_monitors_lock);

    /* 尝试重启监听 */
    netif_start_monitor_process(ifname);
    return -2; /* 返回 -2 表示正在重启 */
  }

  /* 读取缓存数据 */
  pthread_mutex_lock(&mon->lock);
  memcpy(stats, &mon->latest_stats, sizeof(NetifStats));
  pthread_mutex_unlock(&mon->lock);

  pthread_mutex_unlock(&g_monitors_lock);
  return 0;
}

/**
 * 获取接口监听状态
 */
int netif_get_monitor_status(const char *ifname) {
  char key[64];
  snprintf(key, sizeof(key), "netif_monitor_%s", ifname);
  return config_get_int(key, 0);
}

/**
 * 设置接口监听状态
 */
int netif_set_monitor(const char *ifname, int enabled) {
  char key[64];
  snprintf(key, sizeof(key), "netif_monitor_%s", ifname);

  if (enabled) {
    /* 启动监听 */
    if (netif_start_monitor_process(ifname) == 0) {
      config_set_int(key, 1);
      return 0;
    }
    return -1;
  } else {
    /* 停止监听 */
    if (netif_stop_monitor_process(ifname) == 0) {
      config_set_int(key, 0);
      return 0;
    }
    return -1;
  }
}

/* ==================== HTTP API处理函数 ==================== */

/**
 * GET /api/netif/list - 获取所有网络接口列表
 */
void handle_netif_list(struct mg_connection *c, struct mg_http_message *hm) {
  HTTP_CHECK_GET(c, hm);

  NetInterface interfaces[MAX_NET_INTERFACES];
  int count = netif_get_list(interfaces, MAX_NET_INTERFACES);

  JsonBuilder *j = json_new();
  json_obj_open(j);
  json_arr_open(j, "interfaces");

  for (int i = 0; i < count; i++) {
    json_arr_obj_open(j);
    json_add_str(j, "name", interfaces[i].name);
    json_add_str(j, "hwaddr", interfaces[i].hwaddr);
    json_add_str(j, "inet_addr", interfaces[i].inet_addr);
    json_add_str(j, "inet6_addr", interfaces[i].inet6_addr);
    json_add_str(j, "mask", interfaces[i].mask);
    json_add_bool(j, "is_up", interfaces[i].is_up);
    json_add_bool(j, "monitoring", interfaces[i].monitoring);
    json_obj_close(j);
  }

  json_arr_close(j);
  json_obj_close(j);

  HTTP_OK_FREE(c, json_finish(j));
}

/**
 * POST /api/netif/stats - 获取指定接口实时流量统计
 */
void handle_netif_stats(struct mg_connection *c, struct mg_http_message *hm) {
  HTTP_CHECK_POST(c, hm);

  char ifname[32] = {0};
  char *ifname_str = mg_json_get_str(hm->body, "$.interface");
  if (ifname_str) {
    strncpy(ifname, ifname_str, sizeof(ifname) - 1);
    free(ifname_str);
  }

  if (strlen(ifname) == 0) {
    HTTP_ERROR(c, 400, "interface参数不能为空");
    return;
  }

  NetifStats stats;
  int ret = netif_get_stats(ifname, &stats);
  if (ret == -2) {
    HTTP_ERROR(c, 503, "监听正在重启中，请稍后再试");
    return;
  }
  if (ret != 0) {
    HTTP_ERROR(c, 500, "获取流量统计失败");
    return;
  }

  JsonBuilder *j = json_new();
  json_obj_open(j);
  json_add_int(j, "index", stats.index);
  json_add_int(j, "seconds", stats.seconds);

  /* RX */
  json_key_obj_open(j, "rx");
  json_add_str(j, "ratestring", stats.rx.ratestring);
  json_add_long(j, "bytespersecond", stats.rx.bytespersecond);
  json_add_long(j, "packetspersecond", stats.rx.packetspersecond);
  json_add_long(j, "bytes", stats.rx.bytes);
  json_add_long(j, "packets", stats.rx.packets);
  json_add_long(j, "totalbytes", stats.rx.totalbytes);
  json_add_long(j, "totalpackets", stats.rx.totalpackets);
  json_obj_close(j);

  /* TX */
  json_key_obj_open(j, "tx");
  json_add_str(j, "ratestring", stats.tx.ratestring);
  json_add_long(j, "bytespersecond", stats.tx.bytespersecond);
  json_add_long(j, "packetspersecond", stats.tx.packetspersecond);
  json_add_long(j, "bytes", stats.tx.bytes);
  json_add_long(j, "packets", stats.tx.packets);
  json_add_long(j, "totalbytes", stats.tx.totalbytes);
  json_add_long(j, "totalpackets", stats.tx.totalpackets);
  json_obj_close(j);

  json_obj_close(j);
  HTTP_OK_FREE(c, json_finish(j));
}

/**
 * GET/POST /api/netif/monitor - 获取/设置监听配置
 */
void handle_netif_monitor(struct mg_connection *c, struct mg_http_message *hm) {
  if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
    /* GET - 获取所有监听配置 */
    NetInterface interfaces[MAX_NET_INTERFACES];
    int count = netif_get_list(interfaces, MAX_NET_INTERFACES);

    JsonBuilder *j = json_new();
    json_obj_open(j);
    json_arr_open(j, "monitors");

    for (int i = 0; i < count; i++) {
      json_arr_obj_open(j);
      json_add_str(j, "interface", interfaces[i].name);
      json_add_bool(j, "enabled", interfaces[i].monitoring);
      json_obj_close(j);
    }

    json_arr_close(j);
    json_obj_close(j);
    HTTP_OK_FREE(c, json_finish(j));

  } else if (hm->method.len == 4 && memcmp(hm->method.buf, "POST", 4) == 0) {
    /* POST - 设置监听状态 */
    char ifname[32] = {0};
    int enabled = 0;

    char *ifname_str = mg_json_get_str(hm->body, "$.interface");
    if (ifname_str) {
      strncpy(ifname, ifname_str, sizeof(ifname) - 1);
      free(ifname_str);
    }

    int val = 0;
    if (mg_json_get_bool(hm->body, "$.enabled", &val)) {
      enabled = val;
    }

    if (strlen(ifname) == 0) {
      HTTP_ERROR(c, 400, "interface参数不能为空");
      return;
    }

    if (netif_set_monitor(ifname, enabled) == 0) {
      JsonBuilder *j = json_new();
      json_obj_open(j);
      json_add_str(j, "status", "success");
      json_add_str(j, "interface", ifname);
      json_add_bool(j, "enabled", enabled);
      json_obj_close(j);
      HTTP_OK_FREE(c, json_finish(j));
    } else {
      HTTP_ERROR(c, 500, "设置监听状态失败");
    }
  } else {
    HTTP_ERROR(c, 405, "Method not allowed");
  }
}
