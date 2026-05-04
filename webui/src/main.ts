import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { createRouter, createWebHashHistory } from 'vue-router'
import App from './App.vue'
import './style.css'
import { initSentry } from './services/sentry'

import Dashboard from './views/Dashboard.vue'
import Models from './views/Models.vue'
import Generate from './views/Generate.vue'
import Upscale from './views/Upscale.vue'
import Queue from './views/Queue.vue'
import RecycleBin from './views/RecycleBin.vue'
import Chat from './views/Chat.vue'
import Downloads from './views/Downloads.vue'
import Settings from './views/Settings.vue'
import Login from './views/Login.vue'

// Lazy loaded views
const ModelLoad = () => import('./views/ModelLoad.vue')

const routes = [
  { path: '/', redirect: '/dashboard' },
  { path: '/login', name: 'Login', component: Login, meta: { public: true } },
  { path: '/dashboard', name: 'Dashboard', component: Dashboard },
  { path: '/models', name: 'Models', component: Models },
  { path: '/models/load/:modelName', name: 'ModelLoad', component: ModelLoad, props: true },
  { path: '/downloads', name: 'Downloads', component: Downloads },
  { path: '/generate', name: 'Generate', component: Generate },
  { path: '/upscale', name: 'Upscale', component: Upscale },
  { path: '/queue', name: 'Queue', component: Queue },
  { path: '/recycle-bin', name: 'RecycleBin', component: RecycleBin },
  { path: '/chat', name: 'Chat', component: Chat },
  { path: '/settings', name: 'Settings', component: Settings }
]

const router = createRouter({
  history: createWebHashHistory('/ui/'),
  routes
})

const app = createApp(App)

// Initialize Sentry error tracking
initSentry(app, router)

app.use(createPinia())
app.use(router)

// Auth navigation guard: routes are protected by default; add `meta.public: true`
// to opt out (login page is the only public route today). Pinia must be
// installed before this guard fires (see app.use(createPinia()) above).
import { useAuthStore } from './stores/auth'
router.beforeEach((to) => {
  if (to.meta?.public) return true
  const auth = useAuthStore()
  if (auth.isAuthenticated) return true
  // Preserve where the user was trying to go so /login can bounce back
  return { name: 'Login', query: { redirect: to.fullPath } }
})

app.mount('#app')
