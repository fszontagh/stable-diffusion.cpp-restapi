import { createApp } from 'vue'
import { createPinia } from 'pinia'
import { createRouter, createWebHashHistory } from 'vue-router'
import App from './App.vue'
import './style.css'

import Dashboard from './views/Dashboard.vue'
import Models from './views/Models.vue'
import Generate from './views/Generate.vue'
import Upscale from './views/Upscale.vue'
import Queue from './views/Queue.vue'
import Chat from './views/Chat.vue'
import Downloads from './views/Downloads.vue'

const routes = [
  { path: '/', redirect: '/dashboard' },
  { path: '/dashboard', name: 'Dashboard', component: Dashboard },
  { path: '/models', name: 'Models', component: Models },
  { path: '/downloads', name: 'Downloads', component: Downloads },
  { path: '/generate', name: 'Generate', component: Generate },
  { path: '/upscale', name: 'Upscale', component: Upscale },
  { path: '/queue', name: 'Queue', component: Queue },
  { path: '/chat', name: 'Chat', component: Chat }
]

const router = createRouter({
  history: createWebHashHistory('/ui/'),
  routes
})

const app = createApp(App)
app.use(createPinia())
app.use(router)
app.mount('#app')
