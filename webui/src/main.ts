import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { createRouter, createWebHashHistory } from 'vue-router'
import App from './App.vue'
import './style.css'
import { initSentry } from './services/sentry'

// Dashboard is the post-login landing route, so eager-load it — async
// loading there means the user sees a flash of nothing on first paint.
import Dashboard from './views/Dashboard.vue'

// Every other view is lazy-loaded. Vite emits a separate chunk per
// dynamic import, so the initial bundle stays small and a route's code
// downloads only when the user navigates to it. Generate.vue + Queue.vue
// are the heaviest (~30+ KB each pre-gzip), and most users never visit
// Settings/Downloads/RecycleBin in a session at all.
const Models      = () => import('./views/Models.vue')
const ModelLoad   = () => import('./views/ModelLoad.vue')
const Generate    = () => import('./views/Generate.vue')
const Upscale     = () => import('./views/Upscale.vue')
const Queue       = () => import('./views/Queue.vue')
const RecycleBin  = () => import('./views/RecycleBin.vue')
const Chat        = () => import('./views/Chat.vue')
const Downloads   = () => import('./views/Downloads.vue')
const Settings    = () => import('./views/Settings.vue')

// Auth is enforced server-side via the sdcpp_auth cookie. The server
// redirects /ui/* to /login (a server-rendered HTML page) for any
// unauthenticated request, so the SPA never needs an in-app login route
// or navigation guard.
const routes = [
  { path: '/', redirect: '/dashboard' },
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
app.mount('#app')
