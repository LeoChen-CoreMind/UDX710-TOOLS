import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  base: './',
  resolve: {
    alias: {
      // 使用包含运行时编译器的Vue版本（支持动态模板编译）
      'vue': 'vue/dist/vue.esm-bundler.js'
    }
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    emptyOutDir: true,
    minify: 'esbuild',
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  },
  server: {
    proxy: {
      '/api': {
        target: 'http://192.168.0.1:80',
        changeOrigin: true
      }
    }
  }
})
