/**
 * 插件UI组件库
 * 提供统一风格的组件供插件使用
 */
import PluginCard from './PluginCard.vue'
import PluginStatus from './PluginStatus.vue'
import PluginBtn from './PluginBtn.vue'

export { PluginCard, PluginStatus, PluginBtn }

// 全局注册函数
export function registerPluginComponents(app) {
  app.component('plugin-card', PluginCard)
  app.component('plugin-status', PluginStatus)
  app.component('plugin-btn', PluginBtn)
}
