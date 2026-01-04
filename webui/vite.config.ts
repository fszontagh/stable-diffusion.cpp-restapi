import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  base: '/ui/',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    sourcemap: false,
    minify: 'terser',
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  },
  server: {
    proxy: {
      '/health': 'http://localhost:8080',
      '/models': 'http://localhost:8080',
      '/txt2img': 'http://localhost:8080',
      '/img2img': 'http://localhost:8080',
      '/txt2vid': 'http://localhost:8080',
      '/upscale': 'http://localhost:8080',
      '/upscaler': 'http://localhost:8080',
      '/queue': 'http://localhost:8080',
      '/output': 'http://localhost:8080',
      '/thumb': 'http://localhost:8080'
    }
  }
})
