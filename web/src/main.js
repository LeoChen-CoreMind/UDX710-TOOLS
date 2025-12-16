import { createApp } from 'vue'
import App from './App.vue'
import './style.css'
import { FontAwesomeIcon } from './plugins/fontawesome'
import { dom } from '@fortawesome/fontawesome-svg-core'
import { registerPluginComponents } from './components/plugin'

const app = createApp(App)
app.component('font-awesome-icon', FontAwesomeIcon)
// 注册插件UI组件库
registerPluginComponents(app)
app.mount('#app')

// 确保 Vue 挂载后重新扫描 DOM 中的图标
setTimeout(() => dom.i2svg(), 100)
