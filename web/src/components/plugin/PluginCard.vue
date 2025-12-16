<!--
  PluginCard - 插件卡片容器组件
  用于统一插件UI风格，提供标题、图标、加载状态等功能
-->
<template>
  <div class="rounded-xl bg-slate-100 dark:bg-white/5 border border-slate-200 dark:border-white/10 overflow-hidden">
    <!-- 标题栏 -->
    <div v-if="title || icon" 
      class="flex items-center justify-between px-4 py-3 border-b border-slate-200 dark:border-white/10"
      :class="{ 'cursor-pointer hover:bg-slate-200/50 dark:hover:bg-white/5': collapsible }"
      @click="collapsible && (collapsed = !collapsed)">
      <div class="flex items-center space-x-3">
        <div v-if="icon" class="w-8 h-8 rounded-lg bg-gradient-to-br from-violet-500 to-purple-500 flex items-center justify-center">
          <i :class="['text-white text-sm', icon.startsWith('fa') ? icon : `fa fa-${icon}`]"></i>
        </div>
        <span class="font-medium text-slate-900 dark:text-white text-sm">{{ title }}</span>
      </div>
      <div class="flex items-center space-x-2">
        <!-- 加载指示器 -->
        <div v-if="loading" class="w-4 h-4 border-2 border-violet-500 border-t-transparent rounded-full animate-spin"></div>
        <!-- 折叠图标 -->
        <i v-if="collapsible" 
          :class="['fa fa-chevron-down text-slate-400 dark:text-white/40 text-xs transition-transform', { 'rotate-180': !collapsed }]"></i>
      </div>
    </div>
    <!-- 内容区域 -->
    <div v-show="!collapsed" class="p-4">
      <div v-if="loading" class="flex items-center justify-center py-8">
        <div class="w-8 h-8 border-3 border-violet-500 border-t-transparent rounded-full animate-spin"></div>
      </div>
      <slot v-else></slot>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'

defineProps({
  title: { type: String, default: '' },
  icon: { type: String, default: '' },
  loading: { type: Boolean, default: false },
  collapsible: { type: Boolean, default: false }
})

const collapsed = ref(false)
</script>
