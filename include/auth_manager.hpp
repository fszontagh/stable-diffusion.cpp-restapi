#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <chrono>
#include <optional>
#include <vector>

namespace sdcpp {

// Forward declaration
struct Config;

/**
 * AuthManager — backend authentication service for sdcpp-restapi.
 *
 * Single credential pair (username + password) configured server-side.
 * Token issuance/verification is in-memory; restarts invalidate all tokens.
 *
 * Source priority for credentials:
 *   1. config.json (auth.username / auth.password)
 *   2. environment variables (SDCPP_AUTH_USERNAME / SDCPP_AUTH_PASSWORD)
 *
 * If auth.enabled == true and neither source provides credentials,
 * the constructor throws std::runtime_error and the server refuses to start.
 *
 * Three auth modes share these credentials:
 *   - Bearer token: REST API + MCP (Authorization: Bearer <token>)
 *   - Query token: WebSocket (?token=<token>)
 *   - HTTP Basic: design hooks reserved for future WebDAV use (NOT YET WIRED)
 */
class AuthManager {
public:
    /**
     * Construct from server configuration.
     * Loads credentials from config or env vars (config takes priority).
     * @throws std::runtime_error if enabled and no credentials available.
     */
    explicit AuthManager(const Config& config);

    /** True iff the server is enforcing authentication. */
    bool enabled() const { return enabled_; }

    /**
     * Constant-time credential check.
     * Always sleeps ~200ms on mismatch to mitigate timing/login probing.
     */
    bool verify_credentials(const std::string& user, const std::string& pass) const;

    /**
     * Issue a fresh opaque token for the given username.
     * Token is a 32-byte random base64url-encoded string (~43 chars, no padding).
     * Stored in-memory with absolute expiry = now + token_ttl_minutes.
     */
    std::string issue_token(const std::string& username);

    /**
     * Verify a previously-issued token.
     * Returns the username it was issued for if still valid; nullopt otherwise.
     * Side-effect: lazily evicts the entry if expired.
     */
    std::optional<std::string> verify_token(const std::string& token) const;

    /** Revoke a token explicitly (logout). No-op if unknown. */
    void revoke_token(const std::string& token);

    /** Token TTL in seconds (used for the login response's expires_at). */
    int token_ttl_seconds() const { return token_ttl_minutes_ * 60; }

    /**
     * The set of path prefixes that are always allowed without authentication.
     * Returned by reference to avoid per-request copies.
     */
    static const std::unordered_set<std::string>& always_allowed_exact_paths();
    static const std::vector<std::string>& always_allowed_path_prefixes();

    /**
     * True iff the given request path should bypass auth entirely.
     * Matches against exact paths and known path prefixes.
     */
    static bool is_always_allowed(const std::string& path);

private:
    /**
     * Constant-time string comparison.
     * Always loops over the longer of the two inputs and XORs char-by-char,
     * never short-circuits, never branches on intermediate match state.
     */
    static bool secure_compare(const std::string& a, const std::string& b);

    /** Generate a 32-byte random token, base64url-encoded (no padding). */
    static std::string generate_token();

    /** Drop all expired tokens from the map. Caller must hold an exclusive lock. */
    void cleanup_expired_locked() const;

    bool enabled_ = true;
    std::string username_;
    std::string password_;  // plaintext for v1; bcrypt is a follow-up
    int token_ttl_minutes_ = 1440;

    struct TokenEntry {
        std::string username;
        std::chrono::steady_clock::time_point expires_at;
    };

    // Mutable so const verify_token() can lazily evict expired entries.
    mutable std::shared_mutex tokens_mutex_;
    mutable std::unordered_map<std::string, TokenEntry> tokens_;
};

} // namespace sdcpp
