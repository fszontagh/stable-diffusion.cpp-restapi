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

class AuthManager;

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
     * Create a WebSocket server (no port needed - uses httplib's port).
     * @param auth_manager Optional auth manager. If non-null and enabled(),
     *        WS handshakes must include `?token=<valid>` in the query string;
     *        invalid/missing tokens cause the handshake to be rejected with
     *        an HTTP 401 (browsers can't attach Authorization headers to WS).
     */
    explicit WebSocketServer(AuthManager* auth_manager = nullptr);

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
     * @param username Authenticated username (empty when auth is disabled)
     */
    void handle_connection(void* ws, const std::string& username);

    /**
     * Convert event type to string for JSON serialization
     */
    static std::string event_type_to_string(WSEventType type);

    /**
     * Format timestamp as ISO 8601 string
     */
    static std::string format_timestamp(const std::chrono::system_clock::time_point& tp);

    // Authentication (optional — null if no auth manager was provided)
    AuthManager* auth_manager_ = nullptr;

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

// When the project is built with SDCPP_WEBSOCKET=OFF, websocket_server.cpp is
// excluded from the build, so any out-of-line member or free function above
// becomes an undefined symbol at link time. Two call sites — model_manager.cpp
// and queue_manager.cpp — invoke get_websocket_server() and broadcast(...)
// without #ifdef gating (every other consumer guards them with
// #ifdef SDCPP_WEBSOCKET_ENABLED), and that's the link breakage in issue #9.
//
// The fix lives here rather than wrapping ~20 individual call sites: when the
// macro is undefined, provide header-inline no-op stubs so the WS-disabled
// build links and the `if (auto* ws = get_websocket_server()) { ws->broadcast(...); }`
// pattern degrades to dead code (the compiler will see the nullptr and elide
// the conditional). Future call sites added in any TU stay safe without
// per-call gating.
//
// The real out-of-line definitions in websocket_server.cpp are only compiled
// when SDCPP_WEBSOCKET=ON (CMakeLists.txt gates the source file on the option),
// so there's no ODR conflict.
#ifndef SDCPP_WEBSOCKET_ENABLED
inline WebSocketServer* get_websocket_server() { return nullptr; }
inline void WebSocketServer::broadcast(WSEventType, const nlohmann::json&) {}
#endif

} // namespace sdcpp
