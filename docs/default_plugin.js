/**
 * 使用文档插件 - 默认插件示例
 * 展示插件商城的使用方法和API
 */
window.PLUGIN = {
  name: '使用文档',
  version: '1.0.0',
  author: '系统',
  description: '插件商城使用指南，包含API文档和示例代码',
  icon: 'fa-book',
  color: 'from-emerald-500 to-teal-400',
  
  template: `
    <div class="space-y-6">
      <div class="flex items-center space-x-3 mb-6">
        <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-emerald-500 to-teal-400 flex items-center justify-center">
          <svg class="w-6 h-6 text-white" fill="currentColor" viewBox="0 0 448 512"><path d="M448 360V24c0-13.3-10.7-24-24-24H96C43 0 0 43 0 96v320c0 53 43 96 96 96h328c13.3 0 24-10.7 24-24v-16c0-7.5-3.5-14.3-8.9-18.7-4.2-15.4-4.2-59.3 0-74.7 5.4-4.3 8.9-11.1 8.9-18.6zM128 134c0-3.3 2.7-6 6-6h212c3.3 0 6 2.7 6 6v20c0 3.3-2.7 6-6 6H134c-3.3 0-6-2.7-6-6v-20zm0 64c0-3.3 2.7-6 6-6h212c3.3 0 6 2.7 6 6v20c0 3.3-2.7 6-6 6H134c-3.3 0-6-2.7-6-6v-20zm253.4 250H96c-17.7 0-32-14.3-32-32 0-17.6 14.4-32 32-32h285.4c-1.9 17.1-1.9 46.9 0 64z"/></svg>
        </div>
        <div>
          <h2 class="text-xl font-bold text-slate-900 dark:text-white">插件开发指南</h2>
          <p class="text-slate-500 dark:text-white/50 text-sm">学习如何创建自定义插件</p>
        </div>
      </div>

      <div class="grid gap-4">
        <div class="p-4 bg-slate-100 dark:bg-white/5 rounded-xl">
          <h3 class="font-semibold text-slate-900 dark:text-white mb-2">
            <svg class="inline w-4 h-4 mr-2 text-violet-500" fill="currentColor" viewBox="0 0 640 512"><path d="M278.9 511.5l-61-17.7c-6.4-1.8-10-8.5-8.2-14.9L346.2 8.7c1.8-6.4 8.5-10 14.9-8.2l61 17.7c6.4 1.8 10 8.5 8.2 14.9L293.8 503.3c-1.9 6.4-8.5 10.1-14.9 8.2zm-114-112.2l43.5-46.4c4.6-4.9 4.3-12.7-.8-17.2L117 256l90.6-79.7c5.1-4.5 5.5-12.3.8-17.2l-43.5-46.4c-4.5-4.8-12.1-5.1-17-.5L3.8 247.2c-5.1 4.7-5.1 12.8 0 17.5l144.1 135.1c4.9 4.6 12.5 4.4 17-.5zm327.2.6l144.1-135.1c5.1-4.7 5.1-12.8 0-17.5L googl492.1 112.1c-4.8-4.5-12.4-4.3-17 .5L431.6 159c-4.6 4.9-4.3 12.7.8 17.2L523 256l-90.6 79.7c-5.1 4.5-5.5 12.3-.8 17.2l43.5 46.4c4.5 4.9 12.1 5.1 17 .6z"/></svg>插件结构
          </h3>
          <pre class="text-xs text-slate-600 dark:text-white/70 overflow-auto">window.PLUGIN = {
  name: '插件名称',
  version: '1.0.0',
  author: '作者',
  description: '描述',
  icon: 'fa-rocket',
  color: 'from-blue-500 to-cyan-400',
  template: \`&lt;div&gt;HTML模板&lt;/div&gt;\`,
  data() { return { count: 0 } },
  methods: {
    async run() {
      const result = await this.$api.shell('uname -a')
      this.$api.toast(result, 'success')
    }
  },
  mounted() { console.log('插件已加载') }
}</pre>
        </div>

        <div class="p-4 bg-slate-100 dark:bg-white/5 rounded-xl">
          <h3 class="font-semibold text-slate-900 dark:text-white mb-2">
            <svg class="inline w-4 h-4 mr-2 text-green-500" fill="currentColor" viewBox="0 0 640 512"><path d="M257.981 272.971L63.638 467.314c-9.373 9.373-24.569 9.373-33.941 0L7.029 444.647c-9.357-9.357-9.375-24.522-.04-33.901L161.011 256 6.99 101.255c-9.335-9.379-9.317-24.544.04-33.901l22.667-22.667c9.373-9.373 24.569-9.373 33.941 0L257.981 239.03c9.373 9.372 9.373 24.568 0 33.941zM640 456v-32c0-13.255-10.745-24-24-24H312c-13.255 0-24 10.745-24 24v32c0 13.255 10.745 24 24 24h304c13.255 0 24-10.745 24-24z"/></svg>可用API
          </h3>
          <ul class="text-sm text-slate-600 dark:text-white/70 space-y-2">
            <li><code class="bg-slate-200 dark:bg-white/10 px-1.5 py-0.5 rounded">$api.shell(cmd)</code> - 执行Shell命令</li>
            <li><code class="bg-slate-200 dark:bg-white/10 px-1.5 py-0.5 rounded">$api.toast(msg, type)</code> - 显示提示 (success/error/info)</li>
            <li><code class="bg-slate-200 dark:bg-white/10 px-1.5 py-0.5 rounded">$api.alert(title, msg)</code> - 显示弹窗</li>
            <li><code class="bg-slate-200 dark:bg-white/10 px-1.5 py-0.5 rounded">$api.confirm(title, msg)</code> - 确认对话框</li>
          </ul>
        </div>

        <div class="p-4 bg-slate-100 dark:bg-white/5 rounded-xl">
          <h3 class="font-semibold text-slate-900 dark:text-white mb-3">
            <svg class="inline w-4 h-4 mr-2 text-blue-500" fill="currentColor" viewBox="0 0 448 512"><path d="M424.4 214.7L72.4 6.6C43.8-10.3 0 6.1 0 47.9V464c0 37.5 40.7 60.1 72.4 41.3l352-208c31.4-18.5 31.5-64.1 0-82.6z"/></svg>快速测试
          </h3>
          <div class="flex flex-wrap gap-2">
            <button @click="testShell">执行Shell</button>
            <button @click="testToast">测试Toast</button>
            <button @click="testAlert">测试弹窗</button>
            <button @click="testConfirm">测试确认</button>
          </div>
        </div>

        <div class="p-4 bg-slate-100 dark:bg-white/5 rounded-xl">
          <h3 class="font-semibold text-slate-900 dark:text-white mb-3">
            <svg class="inline w-4 h-4 mr-2 text-purple-500" fill="currentColor" viewBox="0 0 576 512"><path d="M402.6 83.2l90.2 90.2c3.8 3.8 3.8 10 0 13.8L274.4 405.6l-92.8 10.3c-12.4 1.4-22.9-9.1-21.5-21.5l10.3-92.8L388.8 83.2c3.8-3.8 10-3.8 13.8 0zm162-22.9l-48.8-48.8c-15.2-15.2-39.9-15.2-55.2 0l-35.4 35.4c-3.8 3.8-3.8 10 0 13.8l90.2 90.2c3.8 3.8 10 3.8 13.8 0l35.4-35.4c15.2-15.3 15.2-40 0-55.2zM384 346.2V448H64V128h229.8c3.2 0 6.2-1.3 8.5-3.5l40-40c7.6-7.6 2.2-20.5-8.5-20.5H48C21.5 64 0 85.5 0 112v352c0 26.5 21.5 48 48 48h352c26.5 0 48-21.5 48-48V306.2c0-10.7-12.9-16-20.5-8.5l-40 40c-2.2 2.3-3.5 5.3-3.5 8.5z"/></svg>双向绑定示例
          </h3>
          <div class="space-y-3">
            <div>
              <label class="block text-sm text-slate-600 dark:text-white/60 mb-1">输入文本：</label>
              <input v-model="inputText" type="text" placeholder="输入内容会实时显示..."
                class="w-full px-3 py-2 bg-white dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-lg text-slate-900 dark:text-white text-sm">
            </div>
            <div class="p-3 bg-white dark:bg-white/10 rounded-lg">
              <span class="text-slate-500 dark:text-white/50 text-sm">实时显示：</span>
              <span class="text-slate-900 dark:text-white font-medium">{{ inputText }}</span>
            </div>
            <button @click="updateText">随机更新文本</button>
          </div>
        </div>
      </div>
    </div>
  `,

  data() {
    return {
      output: '',
      inputText: '你好，插件商城！'
    }
  },

  methods: {
    async testShell() {
      const result = await this.$api.shell('uname -a')
      return result
    },
    testToast() {
      this.$api.toast('这是一条成功提示！', 'success')
    },
    testAlert() {
      this.$api.alert('提示', '这是一个弹窗消息')
    },
    async testConfirm() {
      const ok = await this.$api.confirm('确认', '是否继续操作？')
      this.$api.toast(ok ? '已确认' : '已取消', ok ? 'success' : 'info')
    },
    updateText() {
      const texts = ['Hello World!', '插件真好用！', '动态更新成功', '双向绑定演示']
      this.$data.inputText = texts[Math.floor(Math.random() * texts.length)]
      this.$refresh()
      this.$api.toast('文本已更新', 'success')
    }
  },

  mounted() {
    console.log('使用文档插件已加载')
  }
}
