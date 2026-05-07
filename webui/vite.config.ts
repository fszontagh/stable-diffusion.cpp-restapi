import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { readFileSync } from 'node:fs'
import { resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

// Read version from VERSION file (single source of truth)
const __dirname = dirname(fileURLToPath(import.meta.url))
const versionFile = resolve(__dirname, '..', 'VERSION')
const version = readFileSync(versionFile, 'utf-8').trim()

export default defineConfig({
  plugins: [vue()],
  base: '/ui/',
  define: {
    __APP_VERSION__: JSON.stringify(version)
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    sourcemap: false,
    minify: 'terser',
    rollupOptions: {
      output: {
        // Split heavy npm dependencies out of the main entry bundle so
        // the initial download stays small and dependencies cache across
        // releases (their hash only changes when the dep changes, not on
        // every app rebuild).
        manualChunks(id) {
          if (id.includes('node_modules')) {
            // Vue runtime + reactivity + router + state — these always
            // load on first paint, so keep them together in one vendor
            // chunk. Splitting them further yields more requests for no
            // payload savings.
            if (id.includes('/vue/') ||
                id.includes('/@vue/') ||
                id.includes('/vue-router/') ||
                id.includes('/pinia/')) {
              return 'vendor-vue'
            }
            // Markdown rendering (used by docs viewer + assistant
            // streaming) — heavyweight and only used on a couple of
            // routes, so it gets its own chunk that loads on demand.
            if (id.includes('/marked/') ||
                id.includes('/highlight') ||
                id.includes('/dompurify/')) {
              return 'vendor-markdown'
            }
            // Everything else from node_modules in a generic vendor bin.
            return 'vendor'
          }
        },
      },
    },
    // Bumped from the 500 KB default — initial bundle stays around the
    // 150 KB-gzip range after the route lazy-load + vendor split. Anything
    // creeping above 700 KB pre-gzip is worth a fresh look at the chunking.
    chunkSizeWarningLimit: 700,
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
