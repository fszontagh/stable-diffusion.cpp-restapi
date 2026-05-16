#pragma once

#include <string>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * URL / proxy-trust helpers.
 *
 * These exist so that response payloads (e.g. job outputs, MCP tool
 * results) can return absolute URLs that match what the client itself
 * used to reach the server, without leaking any information that an
 * arbitrary client could spoof.
 *
 * Trust model
 * -----------
 * The server reads `Host` from the request to construct URLs. When the
 * server is fronted by a reverse proxy that terminates TLS or rewrites
 * the host (nginx, Caddy, Traefik, kube-ingress), the proxy injects
 * `X-Forwarded-Host` and `X-Forwarded-Proto`. Honoring those blindly is
 * a security hole: any external client could send the same headers and
 * get the API to print URLs of its choosing in responses (open-redirect
 * style).
 *
 * The compromise: forwarded headers are honored *only when the
 * connecting peer is in the configured trusted-proxies list*. If the
 * list is empty (default), forwarded headers are ignored everywhere.
 */

/**
 * True if `ip` matches any entry in `trusted`. Entries may be:
 *   - exact IP literal (`"127.0.0.1"`, `"::1"`, `"10.0.0.5"`)
 *   - IPv4 CIDR (`"10.0.0.0/8"`, `"192.168.1.0/24"`)
 *
 * Unparseable entries are skipped silently — bad config shouldn't crash
 * the server. IPv6 CIDR is not yet supported; pass exact IPv6 literals.
 */
bool ip_in_trusted_list(const std::string& ip,
                        const std::vector<std::string>& trusted);

/**
 * Compute the base URL (`scheme://host[:port]`) the client used to
 * reach the server, for use in response payloads.
 *
 * Honors `X-Forwarded-Proto` / `X-Forwarded-Host` only when
 * `req.remote_addr` is in `trusted_proxies`. Otherwise falls back to the
 * literal `Host` header with `http://`. If even that's missing,
 * `"http://localhost"` is returned as a last-resort placeholder.
 */
std::string compute_base_url(const httplib::Request& req,
                             const std::vector<std::string>& trusted_proxies);

/**
 * Build absolute `/output/<path>` URLs for each entry in `outputs`,
 * using the supplied `base_url` (already computed via
 * `compute_base_url`). Returns a JSON array of strings.
 */
nlohmann::json build_output_urls(const std::vector<std::string>& outputs,
                                 const std::string& base_url);

} // namespace sdcpp
