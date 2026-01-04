#pragma once

#include <string>
#include <mutex>
#include <deque>
#include <chrono>

namespace sdcpp {

/**
 * Thread-safe error capture for sd.cpp library errors.
 * Captures SD_LOG_ERROR messages from the logging callback
 * and makes them available for job error reporting.
 */
class SDErrorCapture {
public:
    static SDErrorCapture& instance() {
        static SDErrorCapture instance;
        return instance;
    }

    /**
     * Capture an error message from sd.cpp
     * Called from the logging callback when level == SD_LOG_ERROR
     */
    void capture(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Trim whitespace from message
        std::string trimmed = message;
        while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ')) {
            trimmed.pop_back();
        }
        while (!trimmed.empty() && (trimmed.front() == '\n' || trimmed.front() == '\r' || trimmed.front() == ' ')) {
            trimmed.erase(0, 1);
        }

        if (trimmed.empty()) return;

        // Store with timestamp
        errors_.push_back({trimmed, std::chrono::steady_clock::now()});

        // Keep only last 10 errors
        while (errors_.size() > 10) {
            errors_.pop_front();
        }
    }

    /**
     * Get the most recent error message and clear the buffer.
     * Returns empty string if no errors captured.
     * Only returns errors from the last 30 seconds (to avoid stale errors).
     */
    std::string get_and_clear() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        std::string result;

        // Find the most recent relevant error (within last 30 seconds)
        for (auto it = errors_.rbegin(); it != errors_.rend(); ++it) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->timestamp);
            if (age.count() < 30) {
                if (!result.empty()) {
                    result = it->message + "; " + result;
                } else {
                    result = it->message;
                }
            }
        }

        errors_.clear();
        return result;
    }

    /**
     * Get the last error without clearing (for inspection)
     */
    std::string peek_last() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (errors_.empty()) return "";
        return errors_.back().message;
    }

    /**
     * Clear all captured errors
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        errors_.clear();
    }

private:
    SDErrorCapture() = default;
    ~SDErrorCapture() = default;
    SDErrorCapture(const SDErrorCapture&) = delete;
    SDErrorCapture& operator=(const SDErrorCapture&) = delete;

    struct ErrorEntry {
        std::string message;
        std::chrono::steady_clock::time_point timestamp;
    };

    mutable std::mutex mutex_;
    std::deque<ErrorEntry> errors_;
};

/**
 * Convenience function to capture an SD error
 */
inline void capture_sd_error(const std::string& message) {
    SDErrorCapture::instance().capture(message);
}

/**
 * Convenience function to get and clear SD errors
 */
inline std::string get_sd_error() {
    return SDErrorCapture::instance().get_and_clear();
}

/**
 * Convenience function to clear SD errors
 */
inline void clear_sd_errors() {
    SDErrorCapture::instance().clear();
}

} // namespace sdcpp
