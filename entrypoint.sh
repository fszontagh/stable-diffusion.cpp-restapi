#!/usr/bin/env bash
#
# Container entrypoint for sdcpp-restapi on RunPod (or any GPU host with a
# persistent volume mounted at $SDCPP_VOLUME_ROOT — defaults to /workspace).
#
# Responsibilities:
#   1. Make sure the volume layout exists (model dirs + output dir).
#   2. Decide what to do if no models are found yet (see preflight_checks).
#   3. Exec the server with the right config.
#

set -euo pipefail

VOLUME_ROOT="${SDCPP_VOLUME_ROOT:-/workspace}"
CONFIG_PATH="${SDCPP_CONFIG:-/etc/sdcpp-restapi/config.json}"

MODEL_DIRS=(
    "${VOLUME_ROOT}/models/checkpoints"
    "${VOLUME_ROOT}/models/diffusion_models"
    "${VOLUME_ROOT}/models/vae"
    "${VOLUME_ROOT}/models/loras"
    "${VOLUME_ROOT}/models/clip"
    "${VOLUME_ROOT}/models/t5"
    "${VOLUME_ROOT}/models/embeddings"
    "${VOLUME_ROOT}/models/controlnet"
    "${VOLUME_ROOT}/models/llm"
    "${VOLUME_ROOT}/models/esrgan"
    "${VOLUME_ROOT}/models/taesd"
)

ensure_volume_layout() {
    echo "[entrypoint] ensuring volume layout under ${VOLUME_ROOT}"
    for d in "${MODEL_DIRS[@]}" "${VOLUME_ROOT}/output"; do
        mkdir -p "$d"
    done
}

# Run the model bootstrap downloader if the user has selected one or more
# architectures via SDCPP_BOOTSTRAP_ARCHITECTURES. The script is idempotent
# (skips files already on disk) and resumable, so it's safe to run on every
# pod start. If the env var is unset, this is a no-op.
bootstrap_models() {
    if [[ -z "${SDCPP_BOOTSTRAP_ARCHITECTURES:-}" ]]; then
        return 0
    fi
    echo "[entrypoint] bootstrapping architectures: ${SDCPP_BOOTSTRAP_ARCHITECTURES}"
    /opt/sdcpp/bin/bootstrap-models.sh
}

# Start the SSH daemon in the background so users can `ssh root@<pod>` for
# shell access. RunPod (and similar platforms) auto-inject the account-level
# pubkey to /root/.ssh/authorized_keys on pod start. We just need to ensure
# host keys exist and that sshd is running. tini (PID 1) reaps the child
# when the container shuts down. No-op if openssh isn't installed.
start_sshd() {
    if [[ ! -x /usr/sbin/sshd ]]; then
        return 0
    fi
    # Generate any missing host keys (idempotent — ssh-keygen -A skips existing)
    ssh-keygen -A >/dev/null 2>&1 || true
    # Make authorized_keys readable by root only if it exists
    if [[ -f /root/.ssh/authorized_keys ]]; then
        chmod 600 /root/.ssh/authorized_keys
    fi
    echo "[entrypoint] starting sshd in background"
    /usr/sbin/sshd -D &
}

main() {
    ensure_volume_layout
    start_sshd
    bootstrap_models

    echo "[entrypoint] starting sdcpp-restapi"
    echo "[entrypoint]   config:       ${CONFIG_PATH}"
    echo "[entrypoint]   volume root:  ${VOLUME_ROOT}"
    echo "[entrypoint]   docs path:    ${SDCPP_DOCS_PATH:-<unset>}"

    exec /opt/sdcpp/bin/sdcpp-restapi --config "${CONFIG_PATH}" "$@"
}

main "$@"
