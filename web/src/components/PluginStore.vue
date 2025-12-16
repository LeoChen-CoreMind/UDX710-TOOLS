<script setup>
import { ref, computed, onMounted, inject, nextTick, createApp, h } from 'vue'
import { PluginCard, PluginStatus, PluginBtn } from './plugin'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'
import { getPluginList, uploadPlugin, deletePlugin, deleteAllPlugins, executeShell, getScriptList, uploadScript, updateScript, deleteScript, getPluginStorage, setPluginStorage, deletePluginStorage } from '../composables/useApi'

const { success, error, info } = useToast()
const { confirm } = useConfirm()
const isDark = inject('isDark', ref(true))

// 插件列表
const plugins = ref([])
const loading = ref(false)
const uploading = ref(false)

// 上传相关
const showUploadModal = ref(false)
const pluginName = ref('')
const pluginContent = ref('')
const isDragging = ref(false)

// 运行插件弹窗
const showPluginModal = ref(false)
const currentPlugin = ref(null)
const pluginOutput = ref('')
const pluginData = ref({})

// 插件内部toast
const pluginToast = ref({ show: false, message: '', type: 'info' })
let pluginToastTimer = null

// 插件内部confirm
const pluginConfirm = ref({ show: false, title: '', message: '', resolve: null })

// 脚本弹窗内部confirm
const scriptConfirm = ref({ show: false, title: '', message: '', resolve: null, danger: false })

// 脚本管理
const showScriptModal = ref(false)
const scripts = ref([])
const scriptsLoading = ref(false)
const editingScript = ref(null)
const newScriptName = ref('')
const newScriptContent = ref('')
const showNewScriptForm = ref(false)

// 插件编辑
const showEditPluginModal = ref(false)
const editingPlugin = ref(null)
const editPluginContent = ref('')

// 获取插件列表
async function fetchPlugins() {
  loading.value = true
  try {
    const res = await getPluginList()
    if (res.Code === 0) {
      plugins.value = res.Data || []
    }
  } catch (e) {
    error('获取插件列表失败')
  } finally {
    loading.value = false
  }
}

// 上传插件
async function handleUpload() {
  if (!pluginContent.value.trim()) {
    error('插件内容不能为空')
    return
  }
  
  uploading.value = true
  try {
    const name = pluginName.value.trim() || 'plugin_' + Date.now()
    const res = await uploadPlugin(name, pluginContent.value)
    if (res.Code === 0) {
      success('插件上传成功')
      showUploadModal.value = false
      pluginName.value = ''
      pluginContent.value = ''
      fetchPlugins()
    } else {
      error(res.Error || '上传失败')
    }
  } catch (e) {
    error('上传失败: ' + e.message)
  } finally {
    uploading.value = false
  }
}


// 删除插件
async function handleDelete(plugin) {
  const confirmed = await confirm({
    title: '删除插件',
    message: `确定要删除插件 "${plugin.name}" 吗？`,
    danger: true
  })
  if (!confirmed) return
  
  try {
    const res = await deletePlugin(plugin.filename)
    if (res.Code === 0) {
      success('插件已删除')
      fetchPlugins()
    } else {
      error(res.Error || '删除失败')
    }
  } catch (e) {
    error('删除失败')
  }
}

// 编辑插件
function editPlugin(plugin) {
  editingPlugin.value = plugin
  editPluginContent.value = plugin.content
  showEditPluginModal.value = true
}

// 保存插件编辑
async function savePluginEdit() {
  if (!editingPlugin.value || !editPluginContent.value.trim()) {
    error('插件内容不能为空')
    return
  }
  try {
    // 先删除旧插件，再上传新内容
    await deletePlugin(editingPlugin.value.filename)
    const name = editingPlugin.value.filename.replace('.js', '')
    const res = await uploadPlugin(name, editPluginContent.value)
    if (res.Code === 0) {
      success('插件已保存')
      showEditPluginModal.value = false
      editingPlugin.value = null
      editPluginContent.value = ''
      fetchPlugins()
    } else {
      error(res.Error || '保存失败')
    }
  } catch (e) {
    error('保存失败: ' + e.message)
  }
}

// 导出插件
function handleExport(plugin) {
  const blob = new Blob([plugin.content], { type: 'application/javascript' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = plugin.filename || `${plugin.name}.js`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  success('插件已导出')
}

// 导出全部插件（打包为JSON）
function handleExportAll() {
  if (plugins.value.length === 0) {
    error('没有可导出的插件')
    return
  }
  const exportData = {
    version: '1.0',
    exportTime: new Date().toISOString(),
    plugins: plugins.value.map(p => ({
      filename: p.filename,
      content: p.content
    }))
  }
  const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `plugins_backup_${Date.now()}.json`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  success(`已导出 ${plugins.value.length} 个插件`)
}

// 导入全部插件
async function handleImportAll(e) {
  const file = e.target.files?.[0]
  if (!file) return
  
  try {
    const text = await file.text()
    const data = JSON.parse(text)
    
    if (!data.plugins || !Array.isArray(data.plugins)) {
      error('无效的插件包格式')
      return
    }
    
    let successCount = 0
    for (const p of data.plugins) {
      if (p.filename && p.content) {
        const name = p.filename.replace('.js', '')
        const res = await uploadPlugin(name, p.content)
        if (res.Code === 0) successCount++
      }
    }
    
    success(`成功导入 ${successCount}/${data.plugins.length} 个插件`)
    fetchPlugins()
  } catch (e) {
    error('导入失败: ' + e.message)
  }
  e.target.value = ''
}

// ==================== 脚本管理 ====================

// 获取脚本列表
async function fetchScripts() {
  scriptsLoading.value = true
  try {
    const res = await getScriptList()
    if (res.Code === 0) {
      scripts.value = res.Data || []
    }
  } catch (e) {
    error('获取脚本列表失败')
  } finally {
    scriptsLoading.value = false
  }
}

// 打开脚本管理弹窗
function openScriptManager() {
  showScriptModal.value = true
  editingScript.value = null
  showNewScriptForm.value = false
  fetchScripts()
}

// 编辑脚本
function editScript(script) {
  editingScript.value = { ...script }
}

// 保存脚本编辑
async function saveScriptEdit() {
  if (!editingScript.value) return
  try {
    const res = await updateScript(editingScript.value.name, editingScript.value.content)
    if (res.Code === 0) {
      success('脚本已保存')
      editingScript.value = null
      fetchScripts()
    } else {
      error(res.Error || '保存失败')
    }
  } catch (e) {
    error('保存失败: ' + e.message)
  }
}

// 删除脚本（使用弹窗内部confirm）
async function handleDeleteScript(script) {
  const confirmed = await showScriptConfirm('删除脚本', `确定要删除脚本 "${script.name}" 吗？`, true)
  if (!confirmed) return
  try {
    const res = await deleteScript(script.name)
    if (res.Code === 0) {
      success('脚本已删除')
      fetchScripts()
    } else {
      error(res.Error || '删除失败')
    }
  } catch (e) {
    error('删除失败')
  }
}

// 上传新脚本
async function handleUploadScript() {
  if (!newScriptName.value.trim() || !newScriptContent.value.trim()) {
    error('脚本名称和内容不能为空')
    return
  }
  try {
    const name = newScriptName.value.trim().endsWith('.sh') ? newScriptName.value.trim() : newScriptName.value.trim() + '.sh'
    const res = await uploadScript(name, newScriptContent.value)
    if (res.Code === 0) {
      success('脚本上传成功')
      newScriptName.value = ''
      newScriptContent.value = ''
      showNewScriptForm.value = false
      fetchScripts()
    } else {
      error(res.Error || '上传失败')
    }
  } catch (e) {
    error('上传失败: ' + e.message)
  }
}

// 导出单个脚本
function handleExportScript(script) {
  const blob = new Blob([script.content], { type: 'text/x-shellscript' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = script.name
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  success('脚本已导出')
}

// 导出全部脚本（打包为JSON）
function handleExportAllScripts() {
  if (scripts.value.length === 0) {
    error('没有可导出的脚本')
    return
  }
  const exportData = {
    version: '1.0',
    type: 'scripts',
    exportTime: new Date().toISOString(),
    scripts: scripts.value.map(s => ({
      name: s.name,
      content: s.content
    }))
  }
  const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `scripts_backup_${Date.now()}.json`
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  success(`已导出 ${scripts.value.length} 个脚本`)
}

// 导入脚本（支持.sh和.json）
async function handleImportScripts(e) {
  const file = e.target.files?.[0]
  if (!file) return
  
  try {
    const text = await file.text()
    
    if (file.name.endsWith('.json')) {
      // JSON批量导入
      const data = JSON.parse(text)
      if (!data.scripts || !Array.isArray(data.scripts)) {
        error('无效的脚本包格式')
        return
      }
      let successCount = 0
      for (const s of data.scripts) {
        if (s.name && s.content) {
          const res = await uploadScript(s.name, s.content)
          if (res.Code === 0) successCount++
        }
      }
      success(`成功导入 ${successCount}/${data.scripts.length} 个脚本`)
    } else {
      // 单个.sh文件导入
      const name = file.name.endsWith('.sh') ? file.name : file.name + '.sh'
      const res = await uploadScript(name, text)
      if (res.Code === 0) {
        success('脚本导入成功')
      } else {
        error(res.Error || '导入失败')
      }
    }
    fetchScripts()
  } catch (e) {
    error('导入失败: ' + e.message)
  }
  e.target.value = ''
}

// 删除所有插件
async function handleDeleteAll() {
  const confirmed = await confirm({
    title: '清空插件',
    message: '确定要删除所有插件吗？此操作不可恢复！',
    danger: true
  })
  if (!confirmed) return
  
  try {
    const res = await deleteAllPlugins()
    if (res.Code === 0) {
      success('所有插件已删除')
      fetchPlugins()
    } else {
      error(res.Error || '删除失败')
    }
  } catch (e) {
    error('删除失败')
  }
}

// 文件拖拽上传
function handleDrop(e) {
  isDragging.value = false
  const file = e.dataTransfer?.files?.[0]
  if (file && file.name.endsWith('.js')) {
    const reader = new FileReader()
    reader.onload = (ev) => {
      pluginContent.value = ev.target.result
      pluginName.value = file.name.replace('.js', '')
    }
    reader.readAsText(file)
  } else {
    error('请上传.js格式的插件文件')
  }
}

// 文件选择上传
function handleFileSelect(e) {
  const file = e.target.files?.[0]
  if (file) {
    const reader = new FileReader()
    reader.onload = (ev) => {
      pluginContent.value = ev.target.result
      pluginName.value = file.name.replace('.js', '')
    }
    reader.readAsText(file)
  }
}

// 显示插件内部toast
function showPluginToast(msg, type = 'info') {
  if (pluginToastTimer) clearTimeout(pluginToastTimer)
  pluginToast.value = { show: true, message: msg, type }
  pluginToastTimer = setTimeout(() => {
    pluginToast.value.show = false
  }, 3000)
}

// 显示插件内部confirm
function showPluginConfirm(title, msg) {
  return new Promise((resolve) => {
    pluginConfirm.value = { show: true, title, message: msg, resolve }
  })
}

function handlePluginConfirm(result) {
  if (pluginConfirm.value.resolve) {
    pluginConfirm.value.resolve(result)
  }
  pluginConfirm.value.show = false
}

// 脚本弹窗内部confirm显示
function showScriptConfirm(title, msg, danger = false) {
  return new Promise((resolve) => {
    scriptConfirm.value = { show: true, title, message: msg, resolve, danger }
  })
}

function handleScriptConfirm(result) {
  if (scriptConfirm.value.resolve) {
    scriptConfirm.value.resolve(result)
  }
  scriptConfirm.value.show = false
}

// 插件定时器追踪（用于自动清理）
let pluginTimers = []

// 当前运行的插件名称（用于storage API）
let currentPluginName = ''

// 插件API注入
const pluginAPI = {
  shell: async (cmd) => {
    try {
      const res = await executeShell(cmd)
      return res.Code === 0 ? res.Data : res.Error
    } catch (e) {
      return 'Error: ' + e.message
    }
  },
  alert: (title, msg) => {
    // 在插件弹窗内显示简单alert
    showPluginToast(`${title}: ${msg}`, 'info')
  },
  toast: (msg, type = 'info') => {
    showPluginToast(msg, type)
  },
  confirm: async (title, msg) => {
    // 在插件弹窗内显示confirm
    return await showPluginConfirm(title, msg)
  },
  // 执行已上传的脚本
  runScript: async (scriptName) => {
    try {
      const res = await executeShell(`sh /home/root/6677/Plugins/scripts/${scriptName}`)
      return res.Code === 0 ? res.Data : res.Error
    } catch (e) {
      return 'Error: ' + e.message
    }
  },
  // 定时器管理API - 自动追踪以便清理
  $setInterval: (fn, ms) => {
    const id = setInterval(fn, ms)
    pluginTimers.push({ type: 'interval', id })
    return id
  },
  $setTimeout: (fn, ms) => {
    const id = setTimeout(fn, ms)
    pluginTimers.push({ type: 'timeout', id })
    return id
  },
  $clearInterval: (id) => {
    clearInterval(id)
    pluginTimers = pluginTimers.filter(t => !(t.type === 'interval' && t.id === id))
  },
  $clearTimeout: (id) => {
    clearTimeout(id)
    pluginTimers = pluginTimers.filter(t => !(t.type === 'timeout' && t.id === id))
  },
  // 持久化存储API
  storage: {
    // 获取存储值
    async get(key, defaultValue = null) {
      try {
        const res = await getPluginStorage(currentPluginName)
        if (res.Code === 0 && res.Data) {
          return key in res.Data ? res.Data[key] : defaultValue
        }
        return defaultValue
      } catch (e) {
        console.error('Storage get error:', e)
        return defaultValue
      }
    },
    // 设置存储值
    async set(key, value) {
      try {
        // 先获取现有数据
        let data = {}
        const res = await getPluginStorage(currentPluginName)
        if (res.Code === 0 && res.Data) {
          data = res.Data
        }
        // 更新指定key
        data[key] = value
        const saveRes = await setPluginStorage(currentPluginName, data)
        return saveRes.Code === 0
      } catch (e) {
        console.error('Storage set error:', e)
        return false
      }
    },
    // 删除存储值
    async remove(key) {
      try {
        const res = await getPluginStorage(currentPluginName)
        if (res.Code === 0 && res.Data) {
          const data = res.Data
          delete data[key]
          const saveRes = await setPluginStorage(currentPluginName, data)
          return saveRes.Code === 0
        }
        return true
      } catch (e) {
        console.error('Storage remove error:', e)
        return false
      }
    },
    // 获取所有存储数据
    async getAll() {
      try {
        const res = await getPluginStorage(currentPluginName)
        if (res.Code === 0 && res.Data) {
          return res.Data
        }
        return {}
      } catch (e) {
        console.error('Storage getAll error:', e)
        return {}
      }
    },
    // 清空所有存储数据
    async clear() {
      try {
        const res = await deletePluginStorage(currentPluginName)
        return res.Code === 0
      } catch (e) {
        console.error('Storage clear error:', e)
        return false
      }
    }
  }
}


// 当前插件定义（用于双向绑定）
let currentPluginDef = null

// 插件组件实例（用于销毁时清理）
let pluginAppInstance = null

// 渲染插件模板
function renderPluginTemplate(container, template, data) {
  // 清理之前的Vue实例
  if (pluginAppInstance) {
    pluginAppInstance.unmount()
    pluginAppInstance = null
  }
  
  // 检测是否使用了plugin-xxx组件
  const hasPluginComponents = /<plugin-(card|status|btn)/i.test(template)
  
  if (hasPluginComponents) {
    // 使用Vue运行时编译渲染组件
    renderWithVueComponents(container, template, data)
  } else {
    // 使用原有的简单HTML渲染
    renderSimpleHtml(container, template, data)
  }
}

// 使用Vue组件渲染模板
function renderWithVueComponents(container, template, data) {
  // 关键修复：直接将$api注入到pluginData.value，而不是创建新对象
  // 这样v-model更新的就是pluginData.value本身，而不是副本
  pluginData.value.$api = pluginAPI
  
  // 创建动态组件
  const DynamicPlugin = {
    template: template,
    components: {
      'plugin-card': PluginCard,
      'plugin-status': PluginStatus,
      'plugin-btn': PluginBtn
    },
    data() {
      // 直接返回pluginData.value的引用，确保v-model更新的是同一个对象
      return pluginData.value
    },
    methods: {
      // 代理所有方法
      ...Object.fromEntries(
        Object.entries(data).filter(([k, v]) => typeof v === 'function').map(([k, v]) => [
          k,
          async function(...args) {
            // 关键：从Vue组件实例(this)同步最新数据到pluginData.value
            // 因为Vue的data()返回后会被reactive包装，this上的属性才是最新的
            const dataKeys = Object.keys(pluginData.value).filter(k => 
              k !== '$api' && typeof pluginData.value[k] !== 'function'
            )
            for (const key of dataKeys) {
              if (this[key] !== undefined) {
                pluginData.value[key] = this[key]
              }
            }
            
            // 构建正确的上下文对象
            const context = {
              $data: pluginData.value,
              $api: pluginAPI,
              $refresh: () => {
                const container = document.getElementById('plugin-container')
                if (container && currentPluginDef?.template) {
                  renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
                }
              }
            }
            // 将绑定后的方法也加入上下文
            if (currentPluginDef?.methods) {
              for (const methodName in currentPluginDef.methods) {
                if (pluginData.value[methodName]) {
                  context[methodName] = pluginData.value[methodName]
                }
              }
            }
            
            // 使用正确的上下文调用原始方法
            const result = await v.apply(context, args)
            if (result !== undefined) {
              pluginOutput.value = typeof result === 'string' ? result : JSON.stringify(result, null, 2)
            }
            return result
          }
        ])
      ),
      // 添加$refresh方法
      $refresh() {
        // 同步当前数据后重新渲染
        for (const key in pluginData.value) {
          if (typeof this[key] !== 'function' && this[key] !== undefined && key !== '$api') {
            pluginData.value[key] = this[key]
          }
        }
        const container = document.getElementById('plugin-container')
        if (container && currentPluginDef?.template) {
          renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
        }
      }
    },
    mounted() {
      // 同步初始数据（排除$api）
      const syncData = { ...this.$data }
      delete syncData.$api
      Object.assign(pluginData.value, syncData)
    }
  }
  
  // 创建并挂载Vue应用
  container.innerHTML = ''
  pluginAppInstance = createApp(DynamicPlugin)
  pluginAppInstance.component('plugin-card', PluginCard)
  pluginAppInstance.component('plugin-status', PluginStatus)
  pluginAppInstance.component('plugin-btn', PluginBtn)
  pluginAppInstance.mount(container)
}

// 简单HTML渲染（原有逻辑）
function renderSimpleHtml(container, template, data) {
  let html = template
  for (const key in data) {
    if (typeof data[key] !== 'function') {
      html = html.replace(new RegExp(`{{\\s*${key}\\s*}}`, 'g'), data[key])
    }
  }
  container.innerHTML = html
  
  // 绑定点击事件
  container.querySelectorAll('[\\@click]').forEach(el => {
    const methodName = el.getAttribute('@click')
    if (data[methodName]) {
      el.addEventListener('click', async () => {
        const result = await data[methodName]()
        if (result !== undefined) {
          pluginOutput.value = typeof result === 'string' ? result : JSON.stringify(result, null, 2)
        }
        // 重新渲染模板
        if (currentPluginDef?.template) {
          renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
        }
      })
    }
  })
  
  // 绑定v-model双向绑定
  container.querySelectorAll('[v-model]').forEach(el => {
    const key = el.getAttribute('v-model')
    if (key in data) {
      el.value = data[key] || ''
      el.addEventListener('input', (e) => {
        pluginData.value[key] = e.target.value
      })
    }
  })
  
  // 绑定输入框change事件
  container.querySelectorAll('[\\@change]').forEach(el => {
    const methodName = el.getAttribute('@change')
    if (data[methodName]) {
      el.addEventListener('change', async (e) => {
        await data[methodName](e.target.value)
      })
    }
  })
}

// 运行插件
async function runPlugin(plugin) {
  currentPlugin.value = plugin
  pluginOutput.value = ''
  pluginData.value = {}
  currentPluginDef = null
  // 设置当前插件名称（用于storage API）
  currentPluginName = plugin.filename ? plugin.filename.replace('.js', '') : plugin.name
  showPluginModal.value = true
  
  await nextTick()
  
  try {
    const pluginCode = plugin.content
    const sandbox = { window: { PLUGIN: null }, console: console, $api: pluginAPI }
    
    const fn = new Function('window', '$api', 'console', pluginCode + '; return window.PLUGIN;')
    const pluginDef = fn(sandbox.window, pluginAPI, console)
    
    if (pluginDef) {
      currentPluginDef = pluginDef
      
      // 初始化插件数据
      if (typeof pluginDef.data === 'function') {
        pluginData.value = pluginDef.data()
      }
      
      // 绑定方法，注入$api和$data
      if (pluginDef.methods) {
        for (const key in pluginDef.methods) {
          pluginData.value[key] = pluginDef.methods[key].bind({
            $data: pluginData.value,
            $api: pluginAPI,
            $refresh: () => {
              const container = document.getElementById('plugin-container')
              if (container && currentPluginDef?.template) {
                renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
              }
            }
          })
        }
      }
      
      // 渲染模板
      if (pluginDef.template) {
        await nextTick()
        const container = document.getElementById('plugin-container')
        if (container) {
          renderPluginTemplate(container, pluginDef.template, pluginData.value)
        }
      }
      
      // 调用mounted - 传入完整上下文包括绑定后的方法
      if (pluginDef.mounted) {
        const mountedContext = {
          $data: pluginData.value,
          $api: pluginAPI,
          $refresh: () => {
            const container = document.getElementById('plugin-container')
            if (container && currentPluginDef?.template) {
              renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
            }
          }
        }
        // 将绑定后的方法也加入上下文
        if (pluginDef.methods) {
          for (const key in pluginDef.methods) {
            mountedContext[key] = pluginData.value[key]
          }
        }
        pluginDef.mounted.call(mountedContext)
      }
    }
  } catch (e) {
    pluginOutput.value = '插件执行错误: ' + e.message
    error('插件执行错误')
  }
}

function closePluginModal() {
  // 1. 调用 destroyed 生命周期钩子
  if (currentPluginDef?.destroyed) {
    try {
      const destroyedContext = {
        $data: pluginData.value,
        $api: pluginAPI,
        $refresh: () => {
          const container = document.getElementById('plugin-container')
          if (container && currentPluginDef?.template) {
            renderPluginTemplate(container, currentPluginDef.template, pluginData.value)
          }
        }
      }
      // 将方法也加入上下文
      if (currentPluginDef.methods) {
        for (const key in currentPluginDef.methods) {
          if (pluginData.value[key]) {
            destroyedContext[key] = pluginData.value[key]
          }
        }
      }
      currentPluginDef.destroyed.call(destroyedContext)
    } catch (e) {
      console.error('Plugin destroyed hook error:', e)
    }
  }
  
  // 2. 自动清理所有定时器
  pluginTimers.forEach(t => {
    if (t.type === 'interval') {
      clearInterval(t.id)
    } else {
      clearTimeout(t.id)
    }
  })
  pluginTimers = []
  
  // 3. 清理Vue组件实例
  if (pluginAppInstance) {
    pluginAppInstance.unmount()
    pluginAppInstance = null
  }
  
  // 4. 重置状态
  showPluginModal.value = false
  currentPlugin.value = null
  currentPluginDef = null
  currentPluginName = ''
  pluginData.value = {}
  pluginOutput.value = ''
}

onMounted(() => {
  fetchPlugins()
})
</script>


<template>
  <div class="space-y-4 sm:space-y-6">
    <!-- 标题栏 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-4 sm:p-6 shadow-lg">
      <div class="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
        <div class="flex items-center space-x-3">
          <div class="w-10 h-10 sm:w-12 sm:h-12 rounded-xl bg-gradient-to-br from-violet-500 to-purple-500 flex items-center justify-center shadow-lg shadow-violet-500/30">
            <font-awesome-icon icon="puzzle-piece" class="text-white text-lg sm:text-xl" />
          </div>
          <div>
            <h3 class="text-slate-900 dark:text-white font-semibold text-sm sm:text-base">插件商城</h3>
            <p class="text-slate-500 dark:text-white/50 text-xs sm:text-sm">已安装 {{ plugins.length }} 个插件</p>
          </div>
        </div>
        <!-- 操作按钮组 - 美化布局 -->
        <div class="flex items-center gap-2">
          <!-- 主要操作 -->
          <div class="flex items-center bg-slate-100 dark:bg-white/5 rounded-xl p-1">
            <button @click="showUploadModal = true"
              class="px-3 py-1.5 bg-violet-500 hover:bg-violet-600 text-white rounded-lg text-xs font-medium transition-all flex items-center space-x-1.5">
              <font-awesome-icon icon="plus" class="text-xs" />
              <span>插件</span>
            </button>
            <button @click="openScriptManager"
              class="px-3 py-1.5 hover:bg-white/50 dark:hover:bg-white/10 text-slate-600 dark:text-white/70 rounded-lg text-xs font-medium transition-all flex items-center space-x-1.5 ml-1">
              <font-awesome-icon icon="terminal" class="text-xs" />
              <span>脚本</span>
            </button>
          </div>
          <!-- 导入导出 -->
          <div class="flex items-center bg-slate-100 dark:bg-white/5 rounded-xl p-1">
            <button @click="handleExportAll" v-if="plugins.length > 0"
              class="px-2.5 py-1.5 hover:bg-blue-500/20 text-blue-500 rounded-lg text-xs font-medium transition-all flex items-center space-x-1" title="导出全部">
              <font-awesome-icon icon="file-export" class="text-xs" />
            </button>
            <label class="px-2.5 py-1.5 hover:bg-cyan-500/20 text-cyan-500 rounded-lg text-xs font-medium transition-all flex items-center space-x-1 cursor-pointer" title="导入全部">
              <font-awesome-icon icon="file-import" class="text-xs" />
              <input type="file" accept=".json" @change="handleImportAll" class="hidden">
            </label>
            <button @click="handleDeleteAll" v-if="plugins.length > 0"
              class="px-2.5 py-1.5 hover:bg-red-500/20 text-red-500 rounded-lg text-xs font-medium transition-all" title="清空插件">
              <font-awesome-icon icon="trash-alt" class="text-xs" />
            </button>
          </div>
          <!-- 刷新 -->
          <button @click="fetchPlugins" :disabled="loading"
            class="w-8 h-8 bg-slate-100 dark:bg-white/5 hover:bg-slate-200 dark:hover:bg-white/10 text-slate-500 dark:text-white/50 rounded-xl text-xs transition-all flex items-center justify-center">
            <font-awesome-icon :icon="loading ? 'spinner' : 'sync-alt'" :class="loading ? 'animate-spin' : ''" />
          </button>
        </div>
      </div>
    </div>

    <!-- 插件列表 -->
    <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
      <div v-for="plugin in plugins" :key="plugin.filename"
        class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-4 sm:p-5 shadow-lg hover:shadow-xl transition-all group">
        <div class="flex items-start justify-between mb-3">
          <div :class="`w-12 h-12 rounded-xl bg-gradient-to-br ${plugin.color || 'from-blue-500 to-cyan-400'} flex items-center justify-center shadow-lg`">
            <font-awesome-icon :icon="(plugin.icon || 'fa-puzzle-piece').replace('fa-', '')" class="text-white text-xl" />
          </div>
          <div class="flex items-center space-x-1 opacity-0 group-hover:opacity-100 transition-opacity">
            <button @click="handleExport(plugin)" title="导出插件"
              class="w-8 h-8 rounded-lg bg-blue-500/10 hover:bg-blue-500/20 text-blue-500 flex items-center justify-center transition-all">
              <font-awesome-icon icon="download" class="text-sm" />
            </button>
            <button @click="editPlugin(plugin)" title="编辑插件"
              class="w-8 h-8 rounded-lg bg-amber-500/10 hover:bg-amber-500/20 text-amber-500 flex items-center justify-center transition-all">
              <font-awesome-icon icon="edit" class="text-sm" />
            </button>
            <button @click="handleDelete(plugin)" title="删除插件"
              class="w-8 h-8 rounded-lg bg-red-500/10 hover:bg-red-500/20 text-red-500 flex items-center justify-center transition-all">
              <font-awesome-icon icon="trash-alt" class="text-sm" />
            </button>
          </div>
        </div>
        <h4 class="text-slate-900 dark:text-white font-semibold text-base mb-1">{{ plugin.name }}</h4>
        <p class="text-slate-500 dark:text-white/50 text-xs mb-1">v{{ plugin.version }} · {{ plugin.author }}</p>
        <p class="text-slate-600 dark:text-white/60 text-sm mb-4 line-clamp-2">{{ plugin.description || '暂无描述' }}</p>
        <button @click="runPlugin(plugin)"
          class="w-full py-2.5 bg-gradient-to-r from-violet-500 to-purple-500 hover:from-violet-600 hover:to-purple-600 text-white rounded-xl text-sm font-medium transition-all flex items-center justify-center space-x-2">
          <font-awesome-icon icon="play" />
          <span>运行</span>
        </button>
      </div>

      <!-- 空状态 -->
      <div v-if="plugins.length === 0 && !loading"
        class="col-span-full rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-8 sm:p-12 text-center">
        <div class="w-16 h-16 mx-auto mb-4 rounded-2xl bg-violet-500/10 flex items-center justify-center">
          <font-awesome-icon icon="puzzle-piece" class="text-violet-500 text-2xl" />
        </div>
        <h4 class="text-slate-900 dark:text-white font-semibold mb-2">暂无插件</h4>
        <p class="text-slate-500 dark:text-white/50 text-sm mb-4">点击上方"上传插件"按钮添加您的第一个插件</p>
      </div>
    </div>


    <!-- 上传弹窗 -->
    <Teleport to="body">
      <Transition name="modal">
        <div v-if="showUploadModal" class="fixed inset-0 z-50 flex items-center justify-center p-4">
          <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="showUploadModal = false"></div>
          <div class="relative w-full max-w-2xl bg-white dark:bg-slate-800 rounded-2xl shadow-2xl overflow-hidden">
            <div class="p-4 sm:p-6 border-b border-slate-200 dark:border-white/10">
              <div class="flex items-center justify-between">
                <h3 class="text-lg font-semibold text-slate-900 dark:text-white">上传插件</h3>
                <button @click="showUploadModal = false" class="w-8 h-8 rounded-lg bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 flex items-center justify-center">
                  <font-awesome-icon icon="times" class="text-slate-600 dark:text-white/60" />
                </button>
              </div>
            </div>
            <div class="p-4 sm:p-6 space-y-4">
              <div @dragover.prevent="isDragging = true" @dragleave.prevent="isDragging = false" @drop.prevent="handleDrop"
                class="relative border-2 border-dashed rounded-xl p-6 text-center transition-all cursor-pointer"
                :class="isDragging ? 'border-violet-500 bg-violet-500/10' : 'border-slate-300 dark:border-white/20 hover:border-violet-400'">
                <input type="file" accept=".js" @change="handleFileSelect" class="absolute inset-0 opacity-0 cursor-pointer">
                <div class="w-12 h-12 mx-auto mb-3 rounded-xl bg-violet-500/10 flex items-center justify-center">
                  <font-awesome-icon icon="cloud-upload-alt" class="text-violet-500 text-xl" />
                </div>
                <p class="text-slate-700 dark:text-white/80 font-medium text-sm mb-1">{{ isDragging ? '释放文件' : '点击或拖拽上传' }}</p>
                <p class="text-slate-500 dark:text-white/50 text-xs">支持 .js 格式插件文件</p>
              </div>
              <div>
                <label class="block text-sm font-medium text-slate-700 dark:text-white/80 mb-2">插件名称</label>
                <input v-model="pluginName" type="text" placeholder="可选，留空自动生成"
                  class="w-full px-4 py-3 bg-slate-100 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-violet-400 focus:ring-2 focus:ring-violet-400/20 transition-all">
              </div>
              <div>
                <label class="block text-sm font-medium text-slate-700 dark:text-white/80 mb-2">插件代码</label>
                <textarea v-model="pluginContent" rows="10" placeholder="粘贴插件代码或上传文件..."
                  class="w-full px-4 py-3 bg-slate-100 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-violet-400 focus:ring-2 focus:ring-violet-400/20 transition-all font-mono text-sm resize-none"></textarea>
              </div>
            </div>
            <div class="p-4 sm:p-6 border-t border-slate-200 dark:border-white/10 flex justify-end space-x-3">
              <button @click="showUploadModal = false" class="px-4 py-2 bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 text-slate-700 dark:text-white/80 rounded-xl text-sm font-medium transition-all">取消</button>
              <button @click="handleUpload" :disabled="uploading"
                class="px-4 py-2 bg-violet-500 hover:bg-violet-600 text-white rounded-xl text-sm font-medium transition-all disabled:opacity-50 flex items-center">
                <font-awesome-icon :icon="uploading ? 'spinner' : 'upload'" :class="uploading ? 'animate-spin' : ''" class="mr-2" />
                {{ uploading ? '上传中...' : '上传' }}
              </button>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- 插件编辑弹窗 -->
    <Teleport to="body">
      <Transition name="modal">
        <div v-if="showEditPluginModal" class="fixed inset-0 z-50 flex items-center justify-center p-4">
          <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="showEditPluginModal = false"></div>
          <div class="relative w-full max-w-3xl max-h-[85vh] bg-white dark:bg-slate-800 rounded-2xl shadow-2xl overflow-hidden flex flex-col">
            <div class="p-4 border-b border-slate-200 dark:border-white/10 flex items-center justify-between flex-shrink-0">
              <div class="flex items-center space-x-3">
                <div :class="`w-10 h-10 rounded-xl bg-gradient-to-br ${editingPlugin?.color || 'from-amber-500 to-orange-500'} flex items-center justify-center`">
                  <font-awesome-icon icon="edit" class="text-white" />
                </div>
                <div>
                  <h3 class="text-lg font-semibold text-slate-900 dark:text-white">编辑插件</h3>
                  <p class="text-xs text-slate-500 dark:text-white/50">{{ editingPlugin?.name }}</p>
                </div>
              </div>
              <button @click="showEditPluginModal = false" class="w-8 h-8 bg-slate-100 dark:bg-white/10 hover:bg-slate-200 dark:hover:bg-white/20 rounded-lg flex items-center justify-center">
                <font-awesome-icon icon="times" class="text-slate-500 dark:text-white/50" />
              </button>
            </div>
            <div class="flex-1 overflow-auto p-4">
              <textarea v-model="editPluginContent" rows="20"
                class="w-full h-full min-h-[400px] px-4 py-3 bg-slate-100 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white font-mono text-sm resize-none focus:border-amber-400 focus:ring-2 focus:ring-amber-400/20 transition-all"></textarea>
            </div>
            <div class="p-4 border-t border-slate-200 dark:border-white/10 flex justify-end space-x-3 flex-shrink-0">
              <button @click="showEditPluginModal = false" class="px-4 py-2 bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 text-slate-700 dark:text-white/80 rounded-xl text-sm font-medium transition-all">取消</button>
              <button @click="savePluginEdit" class="px-4 py-2 bg-amber-500 hover:bg-amber-600 text-white rounded-xl text-sm font-medium transition-all flex items-center">
                <font-awesome-icon icon="save" class="mr-2" />
                保存
              </button>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- 脚本管理弹窗 -->
    <Teleport to="body">
      <Transition name="modal">
        <div v-if="showScriptModal" class="fixed inset-0 z-50 flex items-center justify-center p-4">
          <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="showScriptModal = false"></div>
          <div class="relative w-full max-w-3xl max-h-[85vh] bg-white dark:bg-slate-800 rounded-2xl shadow-2xl flex flex-col overflow-hidden">
            <!-- 标题栏 -->
            <div class="p-4 border-b border-slate-200 dark:border-white/10 flex items-center justify-between flex-shrink-0">
              <div class="flex items-center space-x-3">
                <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-emerald-500 to-teal-500 flex items-center justify-center">
                  <font-awesome-icon icon="terminal" class="text-white" />
                </div>
                <div>
                  <h3 class="text-lg font-semibold text-slate-900 dark:text-white">脚本管理</h3>
                  <p class="text-xs text-slate-500 dark:text-white/50">{{ scripts.length }} 个脚本</p>
                </div>
              </div>
              <div class="flex items-center space-x-2">
                <button @click="showNewScriptForm = !showNewScriptForm"
                  class="px-3 py-1.5 bg-emerald-500 hover:bg-emerald-600 text-white rounded-lg text-xs font-medium transition-all flex items-center space-x-1">
                  <font-awesome-icon icon="plus" class="text-xs" />
                  <span>新建</span>
                </button>
                <!-- 导出/导入按钮组 -->
                <div class="flex items-center bg-slate-100 dark:bg-white/5 rounded-lg p-0.5">
                  <button @click="handleExportAllScripts" v-if="scripts.length > 0" title="导出全部"
                    class="w-7 h-7 hover:bg-blue-500/20 text-blue-500 rounded flex items-center justify-center transition-all">
                    <font-awesome-icon icon="file-export" class="text-xs" />
                  </button>
                  <label class="w-7 h-7 hover:bg-cyan-500/20 text-cyan-500 rounded flex items-center justify-center cursor-pointer transition-all" title="导入脚本">
                    <font-awesome-icon icon="file-import" class="text-xs" />
                    <input type="file" accept=".sh,.json" @change="handleImportScripts" class="hidden">
                  </label>
                </div>
                <button @click="fetchScripts" :disabled="scriptsLoading"
                  class="w-8 h-8 bg-slate-100 dark:bg-white/10 hover:bg-slate-200 dark:hover:bg-white/20 rounded-lg flex items-center justify-center">
                  <font-awesome-icon :icon="scriptsLoading ? 'spinner' : 'sync-alt'" :class="scriptsLoading ? 'animate-spin' : ''" class="text-slate-500 dark:text-white/50 text-xs" />
                </button>
                <button @click="showScriptModal = false" class="w-8 h-8 bg-slate-100 dark:bg-white/10 hover:bg-slate-200 dark:hover:bg-white/20 rounded-lg flex items-center justify-center">
                  <font-awesome-icon icon="times" class="text-slate-500 dark:text-white/50" />
                </button>
              </div>
            </div>
            <!-- 新建脚本表单 -->
            <Transition name="slide">
              <div v-if="showNewScriptForm" class="p-4 bg-emerald-50 dark:bg-emerald-900/20 border-b border-emerald-200 dark:border-emerald-800/30 flex-shrink-0">
                <div class="flex items-start gap-3">
                  <input v-model="newScriptName" type="text" placeholder="脚本名称 (如: backup.sh)"
                    class="flex-1 px-3 py-2 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-lg text-sm text-slate-900 dark:text-white placeholder-slate-400">
                  <button @click="handleUploadScript" class="px-4 py-2 bg-emerald-500 hover:bg-emerald-600 text-white rounded-lg text-sm font-medium">保存</button>
                  <button @click="showNewScriptForm = false" class="px-3 py-2 bg-slate-200 dark:bg-white/10 rounded-lg text-sm">取消</button>
                </div>
                <textarea v-model="newScriptContent" rows="4" placeholder="#!/bin/bash&#10;echo 'Hello World'"
                  class="w-full mt-2 px-3 py-2 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-lg text-sm text-slate-900 dark:text-white font-mono placeholder-slate-400 resize-none"></textarea>
              </div>
            </Transition>
            <!-- 脚本列表 -->
            <div class="flex-1 overflow-auto p-4 space-y-3">
              <div v-if="scripts.length === 0 && !scriptsLoading" class="text-center py-12">
                <div class="w-16 h-16 mx-auto mb-4 rounded-2xl bg-slate-100 dark:bg-white/5 flex items-center justify-center">
                  <font-awesome-icon icon="terminal" class="text-slate-400 dark:text-white/30 text-2xl" />
                </div>
                <p class="text-slate-500 dark:text-white/50 text-sm">暂无脚本，点击"新建"添加</p>
              </div>
              <!-- 编辑中的脚本 -->
              <div v-if="editingScript" class="p-4 bg-blue-50 dark:bg-blue-900/20 rounded-xl border border-blue-200 dark:border-blue-800/30">
                <div class="flex items-center justify-between mb-3">
                  <span class="text-sm font-medium text-blue-700 dark:text-blue-300">{{ editingScript.name }}</span>
                  <div class="flex items-center space-x-2">
                    <button @click="saveScriptEdit" class="px-3 py-1 bg-blue-500 hover:bg-blue-600 text-white rounded-lg text-xs font-medium">保存</button>
                    <button @click="editingScript = null" class="px-3 py-1 bg-slate-200 dark:bg-white/10 rounded-lg text-xs">取消</button>
                  </div>
                </div>
                <textarea v-model="editingScript.content" rows="8"
                  class="w-full px-3 py-2 bg-white dark:bg-white/10 border border-blue-200 dark:border-blue-800/30 rounded-lg text-sm text-slate-900 dark:text-white font-mono resize-none"></textarea>
              </div>
              <!-- 脚本列表项 -->
              <div v-for="script in scripts" :key="script.name" v-show="!editingScript || editingScript.name !== script.name"
                class="p-3 bg-slate-50 dark:bg-white/5 rounded-xl border border-slate-200 dark:border-white/10 hover:border-emerald-300 dark:hover:border-emerald-700 transition-all group">
                <div class="flex items-center justify-between">
                  <div class="flex items-center space-x-3">
                    <div class="w-8 h-8 rounded-lg bg-emerald-500/10 flex items-center justify-center">
                      <font-awesome-icon icon="file-code" class="text-emerald-500 text-sm" />
                    </div>
                    <div>
                      <p class="text-sm font-medium text-slate-900 dark:text-white">{{ script.name }}</p>
                      <p class="text-xs text-slate-500 dark:text-white/50">{{ script.size }} 字节</p>
                    </div>
                  </div>
                  <div class="flex items-center space-x-1 opacity-0 group-hover:opacity-100 transition-opacity">
                    <button @click="handleExportScript(script)" title="导出"
                      class="w-7 h-7 rounded-lg bg-cyan-500/10 hover:bg-cyan-500/20 text-cyan-500 flex items-center justify-center">
                      <font-awesome-icon icon="download" class="text-xs" />
                    </button>
                    <button @click="editScript(script)" title="编辑"
                      class="w-7 h-7 rounded-lg bg-blue-500/10 hover:bg-blue-500/20 text-blue-500 flex items-center justify-center">
                      <font-awesome-icon icon="edit" class="text-xs" />
                    </button>
                    <button @click="handleDeleteScript(script)" title="删除"
                      class="w-7 h-7 rounded-lg bg-red-500/10 hover:bg-red-500/20 text-red-500 flex items-center justify-center">
                      <font-awesome-icon icon="trash-alt" class="text-xs" />
                    </button>
                  </div>
                </div>
              </div>
            </div>
            <!-- 底部提示 -->
            <div class="p-3 bg-slate-50 dark:bg-white/5 border-t border-slate-200 dark:border-white/10 flex-shrink-0">
              <p class="text-xs text-slate-500 dark:text-white/50 text-center">
                <font-awesome-icon icon="info-circle" class="mr-1" />
                插件可通过 <code class="bg-slate-200 dark:bg-white/10 px-1 rounded">$api.runScript('脚本名.sh')</code> 调用脚本
              </p>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- 脚本弹窗内部Confirm - 使用内联style确保最高层级 -->
    <Teleport to="body">
      <Transition name="modal">
        <div v-if="scriptConfirm.show" class="fixed inset-0 flex items-center justify-center p-4" style="z-index: 9999 !important;">
          <div class="absolute inset-0 bg-black/60" @click="handleScriptConfirm(false)"></div>
          <div class="relative bg-white dark:bg-slate-700 rounded-xl shadow-2xl p-5 max-w-sm w-full" style="z-index: 10000 !important;">
            <div class="flex items-center space-x-3 mb-3">
              <div :class="['w-10 h-10 rounded-full flex items-center justify-center', scriptConfirm.danger ? 'bg-red-500/10' : 'bg-blue-500/10']">
                <font-awesome-icon :icon="scriptConfirm.danger ? 'exclamation-triangle' : 'question-circle'" :class="scriptConfirm.danger ? 'text-red-500' : 'text-blue-500'" class="text-lg" />
              </div>
              <h4 class="text-slate-900 dark:text-white font-semibold">{{ scriptConfirm.title }}</h4>
            </div>
            <p class="text-slate-600 dark:text-white/70 text-sm mb-4">{{ scriptConfirm.message }}</p>
            <div class="flex justify-end space-x-2">
              <button @click="handleScriptConfirm(false)" class="px-4 py-2 bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 text-slate-700 dark:text-white/80 rounded-lg text-sm font-medium transition-all">取消</button>
              <button @click="handleScriptConfirm(true)" :class="['px-4 py-2 rounded-lg text-sm font-medium transition-all text-white', scriptConfirm.danger ? 'bg-red-500 hover:bg-red-600' : 'bg-blue-500 hover:bg-blue-600']">确定</button>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>

    <!-- 插件运行弹窗 -->
    <Teleport to="body">
      <Transition name="modal">
        <div v-if="showPluginModal" class="fixed inset-0 z-50 flex items-center justify-center p-2 sm:p-4">
          <div class="absolute inset-0 bg-black/50 backdrop-blur-sm" @click="closePluginModal"></div>
          <div class="relative w-full h-full sm:h-auto sm:max-h-[90vh] max-w-4xl bg-white dark:bg-slate-800 rounded-2xl shadow-2xl overflow-hidden flex flex-col">
            <!-- 插件内部Toast -->
            <Transition name="toast">
              <div v-if="pluginToast.show" 
                class="absolute top-16 left-1/2 -translate-x-1/2 z-10 px-4 py-2 rounded-xl shadow-lg text-sm font-medium flex items-center space-x-2"
                :class="{
                  'bg-green-500 text-white': pluginToast.type === 'success',
                  'bg-red-500 text-white': pluginToast.type === 'error',
                  'bg-blue-500 text-white': pluginToast.type === 'info'
                }">
                <font-awesome-icon :icon="pluginToast.type === 'success' ? 'check-circle' : pluginToast.type === 'error' ? 'exclamation-circle' : 'info-circle'" />
                <span>{{ pluginToast.message }}</span>
              </div>
            </Transition>
            <!-- 插件内部Confirm -->
            <Transition name="modal">
              <div v-if="pluginConfirm.show" class="absolute inset-0 z-20 flex items-center justify-center p-4">
                <div class="absolute inset-0 bg-black/30" @click="handlePluginConfirm(false)"></div>
                <div class="relative bg-white dark:bg-slate-700 rounded-xl shadow-xl p-5 max-w-sm w-full">
                  <div class="flex items-center space-x-3 mb-3">
                    <div class="w-10 h-10 rounded-full bg-blue-500/10 flex items-center justify-center">
                      <font-awesome-icon icon="question-circle" class="text-blue-500 text-lg" />
                    </div>
                    <h4 class="text-slate-900 dark:text-white font-semibold">{{ pluginConfirm.title }}</h4>
                  </div>
                  <p class="text-slate-600 dark:text-white/70 text-sm mb-4">{{ pluginConfirm.message }}</p>
                  <div class="flex justify-end space-x-2">
                    <button @click="handlePluginConfirm(false)" class="px-4 py-2 bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 text-slate-700 dark:text-white/80 rounded-lg text-sm font-medium transition-all">取消</button>
                    <button @click="handlePluginConfirm(true)" class="px-4 py-2 bg-blue-500 hover:bg-blue-600 text-white rounded-lg text-sm font-medium transition-all">确定</button>
                  </div>
                </div>
              </div>
            </Transition>
            <div class="p-4 border-b border-slate-200 dark:border-white/10 flex items-center justify-between flex-shrink-0">
              <div class="flex items-center space-x-3">
                <div :class="`w-10 h-10 rounded-xl bg-gradient-to-br ${currentPlugin?.color || 'from-violet-500 to-purple-500'} flex items-center justify-center`">
                  <font-awesome-icon :icon="(currentPlugin?.icon || 'fa-puzzle-piece').replace('fa-', '')" class="text-white" />
                </div>
                <div>
                  <h3 class="text-lg font-semibold text-slate-900 dark:text-white">{{ currentPlugin?.name }}</h3>
                  <p class="text-xs text-slate-500 dark:text-white/50">v{{ currentPlugin?.version }}</p>
                </div>
              </div>
              <button @click="closePluginModal" class="w-10 h-10 rounded-xl bg-slate-200 dark:bg-white/10 hover:bg-slate-300 dark:hover:bg-white/20 flex items-center justify-center transition-all">
                <font-awesome-icon icon="times" class="text-slate-600 dark:text-white/60" />
              </button>
            </div>
            <div class="flex-1 overflow-auto p-4 sm:p-6">
              <div id="plugin-container" class="prose dark:prose-invert max-w-none"></div>
              <div v-if="pluginOutput" class="mt-4 p-4 bg-slate-900 dark:bg-black/50 rounded-xl">
                <div class="flex items-center space-x-2 mb-2">
                  <font-awesome-icon icon="terminal" class="text-green-400 text-sm" />
                  <span class="text-green-400 text-sm font-medium">输出</span>
                </div>
                <pre class="text-green-300 text-sm font-mono whitespace-pre-wrap">{{ pluginOutput }}</pre>
              </div>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>
  </div>
</template>

<style scoped>
.modal-enter-active, .modal-leave-active { transition: all 0.3s ease; }
.modal-enter-from, .modal-leave-to { opacity: 0; }
.modal-enter-from > div:last-child, .modal-leave-to > div:last-child { transform: scale(0.95); }
.line-clamp-2 { display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden; }

/* Toast动画 */
.toast-enter-active, .toast-leave-active { transition: all 0.3s ease; }
.toast-enter-from, .toast-leave-to { opacity: 0; transform: translate(-50%, -20px); }

/* Slide动画 */
.slide-enter-active, .slide-leave-active { transition: all 0.2s ease; }
.slide-enter-from, .slide-leave-to { opacity: 0; max-height: 0; padding-top: 0; padding-bottom: 0; }
.slide-enter-to, .slide-leave-from { max-height: 200px; }

/* 插件容器内的按钮样式 */
:deep(#plugin-container) button,
:deep(#plugin-container) .btn {
  @apply px-4 py-2 bg-violet-500 hover:bg-violet-600 text-white rounded-lg text-sm font-medium transition-all mr-2 mb-2;
}
:deep(#plugin-container) pre {
  @apply bg-slate-100 dark:bg-white/10 p-4 rounded-xl overflow-auto text-sm;
}
:deep(#plugin-container) code {
  @apply bg-slate-100 dark:bg-white/10 px-1.5 py-0.5 rounded text-sm;
}
:deep(#plugin-container) input,
:deep(#plugin-container) textarea {
  @apply w-full px-3 py-2 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-lg text-slate-900 dark:text-white text-sm;
}
</style>
