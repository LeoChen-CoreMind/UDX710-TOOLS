# 插件商城完整使用指南

## 目录

1. [概述](#概述)
2. [快速开始](#快速开始)
3. [插件开发详解](#插件开发详解)
4. [脚本管理](#脚本管理)
5. [API参考](#api参考)
6. [示例插件](#示例插件)
7. [常见问题](#常见问题)

---

## 概述

插件商城是一个强大的扩展系统，允许用户：
- 上传、管理和运行自定义JavaScript插件
- 创建和管理Shell脚本
- 通过插件执行系统命令
- 自定义UI界面与交互

### 存储路径
- 插件: `/home/root/6677/Plugins/plugins/`
- 脚本: `/home/root/6677/Plugins/scripts/`

### 技术架构
```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Vue前端       │────▶│   C语言后端     │────▶│   Linux系统     │
│  PluginStore    │     │   handlers.c    │     │   Shell执行     │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

---

## 快速开始

### 1. 上传第一个插件

1. 点击顶部 **"插件"** 按钮打开上传弹窗
2. 拖拽 `.js` 文件或粘贴代码
3. 输入插件名称（可选）
4. 点击 **"上传"**

### 2. 运行插件

1. 在插件卡片上点击 **"运行"** 按钮
2. 插件将在弹窗中执行
3. 可以与插件UI交互

### 3. 管理脚本

1. 点击顶部 **"脚本"** 按钮
2. 新建、编辑、删除脚本
3. 支持导入/导出

---

## 插件开发详解

### 基本结构

每个插件必须定义 `window.PLUGIN` 对象：

```javascript
window.PLUGIN = {
  // ========== 元信息（必填）==========
  name: '我的插件',           // 插件名称
  version: '1.0.0',          // 版本号
  author: '作者名',          // 作者
  description: '插件描述',    // 简短描述
  
  // ========== 外观配置（可选）==========
  icon: 'fa-rocket',         // FontAwesome图标名
  color: 'from-blue-500 to-cyan-400',  // 渐变色类名
  
  // ========== HTML模板 ==========
  template: `
    <div class="p-4">
      <h2 class="text-xl font-bold mb-4">{{ title }}</h2>
      <button @click="run">执行</button>
    </div>
  `,
  
  // ========== 响应式数据 ==========
  data() {
    return {
      title: 'Hello World',
      count: 0,
      result: ''
    }
  },
  
  // ========== 方法定义 ==========
  methods: {
    async run() {
      const output = await this.$api.shell('uname -a')
      this.$data.result = output
      this.$refresh()  // 刷新UI
    }
  },
  
  // ========== 生命周期 ==========
  mounted() {
    console.log('插件已加载')
  }
}
```

### 元信息字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | ✅ | 插件显示名称 |
| `version` | string | ✅ | 语义化版本号 |
| `author` | string | ✅ | 作者名称 |
| `description` | string | ❌ | 插件功能描述 |
| `icon` | string | ❌ | FontAwesome图标（不含fa-前缀也可） |
| `color` | string | ❌ | Tailwind渐变色类名 |

### 可用图标示例

```
rocket, terminal, cog, wifi, server, database, 
chart-line, shield-alt, sync-alt, download, upload,
file-code, folder, search, bell, user, lock
```

### 可用渐变色

```
from-blue-500 to-cyan-400      // 蓝青
from-violet-500 to-purple-500  // 紫色
from-emerald-500 to-teal-500   // 绿色
from-orange-500 to-red-500     // 橙红
from-pink-500 to-rose-500      // 粉色
from-amber-500 to-yellow-500   // 金黄
```

---

### 模板语法

#### 变量绑定
```html
<span>{{ variableName }}</span>
<p>计数: {{ count }}</p>
```

#### 点击事件
```html
<button @click="methodName">点击我</button>
<button @click="run">执行命令</button>
```

#### 双向绑定（输入框）
```html
<input v-model="inputText" type="text" />
<textarea v-model="content"></textarea>
```

#### 变更事件
```html
<select @change="onSelect">
  <option value="1">选项1</option>
</select>
```

---

### 内置API详解

#### 1. `$api.shell(command)` - 执行Shell命令

```javascript
// 基本用法
const result = await this.$api.shell('ls -la')

// 执行多条命令
const result = await this.$api.shell('cd /tmp && ls')

// 获取系统信息
const uname = await this.$api.shell('uname -a')
const uptime = await this.$api.shell('uptime')
const memory = await this.$api.shell('free -h')
```

#### 2. `$api.toast(message, type)` - 显示提示

```javascript
// 成功提示（绿色）
this.$api.toast('操作成功', 'success')

// 错误提示（红色）
this.$api.toast('操作失败', 'error')

// 信息提示（蓝色）
this.$api.toast('正在处理...', 'info')
```

#### 3. `$api.alert(title, message)` - 显示弹窗

```javascript
this.$api.alert('提示', '这是一条消息')
```

#### 4. `$api.confirm(title, message)` - 确认对话框

```javascript
const confirmed = await this.$api.confirm('确认', '是否继续操作？')
if (confirmed) {
  // 用户点击了确定
  await this.$api.shell('rm -rf /tmp/cache')
}
```

#### 5. `$api.runScript(scriptName)` - 执行已上传的脚本

```javascript
// 执行脚本（脚本需先在脚本管理中上传）
const output = await this.$api.runScript('backup.sh')
const result = await this.$api.runScript('cleanup.sh')
```

#### 6. `this.$data` - 访问插件数据

```javascript
// 读取数据
const currentCount = this.$data.count

// 修改数据
this.$data.count++
this.$data.result = 'new value'
```

#### 7. `this.$refresh()` - 刷新UI

```javascript
// 数据修改后刷新界面
this.$data.count++
this.$refresh()
```

---

## 脚本管理

### 功能概述

脚本管理允许你创建和管理Shell脚本，这些脚本可以被插件调用。

### 操作指南

#### 新建脚本
1. 点击 **"脚本"** 按钮打开管理弹窗
2. 点击 **"新建"** 按钮
3. 输入脚本名称（如 `backup.sh`）
4. 编写脚本内容
5. 点击 **"保存"**

#### 编辑脚本
1. 鼠标悬停在脚本项上
2. 点击 **编辑** 图标
3. 修改内容后点击 **"保存"**

#### 导出脚本
- **单个导出**: 点击脚本项的 **下载** 图标
- **批量导出**: 点击顶部 **导出** 图标，生成JSON备份文件

#### 导入脚本
- 点击 **导入** 图标
- 支持 `.sh` 单文件或 `.json` 批量导入

### 脚本示例

#### 系统信息脚本
```bash
#!/bin/bash
echo "=== 系统信息 ==="
echo "主机名: $(hostname)"
echo "内核: $(uname -r)"
echo "运行时间: $(uptime -p)"
echo ""
echo "=== 内存使用 ==="
free -h
echo ""
echo "=== 磁盘使用 ==="
df -h /
```

#### 网络检测脚本
```bash
#!/bin/bash
echo "=== 网络接口 ==="
ip addr show | grep -E "^[0-9]+:|inet "
echo ""
echo "=== 路由表 ==="
ip route
echo ""
echo "=== DNS服务器 ==="
cat /etc/resolv.conf | grep nameserver
```

#### 清理缓存脚本
```bash
#!/bin/bash
echo "清理前:"
df -h /tmp
rm -rf /tmp/cache/*
sync
echo ""
echo "清理后:"
df -h /tmp
echo "清理完成!"
```

---

## API参考

### HTTP接口

#### 插件管理

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/plugins` | 获取插件列表 |
| POST | `/api/plugins` | 上传插件 |
| DELETE | `/api/plugins/:name` | 删除指定插件 |
| DELETE | `/api/plugins/all` | 删除所有插件 |

#### 脚本管理

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/scripts` | 获取脚本列表 |
| POST | `/api/scripts` | 上传脚本 |
| PUT | `/api/scripts/:name` | 更新脚本 |
| DELETE | `/api/scripts/:name` | 删除脚本 |

#### Shell执行

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/shell` | 执行Shell命令 |

### 请求/响应示例

#### 上传插件
```http
POST /api/plugins
Content-Type: application/json

{
  "name": "my_plugin",
  "content": "window.PLUGIN = { name: 'Test', ... }"
}
```

#### 执行Shell
```http
POST /api/shell
Content-Type: application/json

{"command": "ls -la /home"}
```

响应:
```json
{
  "Code": 0,
  "Error": "",
  "Data": "total 4\ndrwxr-xr-x 2 root root 4096 ..."
}
```

---

## 示例插件

### 示例1: 系统监控插件

```javascript
window.PLUGIN = {
  name: '系统监控',
  version: '1.0.0',
  author: 'Admin',
  description: '实时查看系统状态',
  icon: 'fa-chart-line',
  color: 'from-emerald-500 to-teal-500',
  
  template: `
    <div class="space-y-4">
      <div class="grid grid-cols-2 gap-4">
        <div class="p-4 bg-blue-500/10 rounded-xl">
          <p class="text-xs text-blue-400 mb-1">CPU使用率</p>
          <p class="text-2xl font-bold text-blue-500">{{ cpu }}%</p>
        </div>
        <div class="p-4 bg-green-500/10 rounded-xl">
          <p class="text-xs text-green-400 mb-1">内存使用</p>
          <p class="text-2xl font-bold text-green-500">{{ memory }}%</p>
        </div>
      </div>
      <button @click="refresh" class="w-full">刷新数据</button>
      <pre class="text-xs">{{ detail }}</pre>
    </div>
  `,
  
  data() {
    return { cpu: '0', memory: '0', detail: '' }
  },
  
  methods: {
    async refresh() {
      this.$api.toast('正在获取...', 'info')
      
      // 获取CPU使用率
      const cpuResult = await this.$api.shell("top -bn1 | grep 'Cpu(s)' | awk '{print $2}'")
      this.$data.cpu = parseFloat(cpuResult).toFixed(1)
      
      // 获取内存使用率
      const memResult = await this.$api.shell("free | grep Mem | awk '{print $3/$2 * 100}'")
      this.$data.memory = parseFloat(memResult).toFixed(1)
      
      // 获取详细信息
      this.$data.detail = await this.$api.shell('free -h && echo "" && df -h /')
      
      this.$refresh()
      this.$api.toast('刷新完成', 'success')
    }
  },
  
  mounted() {
    this.refresh()
  }
}
```

### 示例2: 网络工具插件

```javascript
window.PLUGIN = {
  name: '网络工具',
  version: '1.0.0',
  author: 'Admin',
  description: 'Ping测试和网络诊断',
  icon: 'fa-wifi',
  color: 'from-blue-500 to-cyan-400',
  
  template: `
    <div class="space-y-4">
      <div class="flex gap-2">
        <input v-model="host" type="text" placeholder="输入IP或域名" class="flex-1" />
        <button @click="ping">Ping</button>
      </div>
      <div class="flex gap-2">
        <button @click="checkDns">DNS查询</button>
        <button @click="showRoutes">路由表</button>
        <button @click="showInterfaces">网卡信息</button>
      </div>
      <pre class="text-xs bg-black/50 p-3 rounded-lg text-green-400 max-h-64 overflow-auto">{{ output }}</pre>
    </div>
  `,
  
  data() {
    return { host: '8.8.8.8', output: '等待操作...' }
  },
  
  methods: {
    async ping() {
      if (!this.$data.host) {
        this.$api.toast('请输入目标地址', 'error')
        return
      }
      this.$data.output = '正在Ping...'
      this.$refresh()
      this.$data.output = await this.$api.shell(`ping -c 4 ${this.$data.host}`)
      this.$refresh()
    },
    
    async checkDns() {
      if (!this.$data.host) return
      this.$data.output = await this.$api.shell(`nslookup ${this.$data.host}`)
      this.$refresh()
    },
    
    async showRoutes() {
      this.$data.output = await this.$api.shell('ip route')
      this.$refresh()
    },
    
    async showInterfaces() {
      this.$data.output = await this.$api.shell('ip addr')
      this.$refresh()
    }
  }
}
```

### 示例3: 脚本执行器插件

```javascript
window.PLUGIN = {
  name: '脚本执行器',
  version: '1.0.0',
  author: 'Admin',
  description: '快速执行已上传的脚本',
  icon: 'fa-terminal',
  color: 'from-amber-500 to-orange-500',
  
  template: `
    <div class="space-y-4">
      <p class="text-sm text-white/60">选择要执行的脚本：</p>
      <div class="flex flex-wrap gap-2">
        <button @click="runBackup">backup.sh</button>
        <button @click="runCleanup">cleanup.sh</button>
        <button @click="runStatus">status.sh</button>
      </div>
      <div class="mt-4">
        <p class="text-xs text-white/40 mb-2">自定义脚本名：</p>
        <div class="flex gap-2">
          <input v-model="scriptName" type="text" placeholder="脚本名.sh" class="flex-1" />
          <button @click="runCustom">执行</button>
        </div>
      </div>
      <pre class="text-xs bg-black/50 p-3 rounded-lg text-green-400 max-h-64 overflow-auto">{{ output }}</pre>
    </div>
  `,
  
  data() {
    return { scriptName: '', output: '等待执行...' }
  },
  
  methods: {
    async runBackup() {
      this.$data.output = '执行 backup.sh...\n'
      this.$refresh()
      this.$data.output += await this.$api.runScript('backup.sh')
      this.$refresh()
    },
    
    async runCleanup() {
      const confirmed = await this.$api.confirm('确认', '确定要执行清理脚本吗？')
      if (!confirmed) return
      this.$data.output = '执行 cleanup.sh...\n'
      this.$refresh()
      this.$data.output += await this.$api.runScript('cleanup.sh')
      this.$refresh()
    },
    
    async runStatus() {
      this.$data.output = '执行 status.sh...\n'
      this.$refresh()
      this.$data.output += await this.$api.runScript('status.sh')
      this.$refresh()
    },
    
    async runCustom() {
      if (!this.$data.scriptName) {
        this.$api.toast('请输入脚本名', 'error')
        return
      }
      this.$data.output = `执行 ${this.$data.scriptName}...\n`
      this.$refresh()
      this.$data.output += await this.$api.runScript(this.$data.scriptName)
      this.$refresh()
    }
  }
}
```

### 示例4: 文件管理器插件

```javascript
window.PLUGIN = {
  name: '文件管理器',
  version: '1.0.0',
  author: 'Admin',
  description: '浏览和管理文件',
  icon: 'fa-folder',
  color: 'from-yellow-500 to-amber-500',
  
  template: `
    <div class="space-y-4">
      <div class="flex gap-2">
        <input v-model="path" type="text" placeholder="路径" class="flex-1" />
        <button @click="browse">浏览</button>
        <button @click="goUp">上级</button>
      </div>
      <div class="flex gap-2 text-xs">
        <button @click="goHome">主目录</button>
        <button @click="goTmp">/tmp</button>
        <button @click="goPlugins">插件目录</button>
      </div>
      <pre class="text-xs bg-black/50 p-3 rounded-lg text-cyan-400 max-h-80 overflow-auto">{{ files }}</pre>
    </div>
  `,
  
  data() {
    return { path: '/home/root', files: '' }
  },
  
  methods: {
    async browse() {
      this.$data.files = await this.$api.shell(`ls -la "${this.$data.path}"`)
      this.$refresh()
    },
    
    goUp() {
      const parts = this.$data.path.split('/').filter(p => p)
      parts.pop()
      this.$data.path = '/' + parts.join('/')
      this.browse()
    },
    
    goHome() {
      this.$data.path = '/home/root'
      this.browse()
    },
    
    goTmp() {
      this.$data.path = '/tmp'
      this.browse()
    },
    
    goPlugins() {
      this.$data.path = '/home/root/6677/Plugins/plugins'
      this.browse()
    }
  },
  
  mounted() {
    this.browse()
  }
}
```

---

## 常见问题

### Q: 插件上传失败？
A: 检查以下几点：
- 文件必须是 `.js` 格式
- 代码必须定义 `window.PLUGIN` 对象
- 文件大小不能超过 100KB

### Q: Shell命令执行没有输出？
A: 可能原因：
- 命令执行时间过长
- 命令语法错误
- 权限不足

### Q: 如何调试插件？
A: 
- 使用 `console.log()` 输出调试信息
- 在浏览器开发者工具中查看控制台
- 使用 `$api.toast()` 显示中间结果

### Q: 脚本执行报错？
A: 检查：
- 脚本第一行是否有 `#!/bin/bash`
- 脚本是否有执行权限
- 脚本路径是否正确

### Q: 如何在插件间共享数据？
A: 目前插件是独立运行的，可以通过：
- 写入临时文件共享
- 使用Shell命令传递

---

## 安全提示

⚠️ **重要安全说明**：

1. Shell命令在服务器端执行，请谨慎使用
2. 不要执行来源不明的插件
3. 避免在插件中硬编码敏感信息
4. 定期备份重要脚本

---

*文档版本: 1.0.0 | 最后更新: 2024年12月*
