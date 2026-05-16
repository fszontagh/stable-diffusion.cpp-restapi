#include "url_utils.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace sdcpp {

namespace {

// Parse an IPv4 dotted-quad to host-order uint32. Returns false on
// malformed input (no exception thrown — bad config is dropped, not
// fatal).
bool parse_ipv4(const std::string& s, uint32_t& out) {
    in_addr addr{};
    if (inet_pton(AF_INET, s.c_str(), &addr) != 1) return false;
    out = ntohl(addr.s_addr);
    return true;
}

// Match `ip` against a single trusted-list entry. Supports exact
// literal match for any address family and IPv4 CIDR (`/N`).
bool entry_matches(const std::string& ip, const std::string& entry) {
    if (entry.empty()) return false;

    auto slash = entry.find('/');
    if (slash == std::string::npos) {
        // Exact literal match. Trim is the caller's job.
        return ip == entry;
    }

    // IPv4 CIDR. We don't support IPv6 CIDR yet — IPv6 callers should
    // use exact literals.
    std::string net  = entry.substr(0, slash);
    std::string bits = entry.substr(slash + 1);
    if (bits.empty()) return false;

    int prefix = 0;
    try {
        prefix = std::stoi(bits);
    } catch (...) {
        return false;
    }
    if (prefix < 0 || prefix > 32) return false;

    uint32_t net_addr = 0, ip_addr = 0;
    if (!parse_ipv4(net, net_addr)) return false;
    if (!parse_ipv4(ip, ip_addr)) return false;

    if (prefix == 0) return true;  // 0.0.0.0/0 matches everything
    uint32_t mask = (prefix == 32) ? 0xFFFFFFFFu
                                   : (~uint32_t{0}) << (32 - prefix);
    return (net_addr & mask) == (ip_addr & mask);
}

// Strip leading/trailing ASCII whitespace.
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Take the first comma-separated value of an X-Forwarded-* header
// (those are spec'd as comma-separated lists; the leftmost value is
// the originating one).
std::string first_csv(const std::string& v) {
    auto comma = v.find(',');
    return trim(comma == std::string::npos ? v : v.substr(0, comma));
}

} // namespace

bool ip_in_trusted_list(const std::string& ip,
                        const std::vector<std::string>& trusted) {
    if (ip.empty()) return false;
    for (const auto& raw : trusted) {
        if (entry_matches(ip, trim(raw))) return true;
    }
    return false;
}

std::string compute_base_url(const httplib::Request& req,
                             const std::vector<std::string>& trusted_proxies) {
    bool trust_forwarded = ip_in_trusted_list(req.remote_addr, trusted_proxies);

    std::string scheme = "http";
    if (trust_forwarded) {
        std::string xfp = first_csv(req.get_header_value("X-Forwarded-Proto"));
        if (!xfp.empty()) {
            std::transform(xfp.begin(), xfp.end(), xfp.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (xfp == "https" || xfp == "http") scheme = xfp;
        }
    }

    std::string host;
    if (trust_forwarded) {
        host = first_csv(req.get_header_value("X-Forwarded-Host"));
    }
    if (host.empty()) {
        host = req.get_header_value("Host");
    }
    if (host.empty()) {
        // Last-resort placeholder. Real clients always send Host.
        host = "localhost";
    }

    return scheme + "://" + host;
}

nlohmann::json build_output_urls(const std::vector<std::string>& outputs,
                                 const std::string& base_url) {
    nlohmann::json urls = nlohmann::json::array();
    for (const auto& rel : outputs) {
        if (rel.empty()) continue;
        // The static handler is mounted at /output/<path>. Outputs are
        // stored as relative paths like `<job_id>/output_0.png`.
        urls.push_back(base_url + "/output/" + rel);
    }
    return urls;
}

} // namespace sdcpp
