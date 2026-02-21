#include "websocket_server.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <set>

// uWebSockets includes
#include <App.h>

namespace sdcpp {

// Global WebSocket server instance
static WebSocketServer* g_websocket_server = nullptr;
static StatusProviderCallback g_status_provider = nullptr;

WebSocketServer* get_websocket_server() {
    return g_websocket_server;
}

void set_websocket_server(WebSocketServer* server) {
    g_websocket_server = server;
}

void set_status_provider(StatusProviderCallback callback) {
    g_status_provider = std::move(callback);
}

nlohmann::json get_server_status() {
    if (g_status_provider) {
        return g_status_provider();
    }
    return nlohmann::json::object();
}

// Per-socket data structure
struct PerSocketData {
    // Can be extended to store per-connection data if needed
    size_t id;
};

// Implementation details using PIMPL pattern
struct WebSocketServer::Impl {
    uWS::App* app = nullptr;
    us_listen_socket_t* listen_socket = nullptr;
    uWS::Loop* loop = nullptr;
    std::set<uWS::WebSocket<false, true, PerSocketData>*> clients;
    std::mutex clients_mutex;
    size_t next_client_id = 0;
};

WebSocketServer::WebSocketServer(int port)
    : port_(port)
    , impl_(std::make_unique<Impl>())
    , last_progress_broadcast_(std::chrono::steady_clock::now())
{
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start() {
    if (running_.load()) {
        return;
    }

    should_stop_.store(false);
    running_.store(true);

    server_thread_ = std::thread(&WebSocketServer::server_thread, this);
}

void WebSocketServer::request_stop() {
    // Async-signal-safe stop request - only sets flag and closes listen socket
    // Does NOT join thread (unsafe from signal handler)
    should_stop_.store(true);

    // Close the listening socket to unblock the event loop
    if (impl_->listen_socket) {
        us_listen_socket_close(0, impl_->listen_socket);
        impl_->listen_socket = nullptr;
    }
}

void WebSocketServer::stop() {
    if (!running_.load()) {
        return;
    }

    // Set stop flag
    should_stop_.store(true);

    // Close the listening socket to unblock the event loop
    if (impl_->listen_socket) {
        us_listen_socket_close(0, impl_->listen_socket);
        impl_->listen_socket = nullptr;
    }

    // Wake up the event loop to process the stop - this ensures the deferred
    // client close runs before the loop exits
    if (impl_->loop) {
        impl_->loop->defer([this]() {
            // Close all client connections from within the event loop
            std::lock_guard<std::mutex> lock(impl_->clients_mutex);
            for (auto* ws : impl_->clients) {
                ws->close();
            }
        });

        // Wake the loop again to ensure it processes the close and exits
        impl_->loop->defer([]() {
            // Empty defer just to wake up the loop
        });
    }

    // Wait for server thread to finish with a timeout
    if (server_thread_.joinable()) {
        // Use a timed approach - give the thread 5 seconds to exit gracefully
        auto start = std::chrono::steady_clock::now();
        while (running_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                std::cerr << "[WebSocket] Server thread did not stop in time, detaching..." << std::endl;
                server_thread_.detach();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    running_.store(false);
}

void WebSocketServer::server_thread() {
    std::cout << "[WebSocket] Starting server on port " << port_ << "..." << std::endl;

    // Create the app
    uWS::App app;
    impl_->app = &app;
    impl_->loop = uWS::Loop::get();

    // Configure WebSocket behavior
    app.ws<PerSocketData>("/*", {
        // Settings
        .compression = uWS::SHARED_COMPRESSOR,
        .maxPayloadLength = 16 * 1024,  // 16KB max message
        .idleTimeout = 120,              // 2 minute idle timeout
        .maxBackpressure = 1 * 1024 * 1024, // 1MB max backpressure

        // Connection opened
        .open = [this](auto* ws) {
            size_t client_id;
            {
                std::lock_guard<std::mutex> lock(impl_->clients_mutex);
                auto* data = ws->getUserData();
                data->id = impl_->next_client_id++;
                client_id = data->id;
                impl_->clients.insert(ws);
                client_count_.store(impl_->clients.size());
            }
            std::cout << "[WebSocket] Client connected (id=" << client_id
                      << ", total=" << client_count_.load() << ")" << std::endl;

            // Send current server status to new client
            auto status = get_server_status();
            if (!status.empty()) {
                nlohmann::json response = {
                    {"event", "server_status"},
                    {"timestamp", format_timestamp(std::chrono::system_clock::now())},
                    {"data", status}
                };
                ws->send(response.dump(), uWS::OpCode::TEXT);
            }
        },

        // Message received
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            // Handle incoming messages (ping, get_status, etc.)
            if (opCode == uWS::OpCode::TEXT) {
                try {
                    auto j = nlohmann::json::parse(message);
                    std::string type = j.value("type", "");

                    if (type == "ping") {
                        // Respond with pong
                        nlohmann::json response = {
                            {"event", "pong"},
                            {"timestamp", format_timestamp(std::chrono::system_clock::now())}
                        };
                        ws->send(response.dump(), uWS::OpCode::TEXT);
                    } else if (type == "get_status") {
                        // Respond with current server status
                        auto status = get_server_status();
                        nlohmann::json response = {
                            {"event", "server_status"},
                            {"timestamp", format_timestamp(std::chrono::system_clock::now())},
                            {"data", status}
                        };
                        ws->send(response.dump(), uWS::OpCode::TEXT);
                    }
                } catch (const std::exception& e) {
                    // Ignore malformed messages
                }
            }
        },

        // Connection closed
        .close = [this](auto* ws, int code, std::string_view /*message*/) {
            std::lock_guard<std::mutex> lock(impl_->clients_mutex);
            auto* data = ws->getUserData();
            impl_->clients.erase(ws);
            client_count_.store(impl_->clients.size());
            std::cout << "[WebSocket] Client disconnected (id=" << data->id
                      << ", code=" << code << ", total=" << client_count_.load() << ")" << std::endl;
        }
    });

    // Start listening
    app.listen(port_, [this](auto* listen_socket) {
        if (listen_socket) {
            impl_->listen_socket = listen_socket;
            std::cout << "[WebSocket] Listening on port " << port_ << std::endl;
        } else {
            std::cerr << "[WebSocket] Failed to listen on port " << port_ << std::endl;
            should_stop_.store(true);
        }
    });

    // Run the event loop with periodic event queue processing
    while (!should_stop_.load()) {
        // Process any pending events in the queue
        process_event_queue();

        // Run the event loop for a short time
        impl_->loop->run();

        // Small sleep to prevent busy-waiting when no events
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Close all remaining client connections before exiting
    {
        std::lock_guard<std::mutex> lock(impl_->clients_mutex);
        for (auto* ws : impl_->clients) {
            ws->close();
        }
    }

    // Run the loop one more time to process the close events
    if (impl_->loop) {
        impl_->loop->run();
    }

    std::cout << "[WebSocket] Server stopped" << std::endl;
    impl_->app = nullptr;
    impl_->loop = nullptr;
    running_.store(false);  // Signal that we've fully stopped
}

void WebSocketServer::broadcast(WSEventType type, const nlohmann::json& data) {
    // Rate limit progress events to prevent flooding
    if (type == WSEventType::JobProgress) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_progress_broadcast_ < PROGRESS_THROTTLE_MS) {
            return;  // Skip this update
        }
        last_progress_broadcast_ = now;
    }

    // Create event and add to queue
    WSEvent event;
    event.type = type;
    event.data = data;
    event.timestamp = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        event_queue_.push(std::move(event));
    }

    // Notify the event loop to process the queue
    if (impl_->loop && running_.load()) {
        impl_->loop->defer([this]() {
            process_event_queue();
        });
    }
}

void WebSocketServer::process_event_queue() {
    std::queue<WSEvent> events_to_process;

    // Get all pending events
    {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        std::swap(events_to_process, event_queue_);
    }

    // Process each event
    while (!events_to_process.empty()) {
        const auto& event = events_to_process.front();

        // Build JSON message
        nlohmann::json message = {
            {"event", event_type_to_string(event.type)},
            {"timestamp", format_timestamp(event.timestamp)},
            {"data", event.data}
        };

        std::string msg_str = message.dump();

        // Send to all connected clients
        {
            std::lock_guard<std::mutex> lock(impl_->clients_mutex);
            for (auto* ws : impl_->clients) {
                ws->send(msg_str, uWS::OpCode::TEXT);
            }
        }

        events_to_process.pop();
    }
}

std::string WebSocketServer::event_type_to_string(WSEventType type) {
    switch (type) {
        case WSEventType::JobAdded:            return "job_added";
        case WSEventType::JobStatusChanged:    return "job_status_changed";
        case WSEventType::JobProgress:         return "job_progress";
        case WSEventType::JobPreview:          return "job_preview";
        case WSEventType::JobCancelled:        return "job_cancelled";
        case WSEventType::ModelLoadingProgress: return "model_loading_progress";
        case WSEventType::ModelLoaded:         return "model_loaded";
        case WSEventType::ModelUnloaded:       return "model_unloaded";
        case WSEventType::UpscalerLoaded:      return "upscaler_loaded";
        case WSEventType::UpscalerUnloaded:    return "upscaler_unloaded";
        case WSEventType::ServerStatus:        return "server_status";
        default:                               return "unknown";
    }
}

std::string WebSocketServer::format_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;

    std::tm tm_val;
    gmtime_r(&time_t_val, &tm_val);

    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count()
        << 'Z';
    return oss.str();
}

} // namespace sdcpp
