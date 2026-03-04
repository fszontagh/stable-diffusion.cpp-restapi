#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>

// Forward declare httplib types
namespace httplib {
class Server;
}

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
    JobDeleted,         // Job was deleted (soft or hard delete)
    JobRestored,        // Job was restored from recycle bin

    // Model lifecycle events
    ModelLoadingProgress, // Model loading progress
    ModelLoaded,        // Model finished loading
    ModelLoadFailed,    // Model failed to load
    ModelUnloaded,      // Model was unloaded

    // Upscaler events
    UpscalerLoaded,     // Upscaler finished loading
    UpscalerUnloaded,   // Upscaler was unloaded

    // Server status
    ServerStatus,       // Periodic server status update / heartbeat
    ServerShutdown,     // Server is shutting down

    // Memory status
    MemoryStatus        // Memory usage update (RAM + VRAM)
};

/**
 * WebSocket event data structure
 */
struct WSEvent {
    WSEventType type;
    nlohmann::json data;
    std::chrono::system_clock::time_point timestamp;
};

// Forward declare internal client connection type (defined in .cpp)
struct ClientConnection;

/**
 * WebSocket Server for real-time communication with WebUI
 *
 * Uses cpp-httplib's built-in WebSocket support (v0.33+).
 * Runs on the same port as HTTP via server.WebSocket("/ws", handler).
 * Each client connection runs in its own thread (httplib's thread pool).
 * Thread-safe broadcasting via per-client send mutexes.
 */
class WebSocketServer {
public:
    /**
     * Create a WebSocket server (no port needed - uses httplib's port)
     */
    WebSocketServer();

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
     * Register the /ws WebSocket endpoint with the httplib server
     * Must be called BEFORE server.listen()
     * @param server The httplib::Server to register the endpoint on
     */
    void setup_endpoint(httplib::Server& server);

    /**
     * Mark the WebSocket server as running
     */
    void start();

    /**
     * Stop the WebSocket server - closes all client connections
     */
    void stop();

    /**
     * Request the server to stop (async-signal-safe)
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

private:
    /**
     * Handle a new WebSocket connection (runs in httplib's thread per connection)
     * @param ws The WebSocket connection (type-erased, actual type is httplib::ws::WebSocket)
     */
    void handle_connection(void* ws);

    /**
     * Convert event type to string for JSON serialization
     */
    static std::string event_type_to_string(WSEventType type);

    /**
     * Format timestamp as ISO 8601 string
     */
    static std::string format_timestamp(const std::chrono::system_clock::time_point& tp);

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};

    // Client registry
    mutable std::mutex clients_mutex_;
    std::vector<ClientConnection*> clients_;
    std::atomic<size_t> client_count_{0};
    size_t next_client_id_{0};

    // Rate limiting for progress events
    std::chrono::steady_clock::time_point last_progress_broadcast_;
    static constexpr std::chrono::milliseconds PROGRESS_THROTTLE_MS{100};
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
