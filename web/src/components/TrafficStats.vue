<script setup>
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { getTrafficTotal, getTrafficConfig, setTrafficLimit, clearTrafficStats } from '../composables/useApi'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'

const { t } = useI18n()
const { success, error } = useToast()
const { confirm } = useConfirm()

// 流量数据
const uploadBytes = ref(0)
const downloadBytes = ref(0)
const totalBytes = ref(0)
const cumUploadBytes = ref(0)
const cumDownloadBytes = ref(0)
const cumTotalBytes = ref(0)

// 配置
const limitEnabled = ref(false)
const limitValue = ref('')
const resetEnabled = ref(false)
const resetDay = ref(1)
const saving = ref(false)
const clearing = ref(false)



// 解析流量字符串为字节数
function parseTrafficValue(str) {
  if (!str) return 0
  const match = str.match(/([\d.]+)\s*(B|KB|MB|GB|TB)/i)
  if (!match) return 0
  const value = parseFloat(match[1])
  const unit = match[2].toUpperCase()
  const units = { B: 1, KB: 1024, MB: 1024**2, GB: 1024**3, TB: 1024**4 }
  return Math.round(value * (units[unit] || 1))
}

// 格式化流量
function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

// 计算使用百分比
const usagePercent = computed(() => {
  if (!limitEnabled.value || !limitValue.value) return 0
  const limitBytes = parseFloat(limitValue.value) * 1073741824
  return Math.min(100, (totalBytes.value / limitBytes) * 100)
})

// 获取流量数据
async function fetchTrafficData() {
  try {
    const data = await getTrafficTotal()
    // 解析流量数据
    uploadBytes.value = parseTrafficValue(data.tx)
    downloadBytes.value = parseTrafficValue(data.rx)
    totalBytes.value = parseTrafficValue(data.total)
    cumUploadBytes.value = parseTrafficValue(data.cum_tx)
    cumDownloadBytes.value = parseTrafficValue(data.cum_rx)
    cumTotalBytes.value = parseTrafficValue(data.cum_total)
  } catch (error) {
    console.error('获取流量数据失败:', error)
  }
}

// 获取配置
async function fetchConfig() {
  try {
    const config = await getTrafficConfig()
    if (config.switch === 1) {
      limitEnabled.value = true
      limitValue.value = (config.much / 1073741824).toFixed(2)
    } else {
      limitEnabled.value = false
    }
    resetEnabled.value = config.reset_enabled === 1
    resetDay.value = config.reset_day || 1
  } catch (error) {
    console.error('获取配置失败:', error)
  }
}

// 保存配置
async function saveConfig() {
  if (limitEnabled.value && (!limitValue.value || parseFloat(limitValue.value) <= 0)) {
    error(t('traffic.enterValidLimit'))
    return
  }
  if (resetEnabled.value && (resetDay.value < 1 || resetDay.value > 31)) {
    error(t('traffic.enterValidResetDay', '请输入正确的重置日期（1-31）'))
    return
  }
  saving.value = true
  try {
    await setTrafficLimit(limitEnabled.value, parseFloat(limitValue.value) || 0, resetEnabled.value, resetDay.value)
    success(t('traffic.saveSuccess'))
    await fetchTrafficData() // Refresh data as offsets might have changed
  } catch (err) {
    error(t('traffic.saveFailed') + ': ' + err.message)
  } finally {
    saving.value = false
  }
}

// 清除统计
async function handleClear() {
  if (!await confirm({ title: t('traffic.clearStats'), message: t('traffic.confirmClear'), danger: true })) return
  clearing.value = true
  try {
    await clearTrafficStats()
    uploadBytes.value = 0
    downloadBytes.value = 0
    totalBytes.value = 0
    success(t('traffic.dataCleared'))
  } catch (err) {
    error(t('traffic.clearFailed') + ': ' + err.message)
  } finally {
    clearing.value = false
  }
}

let refreshInterval = null
onMounted(() => {
  fetchTrafficData()
  fetchConfig()
  refreshInterval = setInterval(fetchTrafficData, 5000)
})
onUnmounted(() => {
  if (refreshInterval) clearInterval(refreshInterval)
})
</script>

<template>
  <div class="space-y-6">
    <!-- 流量概览卡片 -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
      <!-- 上传流量 -->
      <div class="relative overflow-hidden rounded-2xl bg-gradient-to-br from-blue-100 to-cyan-100 dark:from-blue-500/20 dark:to-cyan-500/20 border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-blue-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-blue-300/60 dark:hover:shadow-black/30 transition-all duration-300">
        <div class="absolute top-0 right-0 w-32 h-32 bg-blue-500/10 rounded-full -translate-y-1/2 translate-x-1/2"></div>
        <div class="relative">
          <div class="flex items-center space-x-3 mb-4">
            <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-blue-500 to-cyan-400 flex items-center justify-center shadow-lg shadow-blue-500/30">
              <i class="fas fa-arrow-up text-white text-lg"></i>
            </div>
            <div>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.uploadTraffic') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-2xl">{{ formatBytes(uploadBytes) }}</p>
            </div>
          </div>
          <div class="flex items-center text-green-600 dark:text-green-400 text-sm">
            <i class="fas fa-arrow-trend-up mr-1"></i>
            <span>{{ t('traffic.realtime') }}</span>
          </div>
        </div>
      </div>

      <!-- 下载流量 -->
      <div class="relative overflow-hidden rounded-2xl bg-gradient-to-br from-purple-100 to-pink-100 dark:from-purple-500/20 dark:to-pink-500/20 border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-purple-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-purple-300/60 dark:hover:shadow-black/30 transition-all duration-300">
        <div class="absolute top-0 right-0 w-32 h-32 bg-purple-500/10 rounded-full -translate-y-1/2 translate-x-1/2"></div>
        <div class="relative">
          <div class="flex items-center space-x-3 mb-4">
            <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-purple-500 to-pink-400 flex items-center justify-center shadow-lg shadow-purple-500/30">
              <i class="fas fa-arrow-down text-white text-lg"></i>
            </div>
            <div>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.downloadTraffic') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-2xl">{{ formatBytes(downloadBytes) }}</p>
            </div>
          </div>
          <div class="flex items-center text-green-600 dark:text-green-400 text-sm">
            <i class="fas fa-arrow-trend-up mr-1"></i>
            <span>{{ t('traffic.realtime') }}</span>
          </div>
        </div>
      </div>

      <!-- 总流量 -->
      <!-- 总流量 -->
      <div class="relative overflow-hidden rounded-2xl bg-gradient-to-br from-orange-100 to-amber-100 dark:from-orange-500/20 dark:to-amber-500/20 border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-orange-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-orange-300/60 dark:hover:shadow-black/30 transition-all duration-300">
        <div class="absolute top-0 right-0 w-32 h-32 bg-orange-500/10 rounded-full -translate-y-1/2 translate-x-1/2"></div>
        <div class="relative">
          <div class="flex items-center space-x-3 mb-4">
            <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-orange-500 to-amber-400 flex items-center justify-center shadow-lg shadow-orange-500/30">
              <i class="fas fa-chart-pie text-white text-lg"></i>
            </div>
            <div>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.totalTraffic') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-2xl">{{ formatBytes(totalBytes) }}</p>
            </div>
          </div>
          <div class="flex items-center text-green-600 dark:text-green-400 text-sm">
            <i class="fas fa-clock mr-1"></i>
            <span>{{ resetEnabled ? t('traffic.monthlyTotal') : t('traffic.allTime') }}</span>
          </div>
        </div>
      </div>
      <!-- 累计总流量 (仅开启每月清零时显示) -->
      <div v-if="resetEnabled" class="relative overflow-hidden rounded-2xl bg-gradient-to-br from-indigo-100 to-purple-100 dark:from-indigo-500/20 dark:to-purple-500/20 border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-indigo-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-indigo-300/60 dark:hover:shadow-black/30 transition-all duration-300">
        <div class="absolute top-0 right-0 w-32 h-32 bg-indigo-500/10 rounded-full -translate-y-1/2 translate-x-1/2"></div>
        <div class="relative">
          <div class="flex items-center space-x-3 mb-4">
            <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-400 flex items-center justify-center shadow-lg shadow-indigo-500/30">
              <i class="fas fa-layer-group text-white text-lg"></i>
            </div>
            <div>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.cumTotalTraffic') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-2xl">{{ formatBytes(cumTotalBytes) }}</p>
            </div>
          </div>
          <div class="flex items-center text-purple-600 dark:text-purple-400 text-sm">
            <i class="fas fa-history mr-1"></i>
            <span>{{ t('traffic.allTime') }}</span>
          </div>
        </div>
      </div>
    </div>

    <!-- 流量使用进度 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <div class="flex items-center justify-between mb-6">
        <h3 class="text-slate-900 dark:text-white font-semibold flex items-center">
          <i class="fas fa-gauge-high text-green-400 mr-2"></i>
          {{ t('traffic.usageProgress') }}
        </h3>
        <div class="flex items-center space-x-2">
          <span class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.limit') }}:</span>
          <span class="text-slate-900 dark:text-white font-medium">{{ limitEnabled ? limitValue + ' GB' : t('traffic.notSet') }}</span>
        </div>
      </div>
      
      <!-- 进度条 -->
      <div class="relative h-8 bg-slate-200 dark:bg-white/10 rounded-full overflow-hidden mb-4">
        <div 
          class="absolute inset-y-0 left-0 rounded-full transition-all duration-500 ease-out"
          :class="usagePercent > 80 ? 'bg-gradient-to-r from-red-500 to-orange-500' : 'bg-gradient-to-r from-green-500 to-emerald-400'"
          :style="{ width: usagePercent + '%' }"
        >
          <div class="absolute inset-0 bg-white/20 animate-pulse"></div>
        </div>
        <div class="absolute inset-0 flex items-center justify-center">
          <span class="text-white font-bold text-sm drop-shadow">{{ usagePercent.toFixed(1) }}%</span>
        </div>
      </div>

      <!-- 流量详情 -->
      <div class="grid grid-cols-3 gap-4 text-center">
        <div class="p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
          <div class="text-blue-600 dark:text-blue-400 text-xs mb-1">{{ t('traffic.uploadPercent') }}</div>
          <div class="text-slate-900 dark:text-white font-semibold">{{ totalBytes ? ((uploadBytes / totalBytes) * 100).toFixed(1) : 0 }}%</div>
        </div>
        <div class="p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
          <div class="text-purple-600 dark:text-purple-400 text-xs mb-1">{{ t('traffic.downloadPercent') }}</div>
          <div class="text-slate-900 dark:text-white font-semibold">{{ totalBytes ? ((downloadBytes / totalBytes) * 100).toFixed(1) : 0 }}%</div>
        </div>
        <div class="p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
          <div class="text-orange-600 dark:text-orange-400 text-xs mb-1">{{ t('traffic.remaining') }}</div>
          <div class="text-slate-900 dark:text-white font-semibold">{{ limitEnabled ? formatBytes(Math.max(0, parseFloat(limitValue) * 1073741824 - totalBytes)) : '∞' }}</div>
        </div>
      </div>
    </div>

    <!-- 流量限制设置 -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
      <!-- 限制配置 -->
      <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
        <h3 class="text-slate-900 dark:text-white font-semibold mb-6 flex items-center">
          <i class="fas fa-shield-halved text-yellow-400 mr-2"></i>
          {{ t('traffic.limitSettings') }}
        </h3>
        
        <!-- 启用开关 -->
        <div class="flex items-center justify-between p-4 bg-slate-100 dark:bg-white/5 rounded-xl mb-4">
          <div class="flex items-center space-x-3">
            <div class="w-10 h-10 rounded-lg bg-yellow-500/20 flex items-center justify-center">
              <i class="fas fa-toggle-on text-yellow-400"></i>
            </div>
            <div>
              <p class="text-slate-900 dark:text-white font-medium">{{ t('traffic.enableLimit') }}</p>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.enableLimitDesc') }}</p>
            </div>
          </div>
          <label class="relative cursor-pointer">
            <input type="checkbox" v-model="limitEnabled" class="sr-only peer">
            <div class="w-14 h-7 bg-slate-300 dark:bg-white/20 rounded-full peer peer-checked:bg-green-500 transition-colors"></div>
            <div class="absolute top-0.5 left-0.5 w-6 h-6 bg-white rounded-full shadow transition-transform peer-checked:translate-x-7"></div>
          </label>
        </div>

        <!-- 每月清零开关 -->
        <div class="flex items-center justify-between p-4 bg-slate-100 dark:bg-white/5 rounded-xl mb-4">
          <div class="flex items-center space-x-3">
            <div class="w-10 h-10 rounded-lg bg-indigo-500/20 flex items-center justify-center">
              <i class="fas fa-calendar-check text-indigo-400"></i>
            </div>
            <div>
              <p class="text-slate-900 dark:text-white font-medium">{{ t('traffic.monthlyReset') }}</p>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('traffic.monthlyResetDesc') }}</p>
            </div>
          </div>
          <label class="relative cursor-pointer">
            <input type="checkbox" v-model="resetEnabled" class="sr-only peer">
            <div class="w-14 h-7 bg-slate-300 dark:bg-white/20 rounded-full peer peer-checked:bg-indigo-500 transition-colors"></div>
            <div class="absolute top-0.5 left-0.5 w-6 h-6 bg-white rounded-full shadow transition-transform peer-checked:translate-x-7"></div>
          </label>
        </div>

        <!-- 结算日输入 -->
        <div class="mb-6" v-if="resetEnabled">
          <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ t('traffic.resetDay') }}</label>
          <div class="flex items-center space-x-3">
            <input
              type="number"
              v-model="resetDay"
              min="1" max="31"
              :placeholder="t('traffic.enterResetDay')"
              class="flex-1 px-4 py-3 bg-slate-100 dark:bg-white/10 border border-slate-300 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-indigo-400 focus:ring-2 focus:ring-indigo-400/20 transition-all"
            >
            <span class="text-slate-600 dark:text-white/60 font-medium">{{ t('traffic.dayOfMonth') }}</span>
          </div>
        </div>

        <!-- 限制值输入 -->
        <div class="mb-6">
          <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ t('traffic.limitValue') }}</label>
          <div class="flex items-center space-x-3">
            <input 
              type="number" 
              v-model="limitValue" 
              :disabled="!limitEnabled"
              :placeholder="t('traffic.enterLimit')"
              class="flex-1 px-4 py-3 bg-slate-100 dark:bg-white/10 border border-slate-300 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-green-400 focus:ring-2 focus:ring-green-400/20 transition-all disabled:opacity-50"
            >
            <span class="text-slate-600 dark:text-white/60 font-medium">GB</span>
          </div>
        </div>

        <!-- 操作按钮 -->
        <div class="flex space-x-3">
          <button 
            @click="saveConfig" 
            :disabled="saving"
            class="flex-1 py-3 bg-gradient-to-r from-green-500 to-emerald-400 text-white font-medium rounded-xl hover:shadow-lg hover:shadow-green-500/30 transition-all disabled:opacity-50"
          >
            <i :class="saving ? 'fas fa-spinner animate-spin' : 'fas fa-save'" class="mr-2"></i>
            {{ saving ? t('traffic.saving') : t('traffic.saveSettings') }}
          </button>
          <button 
            @click="handleClear" 
            :disabled="clearing"
            class="flex-1 py-3 bg-gradient-to-r from-red-500 to-orange-400 text-white font-medium rounded-xl hover:shadow-lg hover:shadow-red-500/30 transition-all disabled:opacity-50"
          >
            <i :class="clearing ? 'fas fa-spinner animate-spin' : 'fas fa-trash'" class="mr-2"></i>
            {{ clearing ? t('traffic.clearing') : t('traffic.clearStats') }}
          </button>
        </div>
      </div>

      <!-- 使用说明 -->
      <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
        <h3 class="text-slate-900 dark:text-white font-semibold mb-4 flex items-center">
          <i class="fas fa-lightbulb text-amber-400 mr-2"></i>
          {{ t('traffic.instructions') }}
        </h3>
        <div class="space-y-3">
          <div class="flex items-start space-x-3 p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
            <div class="w-6 h-6 rounded-full bg-blue-500/20 flex items-center justify-center flex-shrink-0 mt-0.5">
              <span class="text-blue-400 text-xs font-bold">1</span>
            </div>
            <p class="text-slate-600 dark:text-white/70 text-sm">{{ t('traffic.tip1') }}</p>
          </div>
          <div class="flex items-start space-x-3 p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
            <div class="w-6 h-6 rounded-full bg-purple-500/20 flex items-center justify-center flex-shrink-0 mt-0.5">
              <span class="text-purple-400 text-xs font-bold">2</span>
            </div>
            <p class="text-slate-600 dark:text-white/70 text-sm">{{ t('traffic.tip2') }}</p>
          </div>
          <div class="flex items-start space-x-3 p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
            <div class="w-6 h-6 rounded-full bg-orange-500/20 flex items-center justify-center flex-shrink-0 mt-0.5">
              <span class="text-orange-400 text-xs font-bold">3</span>
            </div>
            <p class="text-slate-600 dark:text-white/70 text-sm">{{ t('traffic.tip3') }}</p>
          </div>
          <div class="flex items-start space-x-3 p-3 bg-slate-100 dark:bg-white/5 rounded-xl">
            <div class="w-6 h-6 rounded-full bg-green-500/20 flex items-center justify-center flex-shrink-0 mt-0.5">
              <span class="text-green-400 text-xs font-bold">4</span>
            </div>
            <p class="text-slate-600 dark:text-white/70 text-sm">{{ t('traffic.tip4') }}</p>
          </div>
        </div>
      </div>
    </div>

  </div>
</template>
