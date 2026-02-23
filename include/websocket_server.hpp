#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <vector>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Event types for WebSocket messages
 */
enum class WSEventType {
    // Job lifecycle events
    JobAdded,           // New job added to queue
    JobStatusChanged,   // Job status changed (pending->processing->completed/failed/cancelled)
    JobProgress,        // Job progress update (step/total_steps)
    JobPreview,         // Job preview image during generation
    JobCancelled,       // Job was cancelled

    // Model lifecycle events
    ModelLoadingProgress, // Model loading progress
    ModelLoaded,        // Model finished loading
    ModelLoadFailed,    // Model failed to load
    ModelUnloaded,      // Model was unloaded

    // Upscaler events
    UpscalerLoaded,     // Upscaler finished loading
    UpscalerUnloaded,   // Upscaler was unloaded

    // Server status
    ServerStatus        // Periodic server status update / heartbeat
};

/**
 * WebSocket event data structure
 */
struct WSEvent {
    WSEventType type;
    nlohmann::json data;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * WebSocket Server for real-time communication with WebUI
 *
 * Uses uWebSockets for high-performance, non-blocking WebSocket handling.
 * Runs in a dedicated thread with its own event loop.
 * Thread-safe broadcasting from other threads via event queue.
 */
class WebSocketServer {
public:
    /**
     * Create a WebSocket server on the specified port
     * @param port Port to listen on
     */
    explicit WebSocketServer(int port);

    /**
     * Destructor - stops the server if running
     */
    ~WebSocketServer();

    // Non-copyable, non-movable
    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;
    WebSocketServer(WebSocketServer&&) = delete;
    WebSocketServer& operator=(WebSocketServer&&) = delete;

    /**
     * Start the WebSocket server (spawns a new thread)
     */
    void start();

    /**
     * Stop the WebSocket server and wait for thread to finish
     */
    void stop();

    /**
     * Request the server to stop (async-signal-safe, doesn't join thread)
     * Call stop() afterwards from main thread to complete shutdown
     */
    void request_stop();

    /**
     * Check if server is running
     * @return true if server is running
     */
    bool is_running() const { return running_.load(); }

    /**
     * Broadcast an event to all connected clients
     * Thread-safe - can be called from any thread
     * @param type Event type
     * @param data Event data as JSON
     */
    void broadcast(WSEventType type, const nlohmann::json& data);

    /**
     * Get the number of connected clients
     * @return Number of connected clients
     */
    size_t get_client_count() const { return client_count_.load(); }

    /**
     * Get the port the server is listening on
     * @return Port number
     */
    int get_port() const { return port_; }

private:
    /**
     * Main server thread function
     */
    void server_thread();

    /**
     * Process pending events from the queue and broadcast to clients
     */
    void process_event_queue();

    /**
     * Convert event type to string for JSON serialization
     * @param type Event type
     * @return String representation of event type
     */
    static std::string event_type_to_string(WSEventType type);

    /**
     * Format timestamp as ISO 8601 string
     * @param tp Time point
     * @return ISO 8601 formatted string
     */
    static std::string format_timestamp(const std::chrono::system_clock::time_point& tp);

    // Server configuration
    int port_;

    // Thread management
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    std::thread server_thread_;

    // Event queue for thread-safe broadcasting
    mutable std::mutex event_queue_mutex_;
    std::queue<WSEvent> event_queue_;

    // Connection tracking
    std::atomic<size_t> client_count_{0};

    // Rate limiting for progress events
    std::chrono::steady_clock::time_point last_progress_broadcast_;
    static constexpr std::chrono::milliseconds PROGRESS_THROTTLE_MS{100};

    // uWebSockets internals (stored as void* to avoid header dependency in public API)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Status provider callback type
 * Returns JSON with current server status (model loaded state, etc.)
 */
using StatusProviderCallback = std::function<nlohmann::json()>;

/**
 * Global WebSocket server instance accessor
 * Used by other components to broadcast events
 */
WebSocketServer* get_websocket_server();

/**
 * Set the global WebSocket server instance
 * Called during server initialization
 */
void set_websocket_server(WebSocketServer* server);

/**
 * Set the status provider callback
 * Called to get current server status for new client connections
 */
void set_status_provider(StatusProviderCallback callback);

/**
 * Get the current server status using the registered provider
 * Returns empty JSON if no provider is set
 */
nlohmann::json get_server_status();

} // namespace sdcpp
