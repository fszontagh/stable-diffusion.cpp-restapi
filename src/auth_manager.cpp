#include "auth_manager.hpp"
#include "config.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

namespace sdcpp {

namespace {

// Base64url alphabet (RFC 4648 §5) — no padding, '+' → '-', '/' → '_'.
constexpr char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
        out.push_back(kBase64UrlAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < len) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len) v |= uint32_t(data[i + 1]) << 8;
        out.push_back(kBase64UrlAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 12) & 0x3F]);
        if (i + 1 < len) {
            out.push_back(kBase64UrlAlphabet[(v >> 6) & 0x3F]);
        }
        // No padding (base64url, unpadded variant).
    }
    return out;
}

std::string env_or_empty(const char* name) {
    if (const char* v = std::getenv(name)) {
        return std::string(v);
    }
    return std::string();
}

} // namespace

AuthManager::AuthManager(const Config& config)
    : enabled_(config.auth.enabled),
      username_(config.auth.username),
      password_(config.auth.password),
      token_ttl_minutes_(config.auth.token_ttl_minutes > 0 ? config.auth.token_ttl_minutes : 1440)
{
    // Apply environment fallback only if the config field is empty.
    if (username_.empty()) {
        username_ = env_or_empty("SDCPP_AUTH_USERNAME");
    }
    if (password_.empty()) {
        password_ = env_or_empty("SDCPP_AUTH_PASSWORD");
    }

    if (!enabled_) {
        std::cout << "[Auth] Authentication is DISABLED. All endpoints are public." << std::endl;
        return;
    }

    if (username_.empty() || password_.empty()) {
        throw std::runtime_error(
            "FATAL: auth.enabled is true but no credentials configured. "
            "Set auth.username + auth.password in config.json or "
            "SDCPP_AUTH_USERNAME + SDCPP_AUTH_PASSWORD env vars.");
    }

    // Loud warning if the deploy template's placeholder is still in place.
    if (password_ == "CHANGE_ME_BEFORE_DEPLOY") {
        std::cerr << "==========================================================" << std::endl;
        std::cerr << "[Auth] WARNING: auth.password is the placeholder value " << std::endl;
        std::cerr << "       'CHANGE_ME_BEFORE_DEPLOY'. This MUST be replaced  " << std::endl;
        std::cerr << "       before exposing the server. Anyone can log in.    " << std::endl;
        std::cerr << "==========================================================" << std::endl;
    }

    std::cout << "[Auth] Authentication enabled (user='" << username_
              << "', token TTL=" << token_ttl_minutes_ << " min)" << std::endl;
}

bool AuthManager::secure_compare(const std::string& a, const std::string& b) {
    // Constant-time compare: always loops over the longer of the two so
    // input-length differences don't leak via early-return timing.
    const size_t la = a.size();
    const size_t lb = b.size();
    const size_t n  = la > lb ? la : lb;
    unsigned char diff = static_cast<unsigned char>(la ^ lb);
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = i < la ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = i < lb ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}

bool AuthManager::verify_credentials(const std::string& user, const std::string& pass) const {
    // Run BOTH compares unconditionally so we don't leak which field mismatched.
    bool user_ok = secure_compare(user, username_);
    bool pass_ok = secure_compare(pass, password_);
    bool ok = user_ok && pass_ok;
    if (!ok) {
        // Constant-ish delay on failure. Not a true rate limiter but enough
        // to make brute-force impractical without a separate attack tool.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return ok;
}

std::string AuthManager::generate_token() {
    // 32 random bytes → 43 base64url chars (no padding).
    std::array<unsigned char, 32> buf{};
    std::random_device rd;
    std::uniform_int_distribution<unsigned int> dist(0, 255);
    for (auto& b : buf) {
        b = static_cast<unsigned char>(dist(rd));
    }
    return base64url_encode(buf.data(), buf.size());
}

std::string AuthManager::issue_token(const std::string& username) {
    auto now = std::chrono::steady_clock::now();
    auto expires = now + std::chrono::minutes(token_ttl_minutes_);

    std::unique_lock<std::shared_mutex> lock(tokens_mutex_);
    cleanup_expired_locked();

    // Retry if (astronomically unlikely) collision occurs.
    std::string token;
    for (int i = 0; i < 5; ++i) {
        token = generate_token();
        if (tokens_.find(token) == tokens_.end()) {
            break;
        }
    }
    tokens_[token] = TokenEntry{username, expires};
    return token;
}

std::optional<std::string> AuthManager::verify_token(const std::string& token) const {
    if (token.empty()) {
        return std::nullopt;
    }
    auto now = std::chrono::steady_clock::now();

    // Fast path under shared lock — read-only check.
    {
        std::shared_lock<std::shared_mutex> lock(tokens_mutex_);
        auto it = tokens_.find(token);
        if (it == tokens_.end()) {
            return std::nullopt;
        }
        if (it->second.expires_at > now) {
            return it->second.username;
        }
    }

    // Expired — upgrade to exclusive to evict.
    std::unique_lock<std::shared_mutex> lock(tokens_mutex_);
    auto it = tokens_.find(token);
    if (it != tokens_.end() && it->second.expires_at <= now) {
        tokens_.erase(it);
    }
    return std::nullopt;
}

void AuthManager::revoke_token(const std::string& token) {
    std::unique_lock<std::shared_mutex> lock(tokens_mutex_);
    tokens_.erase(token);
}

void AuthManager::cleanup_expired_locked() const {
    auto now = std::chrono::steady_clock::now();
    for (auto it = tokens_.begin(); it != tokens_.end(); ) {
        if (it->second.expires_at <= now) {
            it = tokens_.erase(it);
        } else {
            ++it;
        }
    }
}

const std::unordered_set<std::string>& AuthManager::always_allowed_exact_paths() {
    static const std::unordered_set<std::string> paths = {
        "/health",
        "/auth/login",
        "/openapi.json",
        "/",
        "/ui",
        "/ui/",
        "/docs",
        "/docs/",
        // llms.txt convention (https://llmstxt.org/) — discoverable
        // by AI agents without needing credentials. Lists pointers to
        // /openapi.json and /docs/* so an agent can self-orient.
        "/llms.txt",
        "/llm.txt",
    };
    return paths;
}

const std::vector<std::string>& AuthManager::always_allowed_path_prefixes() {
    // These prefixes bypass the BEARER/cookie middleware. /output and
    // /thumb are NOT in this list any more — they go through the normal
    // cookie-or-bearer path because browsers attach the auth cookie on
    // <img>/<video> tags automatically. /webdav stays here because Basic
    // auth happens in RequestHandlers' pre-routing and DAV clients speak
    // Basic only, not the cookie.
    static const std::vector<std::string> prefixes = {
        "/ui/",
        "/docs/",
        "/webdav/",
    };
    return prefixes;
}

bool AuthManager::is_always_allowed(const std::string& path) {
    const auto& exact = always_allowed_exact_paths();
    if (exact.find(path) != exact.end()) {
        return true;
    }
    const auto& prefixes = always_allowed_path_prefixes();
    for (const auto& p : prefixes) {
        if (path.size() >= p.size() && path.compare(0, p.size(), p) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace sdcpp
