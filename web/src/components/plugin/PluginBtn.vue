<!--
  PluginBtn - 统一风格按钮组件
  支持type(primary/danger/default)和disabled状态
-->
<template>
  <button 
    :class="['px-4 py-2 rounded-lg text-sm font-medium transition-all inline-flex items-center justify-center space-x-2', btnClass]"
    :disabled="disabled || loading"
    @click="$emit('click', $event)">
    <div v-if="loading" class="w-4 h-4 border-2 border-current border-t-transparent rounded-full animate-spin"></div>
    <i v-else-if="icon" :class="['text-sm', icon.startsWith('fa') ? icon : `fa fa-${icon}`]"></i>
    <span><slot>按钮</slot></span>
  </button>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  type: { 
    type: String, 
    default: 'primary',
    validator: (v) => ['primary', 'danger', 'default', 'success'].includes(v)
  },
  disabled: { type: Boolean, default: false },
  loading: { type: Boolean, default: false },
  icon: { type: String, default: '' },
  block: { type: Boolean, default: false }
})

defineEmits(['click'])

const btnClass = computed(() => {
  const base = props.block ? 'w-full' : ''
  if (props.disabled || props.loading) {
    return `${base} bg-slate-200 dark:bg-white/10 text-slate-400 dark:text-white/30 cursor-not-allowed`
  }
  return {
    'primary': `${base} bg-violet-500 hover:bg-violet-600 text-white`,
    'danger': `${base} bg-red-500 hover:bg-red-600 text-white`,
    'success': `${base} bg-green-500 hover:bg-green-600 text-white`,
    'default': `${base} bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 text-slate-700 dark:text-white/80`
  }[props.type]
})
</script>
