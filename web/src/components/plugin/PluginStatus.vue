<!--
  PluginStatus - 状态显示组件
  用于显示监控项（如CPU/内存），支持label/value/status属性
-->
<template>
  <div class="flex items-center justify-between py-2 px-3 rounded-lg bg-slate-50 dark:bg-white/5">
    <div class="flex items-center space-x-2">
      <div :class="['w-2 h-2 rounded-full', statusColor]"></div>
      <span class="text-slate-600 dark:text-white/70 text-sm">{{ label }}</span>
    </div>
    <div class="flex items-center space-x-2">
      <span :class="['font-medium text-sm', textColor]">{{ value }}</span>
      <span v-if="unit" class="text-slate-400 dark:text-white/40 text-xs">{{ unit }}</span>
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: [String, Number], required: true },
  unit: { type: String, default: '' },
  status: { 
    type: String, 
    default: 'default',
    validator: (v) => ['success', 'warning', 'error', 'default'].includes(v)
  }
})

const statusColor = computed(() => ({
  'success': 'bg-green-500',
  'warning': 'bg-amber-500',
  'error': 'bg-red-500',
  'default': 'bg-slate-400 dark:bg-white/40'
}[props.status]))

const textColor = computed(() => ({
  'success': 'text-green-600 dark:text-green-400',
  'warning': 'text-amber-600 dark:text-amber-400',
  'error': 'text-red-600 dark:text-red-400',
  'default': 'text-slate-900 dark:text-white'
}[props.status]))
</script>
