/**
 * ============================================================================
 * 系统工具箱插件 - 超详细示例
 * ============================================================================
 * 
 * 这是一个功能完整的插件示例，展示了插件开发的所有核心功能：
 * - 元信息配置
 * - HTML模板与数据绑定
 * - 响应式数据管理
 * - 方法定义与API调用
 * - 生命周期钩子
 * - 脚本执行
 * - 用户交互（toast、confirm）
 * 
 * 作者: Admin
 * 版本: 1.0.0
 * 更新: 2024-12
 * ============================================================================
 */

window.PLUGIN = {
  // ============================================================================
  // 第一部分：元信息配置
  // ============================================================================
  // 这些信息会显示在插件卡片上
  
  name: '系统工具箱',           // 插件名称（必填）- 显示在卡片标题
  version: '1.0.0',            // 版本号（必填）- 显示在卡片副标题
  author: 'Admin',             // 作者（必填）- 显示在卡片副标题
  description: '集成系统监控、网络诊断、文件管理、脚本执行等多功能工具',  // 描述
  
  // 图标配置 - 使用FontAwesome图标名
  // 常用图标: rocket, terminal, cog, wifi, server, database, chart-line, 
  //          shield-alt, sync-alt, download, upload, file-code, folder
  icon: 'fa-toolbox',
  
  // 渐变色配置 - 使用Tailwind CSS渐变类
  // 常用配色:
  //   from-blue-500 to-cyan-400      蓝青色
  //   from-violet-500 to-purple-500  紫色
  //   from-emerald-500 to-teal-500   绿色
  //   from-orange-500 to-red-500     橙红色
  //   from-pink-500 to-rose-500      粉色
  color: 'from-indigo-500 to-purple-600',

  // ============================================================================
  // 第二部分：HTML模板
  // ============================================================================
  // 模板语法说明：
  //   {{ variable }}     - 变量绑定，显示data中的值
  //   @click="method"    - 点击事件，调用methods中的方法
  //   v-model="variable" - 双向绑定，用于输入框
  //   @change="method"   - 变更事件，用于select等
  
  template: `
    <div class="space-y-4">
      <!-- ==================== 标签页导航 ==================== -->
      <div class="flex space-x-1 bg-white/5 p-1 rounded-xl">
        <button @click="switchTab" data-tab="monitor" 
          class="flex-1 py-2 px-3 rounded-lg text-sm font-medium transition-all
                 {{ activeTab === 'monitor' ? 'bg-indigo-500 text-white' : 'text-white/60 hover:text-white' }}">
          📊 系统监控
        </button>
        <button @click="switchTab" data-tab="network"
          class="flex-1 py-2 px-3 rounded-lg text-sm font-medium transition-all
                 {{ activeTab === 'network' ? 'bg-indigo-500 text-white' : 'text-white/60 hover:text-white' }}">
          🌐 网络诊断
        </button>
        <button @click="switchTab" data-tab="files"
          class="flex-1 py-2 px-3 rounded-lg text-sm font-medium transition-all
                 {{ activeTab === 'files' ? 'bg-indigo-500 text-white' : 'text-white/60 hover:text-white' }}">
          📁 文件管理
        </button>
        <button @click="switchTab" data-tab="scripts"
          class="flex-1 py-2 px-3 rounded-lg text-sm font-medium transition-all
                 {{ activeTab === 'scripts' ? 'bg-indigo-500 text-white' : 'text-white/60 hover:text-white' }}">
          ⚡ 脚本执行
        </button>
      </div>

      <!-- ==================== 系统监控面板 ==================== -->
      <div id="panel-monitor" style="{{ activeTab === 'monitor' ? '' : 'display:none' }}">
        <div class="grid grid-cols-2 gap-3 mb-4">
          <!-- CPU卡片 -->
          <div class="p-4 bg-gradient-to-br from-blue-500/20 to-blue-600/10 rounded-xl border border-blue-500/20">
            <div class="flex items-center justify-between mb-2">
              <span class="text-blue-400 text-xs font-medium">CPU使用率</span>
              <span class="text-blue-300 text-xs">🔄</span>
            </div>
            <p class="text-3xl font-bold text-blue-400">{{ cpuUsage }}%</p>
            <div class="mt-2 h-1.5 bg-blue-900/50 rounded-full overflow-hidden">
              <div class="h-full bg-blue-500 rounded-full transition-all" style="width: {{ cpuUsage }}%"></div>
            </div>
          </div>
          
          <!-- 内存卡片 -->
          <div class="p-4 bg-gradient-to-br from-green-500/20 to-green-600/10 rounded-xl border border-green-500/20">
            <div class="flex items-center justify-between mb-2">
              <span class="text-green-400 text-xs font-medium">内存使用</span>
              <span class="text-green-300 text-xs">💾</span>
            </div>
            <p class="text-3xl font-bold text-green-400">{{ memUsage }}%</p>
            <p class="text-xs text-green-300/60 mt-1">{{ memUsed }} / {{ memTotal }}</p>
          </div>
          
          <!-- 磁盘卡片 -->
          <div class="p-4 bg-gradient-to-br from-amber-500/20 to-amber-600/10 rounded-xl border border-amber-500/20">
            <div class="flex items-center justify-between mb-2">
              <span class="text-amber-400 text-xs font-medium">磁盘使用</span>
              <span class="text-amber-300 text-xs">💿</span>
            </div>
            <p class="text-3xl font-bold text-amber-400">{{ diskUsage }}%</p>
            <p class="text-xs text-amber-300/60 mt-1">{{ diskUsed }} / {{ diskTotal }}</p>
          </div>
          
          <!-- 运行时间卡片 -->
          <div class="p-4 bg-gradient-to-br from-purple-500/20 to-purple-600/10 rounded-xl border border-purple-500/20">
            <div class="flex items-center justify-between mb-2">
              <span class="text-purple-400 text-xs font-medium">运行时间</span>
              <span class="text-purple-300 text-xs">⏱️</span>
            </div>
            <p class="text-xl font-bold text-purple-400">{{ uptime }}</p>
            <p class="text-xs text-purple-300/60 mt-1">{{ loadAvg }}</p>
          </div>
        </div>
        
        <button @click="refreshMonitor" class="w-full py-2.5 bg-indigo-500 hover:bg-indigo-600 rounded-xl text-sm font-medium">
          🔄 刷新系统状态
        </button>
      </div>

      <!-- ==================== 网络诊断面板 ==================== -->
      <div id="panel-network" style="{{ activeTab === 'network' ? '' : 'display:none' }}">
        <div class="space-y-3">
          <!-- Ping测试 -->
          <div class="p-3 bg-white/5 rounded-xl">
            <p class="text-xs text-white/60 mb-2">🏓 Ping测试</p>
            <div class="flex gap-2">
              <input v-model="pingHost" type="text" placeholder="IP或域名 (如: 8.8.8.8)" 
                class="flex-1 px-3 py-2 bg-white/10 border border-white/20 rounded-lg text-sm" />
              <button @click="doPing" class="px-4 py-2 bg-blue-500 hover:bg-blue-600 rounded-lg text-sm">Ping</button>
            </div>
          </div>
          
          <!-- 快捷操作 -->
          <div class="grid grid-cols-2 gap-2">
            <button @click="showDns" class="p-3 bg-cyan-500/20 hover:bg-cyan-500/30 border border-cyan-500/30 rounded-xl text-sm text-cyan-400">
              🔍 DNS查询
            </button>
            <button @click="showRoutes" class="p-3 bg-teal-500/20 hover:bg-teal-500/30 border border-teal-500/30 rounded-xl text-sm text-teal-400">
              🛤️ 路由表
            </button>
            <button @click="showInterfaces" class="p-3 bg-emerald-500/20 hover:bg-emerald-500/30 border border-emerald-500/30 rounded-xl text-sm text-emerald-400">
              📡 网卡信息
            </button>
            <button @click="showConnections" class="p-3 bg-green-500/20 hover:bg-green-500/30 border border-green-500/30 rounded-xl text-sm text-green-400">
              🔗 连接状态
            </button>
          </div>
        </div>
      </div>

      <!-- ==================== 文件管理面板 ==================== -->
      <div id="panel-files" style="{{ activeTab === 'files' ? '' : 'display:none' }}">
        <div class="space-y-3">
          <!-- 路径导航 -->
          <div class="flex gap-2">
            <input v-model="currentPath" type="text" placeholder="输入路径" 
              class="flex-1 px-3 py-2 bg-white/10 border border-white/20 rounded-lg text-sm font-mono" />
            <button @click="browsePath" class="px-3 py-2 bg-amber-500 hover:bg-amber-600 rounded-lg text-sm">浏览</button>
            <button @click="goParent" class="px-3 py-2 bg-white/10 hover:bg-white/20 rounded-lg text-sm">⬆️</button>
          </div>
          
          <!-- 快捷目录 -->
          <div class="flex flex-wrap gap-1">
            <button @click="goToPath" data-path="/home/root" class="px-2 py-1 bg-white/10 hover:bg-white/20 rounded text-xs">🏠 主目录</button>
            <button @click="goToPath" data-path="/tmp" class="px-2 py-1 bg-white/10 hover:bg-white/20 rounded text-xs">📦 /tmp</button>
            <button @click="goToPath" data-path="/var/log" class="px-2 py-1 bg-white/10 hover:bg-white/20 rounded text-xs">📋 日志</button>
            <button @click="goToPath" data-path="/home/root/6677/Plugins/plugins" class="px-2 py-1 bg-white/10 hover:bg-white/20 rounded text-xs">🔌 插件</button>
            <button @click="goToPath" data-path="/home/root/6677/Plugins/scripts" class="px-2 py-1 bg-white/10 hover:bg-white/20 rounded text-xs">📜 脚本</button>
          </div>
          
          <!-- 文件操作 -->
          <div class="flex gap-2">
            <input v-model="fileName" type="text" placeholder="文件名" 
              class="flex-1 px-3 py-2 bg-white/10 border border-white/20 rounded-lg text-sm" />
            <button @click="viewFile" class="px-3 py-2 bg-blue-500/80 hover:bg-blue-500 rounded-lg text-xs">查看</button>
            <button @click="deleteFile" class="px-3 py-2 bg-red-500/80 hover:bg-red-500 rounded-lg text-xs">删除</button>
          </div>
        </div>
      </div>

      <!-- ==================== 脚本执行面板 ==================== -->
      <div id="panel-scripts" style="{{ activeTab === 'scripts' ? '' : 'display:none' }}">
        <div class="space-y-3">
          <p class="text-xs text-white/60">选择要执行的脚本（需先在脚本管理中上传）：</p>
          
          <!-- 脚本名输入 -->
          <div class="flex gap-2">
            <input v-model="scriptName" type="text" placeholder="脚本名称 (如: backup.sh)" 
              class="flex-1 px-3 py-2 bg-white/10 border border-white/20 rounded-lg text-sm font-mono" />
            <button @click="runScript" class="px-4 py-2 bg-orange-500 hover:bg-orange-600 rounded-lg text-sm font-medium">
              ▶️ 执行
            </button>
          </div>
          
          <!-- 常用脚本快捷按钮 -->
          <div class="grid grid-cols-3 gap-2">
            <button @click="quickScript" data-script="status.sh" 
              class="p-2 bg-emerald-500/20 hover:bg-emerald-500/30 border border-emerald-500/30 rounded-lg text-xs text-emerald-400">
              📊 status.sh
            </button>
            <button @click="quickScript" data-script="backup.sh"
              class="p-2 bg-blue-500/20 hover:bg-blue-500/30 border border-blue-500/30 rounded-lg text-xs text-blue-400">
              💾 backup.sh
            </button>
            <button @click="quickScript" data-script="cleanup.sh"
              class="p-2 bg-red-500/20 hover:bg-red-500/30 border border-red-500/30 rounded-lg text-xs text-red-400">
              🧹 cleanup.sh
            </button>
          </div>
          
          <!-- 自定义命令 -->
          <div class="p-3 bg-white/5 rounded-xl">
            <p class="text-xs text-white/60 mb-2">💻 自定义Shell命令</p>
            <div class="flex gap-2">
              <input v-model="customCmd" type="text" placeholder="输入Shell命令" 
                class="flex-1 px-3 py-2 bg-white/10 border border-white/20 rounded-lg text-sm font-mono" />
              <button @click="runCustomCmd" class="px-4 py-2 bg-violet-500 hover:bg-violet-600 rounded-lg text-sm">执行</button>
            </div>
          </div>
        </div>
      </div>

      <!-- ==================== 输出区域 ==================== -->
      <div class="mt-4">
        <div class="flex items-center justify-between mb-2">
          <span class="text-xs text-white/60">📤 输出结果</span>
          <button @click="clearOutput" class="text-xs text-white/40 hover:text-white/60">清空</button>
        </div>
        <pre class="p-3 bg-black/50 rounded-xl text-xs font-mono text-green-400 max-h-48 overflow-auto whitespace-pre-wrap">{{ output }}</pre>
      </div>
    </div>
  `,

  // ============================================================================
  // 第三部分：响应式数据
  // ============================================================================
  // data() 函数返回插件的所有响应式数据
  // 这些数据可以在模板中通过 {{ variableName }} 访问
  // 在方法中通过 this.$data.variableName 访问和修改
  
  data() {
    return {
      // ----- 标签页状态 -----
      activeTab: 'monitor',      // 当前激活的标签页
      
      // ----- 系统监控数据 -----
      cpuUsage: '0',             // CPU使用率
      memUsage: '0',             // 内存使用率
      memUsed: '0MB',            // 已用内存
      memTotal: '0MB',           // 总内存
      diskUsage: '0',            // 磁盘使用率
      diskUsed: '0GB',           // 已用磁盘
      diskTotal: '0GB',          // 总磁盘
      uptime: '0天0时0分',        // 运行时间
      loadAvg: '负载: 0 0 0',     // 系统负载
      
      // ----- 网络诊断数据 -----
      pingHost: '8.8.8.8',       // Ping目标地址
      
      // ----- 文件管理数据 -----
      currentPath: '/home/root', // 当前路径
      fileName: '',              // 文件名
      
      // ----- 脚本执行数据 -----
      scriptName: '',            // 脚本名称
      customCmd: '',             // 自定义命令
      
      // ----- 通用数据 -----
      output: '等待操作...',      // 输出结果
      isLoading: false           // 加载状态
    }
  },

  // ============================================================================
  // 第四部分：方法定义
  // ============================================================================
  // methods 对象包含所有可调用的方法
  // 方法中可以使用：
  //   this.$api      - 访问插件API（shell、toast、confirm等）
  //   this.$data     - 访问和修改响应式数据
  //   this.$refresh  - 刷新UI（数据修改后需调用）
  
  methods: {
    // ==================== 标签页切换 ====================
    switchTab(tab) {
      // 注意：由于简化的事件系统，这里需要从按钮获取tab值
      // 实际使用时可能需要根据点击的按钮来判断
      if (tab === 'monitor') this.$data.activeTab = 'monitor'
      else if (tab === 'network') this.$data.activeTab = 'network'
      else if (tab === 'files') this.$data.activeTab = 'files'
      else if (tab === 'scripts') this.$data.activeTab = 'scripts'
      this.$refresh()
    },

    // ==================== 系统监控方法 ====================
    async refreshMonitor() {
      this.$api.toast('正在获取系统状态...', 'info')
      this.$data.output = '正在获取系统信息...\n'
      this.$refresh()
      
      try {
        // 获取CPU使用率
        // 使用top命令获取CPU空闲率，然后计算使用率
        const cpuResult = await this.$api.shell("top -bn1 | grep 'Cpu(s)' | awk '{print 100 - $8}'")
        this.$data.cpuUsage = parseFloat(cpuResult || '0').toFixed(1)
        
        // 获取内存信息
        // 使用free命令获取内存使用情况
        const memResult = await this.$api.shell("free -m | grep Mem | awk '{printf \"%.1f|%d|%d\", $3/$2*100, $3, $2}'")
        const memParts = memResult.split('|')
        if (memParts.length >= 3) {
          this.$data.memUsage = memParts[0]
          this.$data.memUsed = memParts[1] + 'MB'
          this.$data.memTotal = memParts[2] + 'MB'
        }
        
        // 获取磁盘信息
        // 使用df命令获取根分区使用情况
        const diskResult = await this.$api.shell("df -h / | tail -1 | awk '{printf \"%s|%s|%s\", $5, $3, $2}'")
        const diskParts = diskResult.split('|')
        if (diskParts.length >= 3) {
          this.$data.diskUsage = parseInt(diskParts[0]) || 0
          this.$data.diskUsed = diskParts[1]
          this.$data.diskTotal = diskParts[2]
        }
        
        // 获取运行时间
        const uptimeResult = await this.$api.shell("uptime -p")
        this.$data.uptime = uptimeResult.replace('up ', '') || '未知'
        
        // 获取系统负载
        const loadResult = await this.$api.shell("cat /proc/loadavg | awk '{print $1, $2, $3}'")
        this.$data.loadAvg = '负载: ' + loadResult
        
        this.$data.output = '系统状态获取完成！\n\n'
        this.$data.output += `CPU: ${this.$data.cpuUsage}%\n`
        this.$data.output += `内存: ${this.$data.memUsed} / ${this.$data.memTotal} (${this.$data.memUsage}%)\n`
        this.$data.output += `磁盘: ${this.$data.diskUsed} / ${this.$data.diskTotal} (${this.$data.diskUsage}%)\n`
        this.$data.output += `运行时间: ${this.$data.uptime}\n`
        this.$data.output += `${this.$data.loadAvg}\n`
        
        this.$refresh()
        this.$api.toast('系统状态已更新', 'success')
      } catch (e) {
        this.$data.output = '获取系统信息失败: ' + e.message
        this.$refresh()
        this.$api.toast('获取失败', 'error')
      }
    },

    // ==================== 网络诊断方法 ====================
    
    // Ping测试
    async doPing() {
      const host = this.$data.pingHost
      if (!host) {
        this.$api.toast('请输入目标地址', 'error')
        return
      }
      
      this.$data.output = `正在Ping ${host}...\n`
      this.$refresh()
      
      // 执行ping命令，发送4个包
      const result = await this.$api.shell(`ping -c 4 ${host}`)
      this.$data.output = result
      this.$refresh()
      
      // 判断是否成功
      if (result.includes('bytes from')) {
        this.$api.toast('Ping成功', 'success')
      } else {
        this.$api.toast('Ping失败', 'error')
      }
    },
    
    // DNS查询
    async showDns() {
      const host = this.$data.pingHost || 'baidu.com'
      this.$data.output = `正在查询 ${host} 的DNS记录...\n`
      this.$refresh()
      
      const result = await this.$api.shell(`nslookup ${host}`)
      this.$data.output = result
      this.$refresh()
    },
    
    // 显示路由表
    async showRoutes() {
      this.$data.output = '正在获取路由表...\n'
      this.$refresh()
      
      const result = await this.$api.shell('ip route')
      this.$data.output = '=== 路由表 ===\n\n' + result
      this.$refresh()
    },
    
    // 显示网卡信息
    async showInterfaces() {
      this.$data.output = '正在获取网卡信息...\n'
      this.$refresh()
      
      const result = await this.$api.shell('ip addr')
      this.$data.output = '=== 网卡信息 ===\n\n' + result
      this.$refresh()
    },
    
    // 显示网络连接
    async showConnections() {
      this.$data.output = '正在获取网络连接...\n'
      this.$refresh()
      
      const result = await this.$api.shell('netstat -tuln 2>/dev/null || ss -tuln')
      this.$data.output = '=== 网络连接 ===\n\n' + result
      this.$refresh()
    },

    // ==================== 文件管理方法 ====================
    
    // 浏览目录
    async browsePath() {
      const path = this.$data.currentPath
      this.$data.output = `正在浏览 ${path}...\n`
      this.$refresh()
      
      const result = await this.$api.shell(`ls -la "${path}"`)
      this.$data.output = `=== ${path} ===\n\n` + result
      this.$refresh()
    },
    
    // 返回上级目录
    goParent() {
      const parts = this.$data.currentPath.split('/').filter(p => p)
      if (parts.length > 0) {
        parts.pop()
        this.$data.currentPath = '/' + parts.join('/')
        if (!this.$data.currentPath) this.$data.currentPath = '/'
        this.$refresh()
        this.browsePath()
      }
    },
    
    // 跳转到指定路径
    goToPath(path) {
      // 注意：实际使用时需要从按钮的data-path属性获取
      this.$data.currentPath = path
      this.$refresh()
      this.browsePath()
    },
    
    // 查看文件内容
    async viewFile() {
      const fileName = this.$data.fileName
      if (!fileName) {
        this.$api.toast('请输入文件名', 'error')
        return
      }
      
      const fullPath = `${this.$data.currentPath}/${fileName}`
      this.$data.output = `正在读取 ${fullPath}...\n`
      this.$refresh()
      
      // 使用head限制输出行数，避免大文件卡死
      const result = await this.$api.shell(`head -100 "${fullPath}"`)
      this.$data.output = `=== ${fullPath} ===\n\n` + result
      this.$refresh()
    },
    
    // 删除文件（带确认）
    async deleteFile() {
      const fileName = this.$data.fileName
      if (!fileName) {
        this.$api.toast('请输入文件名', 'error')
        return
      }
      
      const fullPath = `${this.$data.currentPath}/${fileName}`
      
      // 使用confirm进行确认
      const confirmed = await this.$api.confirm('确认删除', `确定要删除 ${fullPath} 吗？此操作不可恢复！`)
      if (!confirmed) {
        this.$api.toast('已取消', 'info')
        return
      }
      
      const result = await this.$api.shell(`rm -f "${fullPath}" && echo "删除成功" || echo "删除失败"`)
      this.$data.output = result
      this.$refresh()
      
      if (result.includes('成功')) {
        this.$api.toast('文件已删除', 'success')
        this.browsePath() // 刷新目录
      } else {
        this.$api.toast('删除失败', 'error')
      }
    },

    // ==================== 脚本执行方法 ====================
    
    // 执行指定脚本
    async runScript() {
      const scriptName = this.$data.scriptName
      if (!scriptName) {
        this.$api.toast('请输入脚本名称', 'error')
        return
      }
      
      this.$data.output = `正在执行脚本: ${scriptName}...\n`
      this.$refresh()
      
      // 使用 $api.runScript 执行已上传的脚本
      const result = await this.$api.runScript(scriptName)
      this.$data.output = `=== ${scriptName} 执行结果 ===\n\n` + result
      this.$refresh()
      this.$api.toast('脚本执行完成', 'success')
    },
    
    // 快捷脚本执行
    async quickScript(scriptName) {
      this.$data.scriptName = scriptName
      this.$refresh()
      await this.runScript()
    },
    
    // 执行自定义命令
    async runCustomCmd() {
      const cmd = this.$data.customCmd
      if (!cmd) {
        this.$api.toast('请输入命令', 'error')
        return
      }
      
      // 危险命令确认
      const dangerousPatterns = ['rm -rf', 'mkfs', 'dd if=', ':(){', 'chmod -R 777']
      const isDangerous = dangerousPatterns.some(p => cmd.includes(p))
      
      if (isDangerous) {
        const confirmed = await this.$api.confirm('⚠️ 危险命令', `检测到危险命令，确定要执行吗？\n\n${cmd}`)
        if (!confirmed) {
          this.$api.toast('已取消', 'info')
          return
        }
      }
      
      this.$data.output = `$ ${cmd}\n\n执行中...\n`
      this.$refresh()
      
      const result = await this.$api.shell(cmd)
      this.$data.output = `$ ${cmd}\n\n` + result
      this.$refresh()
    },

    // ==================== 通用方法 ====================
    
    // 清空输出
    clearOutput() {
      this.$data.output = '等待操作...'
      this.$refresh()
    }
  },

  // ============================================================================
  // 第五部分：生命周期钩子
  // ============================================================================
  // mounted() 在插件加载完成后自动调用
  // 适合用于初始化数据、自动执行首次查询等
  
  mounted() {
    console.log('系统工具箱插件已加载')
    // 自动刷新系统监控数据
    this.refreshMonitor()
  }
}

// ============================================================================
// 使用说明
// ============================================================================
/*
1. 将此文件上传到插件商城
2. 点击"运行"按钮启动插件
3. 使用标签页切换不同功能模块

功能模块：
- 系统监控：查看CPU、内存、磁盘、运行时间
- 网络诊断：Ping测试、DNS查询、路由表、网卡信息
- 文件管理：浏览目录、查看文件、删除文件
- 脚本执行：执行已上传的脚本或自定义命令

注意事项：
- 脚本执行功能需要先在"脚本管理"中上传脚本
- 删除文件操作不可恢复，请谨慎使用
- 自定义命令会检测危险操作并提示确认
*/
