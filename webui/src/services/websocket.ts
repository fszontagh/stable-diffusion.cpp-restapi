/**
 * WebSocket Service for real-time communication with the server
 */

// WebSocket event types (matching backend)
export type WSEventType =
  | 'job_added'
  | 'job_status_changed'
  | 'job_progress'
  | 'job_preview'
  | 'job_cancelled'
  | 'model_loading_progress'
  | 'model_loaded'
  | 'model_load_failed'
  | 'model_unloaded'
  | 'upscaler_loaded'
  | 'upscaler_unloaded'
  | 'server_status'
  | 'server_shutdown'
  | 'pong'

// Event data interfaces
export interface JobAddedData {
  job_id: string
  type: string
  queue_position: number
}

export interface JobStatusChangedData {
  job_id: string
  status: 'pending' | 'processing' | 'completed' | 'failed' | 'cancelled'
  previous_status: string
  outputs?: string[]
  error?: string
}

export interface JobProgressData {
  job_id: string
  step: number
  total_steps: number
}

export interface JobPreviewData {
  job_id: string
  step: number
  frame_count: number
  width: number
  height: number
  is_noisy: boolean
  preview_url: string  // URL to fetch preview image (e.g., /jobs/{id}/preview)
}

export interface JobCancelledData {
  job_id: string
}

export interface ModelLoadingProgressData {
  model_name: string
  step: number
  total_steps: number
}

export interface ModelLoadedData {
  model_name: string
  model_type: string
  model_architecture: string
}

export interface ModelLoadFailedData {
  model_name: string
  error: string
}

export interface ModelUnloadedData {
  model_name: string
}

export interface UpscalerLoadedData {
  model_name: string
  upscale_factor: number
}

export interface UpscalerUnloadedData {
  model_name: string
}

export interface ServerStatusData {
  model_loaded: boolean
  model_loading: boolean
  model_name: string | null
  model_type: string | null
  model_architecture: string | null
  loading_model_name: string | null
  loading_step: number | null
  loading_total_steps: number | null
  last_error: string | null
  loaded_components?: {
    vae?: string | null
    clip_l?: string | null
    clip_g?: string | null
    t5xxl?: string | null
    controlnet?: string | null
    llm?: string | null
    llm_vision?: string | null
  }
  upscaler_loaded?: boolean
  upscaler_name?: string | null
}

export interface ServerShutdownData {
  reason: string
}

// WebSocket message structure
export interface WSMessage<T = unknown> {
  event: WSEventType
  timestamp: string
  data: T
}

// Event handler type
type EventHandler<T> = (data: T) => void

// Connection state
export type ConnectionState = 'connecting' | 'connected' | 'disconnected' | 'reconnecting'

/**
 * WebSocket service class with auto-reconnection and event handling
 */
class WebSocketService {
  private ws: WebSocket | null = null
  private url: string = ''
  private reconnectAttempts = 0
  private maxReconnectAttempts = 10
  private reconnectDelay = 1000
  private maxReconnectDelay = 30000
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private pingTimer: ReturnType<typeof setInterval> | null = null
  private state: ConnectionState = 'disconnected'
  private lastDisconnectTime: number | null = null

  // Event handlers registry
  private handlers: Map<WSEventType, Set<EventHandler<unknown>>> = new Map()
  private stateChangeHandlers: Set<(state: ConnectionState) => void> = new Set()

  /**
   * Connect to WebSocket server
   * @param wsPort WebSocket port from server's /health endpoint. If not provided or undefined, won't connect.
   */
  connect(wsPort?: number): void {
    if (wsPort === undefined || wsPort === null) {
      console.log('[WebSocket] No WebSocket port provided, server may have WebSocket disabled')
      return
    }

    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
      console.log('[WebSocket] Already connected or connecting')
      return
    }

    // Build WebSocket URL using port from server
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const host = window.location.hostname
    this.url = `${protocol}//${host}:${wsPort}`

    this.updateState('connecting')
    console.log(`[WebSocket] Connecting to ${this.url}...`)

    try {
      this.ws = new WebSocket(this.url)

      this.ws.onopen = () => {
        console.log('[WebSocket] Connected')
        this.reconnectAttempts = 0
        this.reconnectDelay = 1000
        this.updateState('connected')
        this.startPingInterval()
      }

      this.ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data) as WSMessage
          this.handleMessage(message)
        } catch (e) {
          console.error('[WebSocket] Failed to parse message:', e)
        }
      }

      this.ws.onclose = (event) => {
        console.log(`[WebSocket] Disconnected (code: ${event.code}, reason: ${event.reason})`)
        this.stopPingInterval()
        this.ws = null

        if (this.state !== 'disconnected') {
          this.scheduleReconnect()
        }
      }

      this.ws.onerror = (error) => {
        console.error('[WebSocket] Error:', error)
      }
    } catch (e) {
      console.error('[WebSocket] Failed to create connection:', e)
      this.scheduleReconnect()
    }
  }

  /**
   * Disconnect from WebSocket server
   */
  disconnect(): void {
    this.updateState('disconnected')

    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }

    this.stopPingInterval()

    if (this.ws) {
      this.ws.close(1000, 'Client disconnect')
      this.ws = null
    }

    console.log('[WebSocket] Disconnected by client')
  }

  /**
   * Check if WebSocket is connected
   */
  isConnected(): boolean {
    return this.ws !== null && this.ws.readyState === WebSocket.OPEN
  }

  /**
   * Get current connection state
   */
  getState(): ConnectionState {
    return this.state
  }

  /**
   * Check if server is available (connected or attempting to connect)
   */
  isServerAvailable(): boolean {
    return this.state === 'connected' || this.state === 'connecting' || this.state === 'reconnecting'
  }

  /**
   * Get timestamp of last disconnect (null if never disconnected)
   */
  getLastDisconnectTime(): number | null {
    return this.lastDisconnectTime
  }

  /**
   * Register an event handler
   */
  on<T>(event: WSEventType, handler: EventHandler<T>): () => void {
    if (!this.handlers.has(event)) {
      this.handlers.set(event, new Set())
    }
    this.handlers.get(event)!.add(handler as EventHandler<unknown>)

    // Return unsubscribe function
    return () => {
      this.handlers.get(event)?.delete(handler as EventHandler<unknown>)
    }
  }

  /**
   * Register a state change handler
   */
  onStateChange(handler: (state: ConnectionState) => void): () => void {
    this.stateChangeHandlers.add(handler)
    return () => {
      this.stateChangeHandlers.delete(handler)
    }
  }

  /**
   * Send a message to the server
   */
  send(data: unknown): void {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(data))
    }
  }

  /**
   * Manually trigger reconnection (resets retry count)
   */
  manualReconnect(): void {
    console.log('[WebSocket] Manual reconnect triggered')
    
    // Reset reconnect attempts to allow new attempts
    this.reconnectAttempts = 0
    this.reconnectDelay = 1000
    
    // Clear any existing reconnect timer
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    
    // If currently disconnected or reconnecting, attempt to reconnect
    if (this.state === 'disconnected' || this.state === 'reconnecting') {
      this.scheduleReconnect()
    }
  }

  // Private methods

  private updateState(newState: ConnectionState): void {
    if (this.state !== newState) {
      this.state = newState
      
      // Track disconnect time
      if (newState === 'disconnected') {
        this.lastDisconnectTime = Date.now()
      }
      
      this.stateChangeHandlers.forEach(handler => handler(newState))
    }
  }

  private handleMessage(message: WSMessage): void {
    const handlers = this.handlers.get(message.event)
    if (handlers) {
      handlers.forEach(handler => {
        try {
          handler(message.data)
        } catch (e) {
          console.error(`[WebSocket] Error in handler for ${message.event}:`, e)
        }
      })
    }
  }

  private scheduleReconnect(): void {
    if (this.state === 'disconnected') {
      return
    }

    if (this.reconnectAttempts >= this.maxReconnectAttempts) {
      console.log('[WebSocket] Max reconnect attempts reached, giving up')
      this.updateState('disconnected')
      return
    }

    this.updateState('reconnecting')
    this.reconnectAttempts++

    // Exponential backoff with jitter
    const delay = Math.min(
      this.reconnectDelay * Math.pow(1.5, this.reconnectAttempts - 1) + Math.random() * 1000,
      this.maxReconnectDelay
    )

    console.log(`[WebSocket] Reconnecting in ${Math.round(delay)}ms (attempt ${this.reconnectAttempts}/${this.maxReconnectAttempts})`)

    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null
      this.connect()
    }, delay)
  }

  private startPingInterval(): void {
    this.stopPingInterval()
    // Send ping every 30 seconds to keep connection alive
    this.pingTimer = setInterval(() => {
      this.send({ type: 'ping' })
    }, 30000)
  }

  private stopPingInterval(): void {
    if (this.pingTimer) {
      clearInterval(this.pingTimer)
      this.pingTimer = null
    }
  }
}

// Export singleton instance
export const wsService = new WebSocketService()
