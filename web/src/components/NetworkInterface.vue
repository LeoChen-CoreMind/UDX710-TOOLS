<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { getNetifList, getNetifStats, setNetifMonitor } from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { success, error } = useToast()

// 接口列表
const interfaces = ref([])
const loading = ref(false)
const statsData = ref({})

// 隐藏详情的接口集合 (默认全部隐藏)
const showDetailsSet = ref(new Set())

// 切换详情显示状态
function toggleDetails(ifname) {
  if (showDetailsSet.value.has(ifname)) {
    showDetailsSet.value.delete(ifname)
  } else {
    showDetailsSet.value.add(ifname)
  }
  // 触发响应式更新
  showDetailsSet.value = new Set(showDetailsSet.value)
}

// 检查是否显示详情
function isDetailsShown(ifname) {
  return showDetailsSet.value.has(ifname)
}

// 遮蔽 IPv4 地址
function maskIPv4(ip, ifname) {
  if (!ip || ip === '-') return '-'
  if (isDetailsShown(ifname)) return ip
  const parts = ip.split('.')
  if (parts.length !== 4) return '•••••••'
  return '•••.•••.•••.' + parts[3]
}

// 遮蔽 IPv6 地址
function maskIPv6(ip, ifname) {
  if (!ip || ip === '-') return '-'
  if (isDetailsShown(ifname)) return ip
  // 只显示最后8个字符
  if (ip.length > 12) {
    return '••••:••••:••••:' + ip.slice(-8)
  }
  return '•••••••'
}

// 遮蔽子网掩码
function maskMask(mask, ifname) {
  if (!mask || mask === '-') return '-'
  if (isDetailsShown(ifname)) return mask
  const parts = mask.split('.')
  if (parts.length !== 4) return '•••••••'
  return '•••.•••.•••.' + parts[3]
}

// 格式化速率
function formatRate(bytesPerSecond) {
  if (bytesPerSecond === 0) return '0 B/s'
  const k = 1024
  const sizes = ['B/s', 'KB/s', 'MB/s', 'GB/s']
  const i = Math.floor(Math.log(bytesPerSecond) / Math.log(k))
  return parseFloat((bytesPerSecond / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

// 格式化字节数
function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

// 获取接口列表
async function fetchInterfaces() {
  loading.value = true
  try {
    const data = await getNetifList()
    interfaces.value = data.interfaces || []
    
    // 获取列表后,立即获取所有已启用监听的接口的统计数据
    for (const iface of interfaces.value) {
      if (iface.monitoring) {
        fetchStats(iface.name)
      }
    }
  } catch (err) {
    console.error('获取接口列表失败:', err)
  } finally {
    loading.value = false
  }
}

// 获取实时流量统计
async function fetchStats(ifname) {
  try {
    const data = await getNetifStats(ifname)
    statsData.value[ifname] = data
  } catch (err) {
    console.error(`获取${ifname}流量统计失败:`, err)
    throw err
  }
}

// 切换监听状态
async function toggleMonitor(iface) {
  const newState = !iface.monitoring
  try {
    await setNetifMonitor(iface.name, newState)
    iface.monitoring = newState
    if (newState) {
      success(t('netif.monitorEnabled', { name: iface.name }))
      fetchStats(iface.name)
    } else {
      success(t('netif.monitorDisabled', { name: iface.name }))
      delete statsData.value[iface.name]
    }
  } catch (err) {
    error(t('netif.toggleFailed') + ': ' + err.message)
  }
}

// 刷新监听中的接口数据
async function refreshMonitoringStats() {
  let hasError = false
  for (const iface of interfaces.value) {
    if (iface.monitoring) {
      try {
        await fetchStats(iface.name)
      } catch (err) {
        hasError = true
      }
    }
  }
  if (hasError) {
    await fetchInterfaces()
  }
}

// 获取接口图标
function getInterfaceIcon(name) {
  if (name.startsWith('wlan')) return 'wifi'
  if (name.startsWith('eth') || name.startsWith('sipa')) return 'ethernet'
  if (name.startsWith('usb')) return 'plug'
  if (name === 'lo') return 'arrows-rotate'
  if (name === 'tether') return 'mobile'
  return 'network-wired'
}

// 获取接口颜色
function getInterfaceColor(name) {
  if (name.startsWith('wlan')) return 'from-blue-500 to-cyan-400'
  if (name.startsWith('eth') || name.startsWith('sipa')) return 'from-green-500 to-emerald-400'
  if (name.startsWith('usb')) return 'from-purple-500 to-pink-400'
  if (name === 'lo') return 'from-gray-500 to-slate-400'
  if (name === 'tether') return 'from-orange-500 to-amber-400'
  return 'from-indigo-500 to-violet-400'
}

let refreshInterval = null
onMounted(() => {
  fetchInterfaces()
  refreshInterval = setInterval(refreshMonitoringStats, 2000)
})
onUnmounted(() => {
  if (refreshInterval) clearInterval(refreshInterval)
})
</script>

<template>
  <div class="space-y-6">
    <!-- 页面标题 -->
    <div class="flex items-center justify-between">
      <div class="flex items-center space-x-3">
        <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-teal-500 to-cyan-400 flex items-center justify-center shadow-lg shadow-teal-500/30">
          <i class="fas fa-network-wired text-white text-xl"></i>
        </div>
        <div>
          <h2 class="text-xl font-bold text-slate-900 dark:text-white">{{ t('netif.title') }}</h2>
          <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('netif.subtitle') }}</p>
        </div>
      </div>
      <button 
        @click="fetchInterfaces"
        :disabled="loading"
        class="px-4 py-2 bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 rounded-xl transition-colors"
      >
        <i :class="loading ? 'fas fa-spinner animate-spin' : 'fas fa-sync-alt'" class="text-slate-600 dark:text-white/60"></i>
      </button>
    </div>

    <!-- 接口列表 -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <div 
        v-for="iface in interfaces" 
        :key="iface.name"
        class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 overflow-hidden shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl transition-all duration-300"
      >
        <!-- 接口头部 -->
        <div class="p-4 border-b border-slate-200 dark:border-white/10">
          <div class="flex items-center justify-between">
            <div class="flex items-center space-x-3">
              <div
                class="w-10 h-10 rounded-lg flex items-center justify-center shadow-lg"
                :class="`bg-gradient-to-br ${getInterfaceColor(iface.name)}`"
              >
                <i :class="`fas fa-${getInterfaceIcon(iface.name)} text-white`"></i>
              </div>
              <div>
                <h3 class="font-semibold text-slate-900 dark:text-white">{{ iface.name }}</h3>
                <p class="text-xs text-slate-500 dark:text-white/50">
                  {{ iface.hwaddr || t('netif.noMac') }}
                </p>
              </div>
            </div>
            <!-- 监听开关 -->
            <label class="relative inline-flex items-center cursor-pointer">
              <input 
                type="checkbox" 
                :checked="iface.monitoring"
                @change="toggleMonitor(iface)"
                class="sr-only peer"
              >
              <div class="w-12 h-6 bg-slate-200 dark:bg-slate-700 rounded-full peer 
                          peer-checked:bg-gradient-to-r peer-checked:from-teal-400 peer-checked:to-cyan-500
                          after:content-[''] after:absolute after:top-0.5 after:left-0.5 
                          after:bg-white after:rounded-full after:h-5 after:w-5 
                          after:transition-all after:duration-300 after:shadow-md
                          peer-checked:after:translate-x-6
                          hover:bg-slate-300 dark:hover:bg-slate-600
                          transition-colors duration-200">
              </div>
            </label>
          </div>
        </div>

        <!-- 接口信息 -->
        <div class="p-4 space-y-4">
          <!-- IP 信息区块 -->
          <div class="relative">
            <!-- 眼睛图标按钮 -->
            <button
              @click="toggleDetails(iface.name)"
              class="absolute top-0 right-0 p-2 rounded-lg hover:bg-slate-100 dark:hover:bg-white/10 transition-colors min-h-8 min-w-8"
              :title="isDetailsShown(iface.name) ? t('netif.hideDetails') : t('netif.showDetails')"
            >
              <i :class="isDetailsShown(iface.name) ? 'fas fa-eye' : 'fas fa-eye-slash'" 
                 class="text-slate-400 dark:text-white/40 hover:text-slate-600 dark:hover:text-white/60 transition-colors"></i>
            </button>
            
            <div class="grid grid-cols-2 gap-3 text-sm pr-10">
              <!-- IPv4 -->
              <div class="p-2 rounded-lg bg-slate-50/50 dark:bg-white/5">
                <span class="text-xs text-slate-500 dark:text-white/50 block mb-1">{{ t('netif.ipv4') }}</span>
                <span class="text-slate-900 dark:text-white font-mono text-sm tracking-tight">
                  {{ maskIPv4(iface.inet_addr, iface.name) }}
                </span>
              </div>
              <!-- 子网掩码 -->
              <div class="p-2 rounded-lg bg-slate-50/50 dark:bg-white/5">
                <span class="text-xs text-slate-500 dark:text-white/50 block mb-1">{{ t('netif.mask') }}</span>
                <span class="text-slate-900 dark:text-white font-mono text-sm tracking-tight">
                  {{ maskMask(iface.mask, iface.name) }}
                </span>
              </div>
              <!-- IPv6 -->
              <div class="col-span-2 p-2 rounded-lg bg-slate-50/50 dark:bg-white/5">
                <span class="text-xs text-slate-500 dark:text-white/50 block mb-1">{{ t('netif.ipv6') }}</span>
                <span class="text-slate-900 dark:text-white font-mono text-xs tracking-tight break-all">
                  {{ maskIPv6(iface.inet6_addr, iface.name) }}
                </span>
              </div>
            </div>
          </div>

          <!-- 状态标签 -->
          <div class="flex items-center space-x-2">
            <span
              class="px-2 py-1 rounded-full text-xs font-medium"
              :class="iface.is_up ? 'bg-green-100 text-green-700 dark:bg-green-500/20 dark:text-green-400' : 'bg-red-100 text-red-700 dark:bg-red-500/20 dark:text-red-400'"
            >
              {{ iface.is_up ? t('netif.up') : t('netif.down') }}
            </span>
            <span
              v-if="iface.monitoring"
              class="px-2 py-1 rounded-full text-xs font-medium bg-teal-100 text-teal-700 dark:bg-teal-500/20 dark:text-teal-400"
            >
              <i class="fas fa-chart-line mr-1"></i>{{ t('netif.monitoring') }}
            </span>
          </div>
        </div>

        <!-- 实时流量统计 -->
        <div v-if="iface.monitoring && statsData[iface.name]" class="p-4 bg-slate-50 dark:bg-white/5 border-t border-slate-200 dark:border-white/10">
          <div class="grid grid-cols-2 gap-4">
            <!-- 下载 -->
            <div class="space-y-2">
              <div class="flex items-center space-x-2">
                <i class="fas fa-arrow-down text-green-500"></i>
                <span class="text-sm font-medium text-slate-700 dark:text-white/70">{{ t('netif.rx') }}</span>
              </div>
              <div class="text-lg font-bold text-green-600 dark:text-green-400">
                {{ statsData[iface.name].rx?.ratestring || '0 bit/s' }}
              </div>
              <div class="text-xs text-slate-500 dark:text-white/50">
                {{ t('netif.packets') }}: {{ statsData[iface.name].rx?.packets || 0 }}
              </div>
            </div>
            <!-- 上传 -->
            <div class="space-y-2">
              <div class="flex items-center space-x-2">
                <i class="fas fa-arrow-up text-blue-500"></i>
                <span class="text-sm font-medium text-slate-700 dark:text-white/70">{{ t('netif.tx') }}</span>
              </div>
              <div class="text-lg font-bold text-blue-600 dark:text-blue-400">
                {{ statsData[iface.name].tx?.ratestring || '0 bit/s' }}
              </div>
              <div class="text-xs text-slate-500 dark:text-white/50">
                {{ t('netif.packets') }}: {{ statsData[iface.name].tx?.packets || 0 }}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 空状态 -->
    <div v-if="!loading && interfaces.length === 0" class="text-center py-12">
      <i class="fas fa-network-wired text-4xl text-slate-300 dark:text-white/20 mb-4"></i>
      <p class="text-slate-500 dark:text-white/50">{{ t('netif.noInterfaces') }}</p>
    </div>
  </div>
</template>
