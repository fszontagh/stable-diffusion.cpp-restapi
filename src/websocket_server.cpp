#include "websocket_server.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>

#include <httplib.h>

namespace sdcpp {

/**
 * Per-client connection data (internal to .cpp)
 */
struct ClientConnection {
    size_t id;
    httplib::ws::WebSocket* ws;  // non-owning, valid during handler lifetime
    std::mutex send_mutex;       // protects ws->send() from concurrent broadcasts
};

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

WebSocketServer::WebSocketServer()
    : last_progress_broadcast_(std::chrono::steady_clock::now())
{
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::setup_endpoint(httplib::Server& server) {
    server.WebSocket("/ws", [this](const httplib::Request& /*req*/,
                                    httplib::ws::WebSocket& ws) {
        handle_connection(static_cast<void*>(&ws));
    });
    std::cout << "[WebSocket] Registered /ws endpoint" << std::endl;
}

void WebSocketServer::start() {
    if (running_.load()) {
        return;
    }
    should_stop_.store(false);
    running_.store(true);
    std::cout << "[WebSocket] Server started (using httplib built-in WebSocket)" << std::endl;
}

void WebSocketServer::request_stop() {
    should_stop_.store(true);
}

void WebSocketServer::stop() {
    if (!running_.load()) {
        return;
    }

    should_stop_.store(true);

    // Send shutdown message and close all client connections
    {
        nlohmann::json message = {
            {"event", "server_shutdown"},
            {"timestamp", format_timestamp(std::chrono::system_clock::now())},
            {"data", {{"reason", "Server is shutting down"}}}
        };
        std::string msg_str = message.dump();

        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto* client : clients_) {
            std::lock_guard<std::mutex> send_lock(client->send_mutex);
            try {
                client->ws->send(msg_str);
                client->ws->close(httplib::ws::CloseStatus::GoingAway, "Server shutting down");
            } catch (...) {
                // Ignore errors during shutdown
            }
        }
    }

    running_.store(false);
    std::cout << "[WebSocket] Server stopped" << std::endl;
}

void WebSocketServer::handle_connection(void* ws_ptr) {
    auto& ws = *static_cast<httplib::ws::WebSocket*>(ws_ptr);

    // Register client
    ClientConnection client;
    client.ws = &ws;

    size_t client_id;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client.id = next_client_id_++;
        client_id = client.id;
        clients_.push_back(&client);
        client_count_.store(clients_.size());
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
        ws.send(response.dump());
    }

    // Read loop - blocks until message arrives or connection closes
    std::string msg;
    while (ws.is_open() && !should_stop_.load()) {
        auto result = ws.read(msg);

        if (result == httplib::ws::Text) {
            // Handle incoming text messages
            try {
                auto j = nlohmann::json::parse(msg);
                std::string type = j.value("type", "");

                if (type == "ping") {
                    nlohmann::json response = {
                        {"event", "pong"},
                        {"timestamp", format_timestamp(std::chrono::system_clock::now())}
                    };
                    std::lock_guard<std::mutex> send_lock(client.send_mutex);
                    ws.send(response.dump());
                } else if (type == "get_status") {
                    auto srv_status = get_server_status();
                    nlohmann::json response = {
                        {"event", "server_status"},
                        {"timestamp", format_timestamp(std::chrono::system_clock::now())},
                        {"data", srv_status}
                    };
                    std::lock_guard<std::mutex> send_lock(client.send_mutex);
                    ws.send(response.dump());
                }
            } catch (const std::exception& /*e*/) {
                // Ignore malformed messages
            }
        } else if (result == httplib::ws::Fail) {
            // Connection closed or error
            break;
        }
        // Binary messages are ignored
    }

    // Unregister client
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(
            std::remove(clients_.begin(), clients_.end(), &client),
            clients_.end()
        );
        client_count_.store(clients_.size());
    }

    std::cout << "[WebSocket] Client disconnected (id=" << client_id
              << ", total=" << client_count_.load() << ")" << std::endl;
}

void WebSocketServer::broadcast(WSEventType type, const nlohmann::json& data) {
    if (!running_.load()) {
        return;
    }

    // Rate limit progress events to prevent flooding
    if (type == WSEventType::JobProgress) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_progress_broadcast_ < PROGRESS_THROTTLE_MS) {
            return;  // Skip this update
        }
        last_progress_broadcast_ = now;
    }

    // Build JSON message once
    nlohmann::json message = {
        {"event", event_type_to_string(type)},
        {"timestamp", format_timestamp(std::chrono::system_clock::now())},
        {"data", data}
    };
    std::string msg_str = message.dump();

    // Send to all connected clients
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto* client : clients_) {
        std::lock_guard<std::mutex> send_lock(client->send_mutex);
        try {
            client->ws->send(msg_str);
        } catch (...) {
            // Ignore send errors - client will be cleaned up by its read loop
        }
    }
}

std::string WebSocketServer::event_type_to_string(WSEventType type) {
    switch (type) {
        case WSEventType::JobAdded:            return "job_added";
        case WSEventType::JobStatusChanged:    return "job_status_changed";
        case WSEventType::JobProgress:         return "job_progress";
        case WSEventType::JobPreview:          return "job_preview";
        case WSEventType::JobCancelled:        return "job_cancelled";
        case WSEventType::JobDeleted:          return "job_deleted";
        case WSEventType::JobRestored:         return "job_restored";
        case WSEventType::ModelLoadingProgress: return "model_loading_progress";
        case WSEventType::ModelLoaded:         return "model_loaded";
        case WSEventType::ModelLoadFailed:     return "model_load_failed";
        case WSEventType::ModelUnloaded:       return "model_unloaded";
        case WSEventType::UpscalerLoaded:      return "upscaler_loaded";
        case WSEventType::UpscalerUnloaded:    return "upscaler_unloaded";
        case WSEventType::ServerStatus:        return "server_status";
        case WSEventType::ServerShutdown:      return "server_shutdown";
        case WSEventType::MemoryStatus:        return "memory_status";
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
