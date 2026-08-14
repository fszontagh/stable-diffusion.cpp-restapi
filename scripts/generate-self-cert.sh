#!/usr/bin/env bash
# Generate a self-signed TLS certificate suitable for local sdcpp-restapi
# use. Chrome/Firefox will still warn (self-signed is not a trusted CA)
# but the origin will count as "secure" once you accept the warning, so
# desktop notifications, WebCrypto, service workers, etc. become
# available.
#
# Usage:
#   scripts/generate-self-cert.sh <hostname-or-ip> [output-dir]
#
# Examples:
#   scripts/generate-self-cert.sh myhost                # -> ./certs/
#   scripts/generate-self-cert.sh myhost.lan  /etc/sdcpp-restapi/
#   scripts/generate-self-cert.sh 192.168.1.10
#
# The generated files:
#   <out>/sdcpp-cert.pem   PEM certificate (public)
#   <out>/sdcpp-key.pem    PEM private key  (mode 600)
#
# Point your config.json at them:
#   "server": {
#     "ssl": {
#       "enabled": true,
#       "cert_path": "/absolute/path/to/sdcpp-cert.pem",
#       "key_path":  "/absolute/path/to/sdcpp-key.pem"
#     }
#   }

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <hostname-or-ip> [output-dir]" >&2
    exit 2
fi

TARGET=$1
OUT_DIR=${2:-./certs}
DAYS=${SDCPP_CERT_DAYS:-825}   # 825d = macOS max for user-trusted self-signed

mkdir -p "$OUT_DIR"

# Build a SAN block so both DNS names and IPs work. Chrome ignores CN
# these days and looks at SAN only.
SAN_LINES=("DNS.1 = localhost")
if [[ "$TARGET" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    SAN_LINES+=("IP.1 = 127.0.0.1")
    SAN_LINES+=("IP.2 = $TARGET")
else
    SAN_LINES+=("DNS.2 = $TARGET")
    SAN_LINES+=("IP.1 = 127.0.0.1")
fi

CONF=$(mktemp)
trap 'rm -f "$CONF"' EXIT

cat > "$CONF" <<EOF
[req]
default_bits       = 2048
prompt             = no
default_md         = sha256
distinguished_name = dn
req_extensions     = req_ext
x509_extensions    = req_ext

[dn]
CN = $TARGET

[req_ext]
subjectAltName = @alt_names
basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[alt_names]
$(printf '%s\n' "${SAN_LINES[@]}")
EOF

CERT="$OUT_DIR/sdcpp-cert.pem"
KEY="$OUT_DIR/sdcpp-key.pem"

openssl req -x509 -new -nodes \
    -newkey rsa:2048 \
    -keyout "$KEY" \
    -out "$CERT" \
    -days "$DAYS" \
    -config "$CONF" \
    -extensions req_ext

chmod 600 "$KEY"

echo
echo "wrote:"
echo "  cert -> $CERT"
echo "  key  -> $KEY"
echo
echo "SAN entries:"
printf '  %s\n' "${SAN_LINES[@]}"
echo
echo "next: set server.ssl in config.json"
echo '  "ssl": {'
echo '    "enabled":   true,'
echo "    \"cert_path\": \"$(readlink -f "$CERT")\","
echo "    \"key_path\":  \"$(readlink -f "$KEY")\""
echo '  }'
echo
echo "To make the browser trust the cert (no warning banner):"
echo "  Brave / Chrome / Chromium (137+):"
echo "    brave://certificate-manager -> Custom -> Trusted Certificates -> Import"
echo "    point at:  $(readlink -f "$CERT")"
echo "    then fully quit and relaunch the browser (pkill brave). The OS-wide"
echo "    trust store (/etc/ssl/certs/) and NSS user DB (~/.pki/nssdb) are"
echo "    IGNORED by Chromium 137+ for TLS. The certificate-manager UI is the"
echo "    only reliable path on Linux."
echo "  Firefox:"
echo "    Settings -> Privacy & Security -> View Certificates -> Authorities -> Import"
echo "  Optional (CLI tools like curl/wget only, does NOT help browsers):"
echo "    sudo cp $CERT /usr/local/share/ca-certificates/sdcpp-<host>.crt"
echo "    sudo update-ca-certificates"
echo
echo "If you skip the import you will get a 'not trusted' click-through warning"
echo "on first visit. Accepting once still gives you a secure context so the"
echo "Notification / WebCrypto / SW APIs work."
