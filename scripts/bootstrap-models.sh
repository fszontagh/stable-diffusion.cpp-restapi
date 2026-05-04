#!/usr/bin/env bash
#
# bootstrap-models.sh — download model files for one or more preselected
# architectures into a RunPod-style network volume.
#
# Reads a registry JSON (default: /opt/sdcpp/data/bootstrap_models.json)
# whose top-level keys are architecture names. Each architecture has a list
# of files: { name, dest, url, size_mb, auth }.
#
# Behaviour:
#   - Idempotent: skips any file that already exists with non-zero size.
#   - Resumable: uses curl --continue-at - so partial downloads (from a
#     killed/evicted pod) resume instead of restarting from byte 0.
#   - Auth: if a file specifies "auth": "hf" and HF_TOKEN is set, sends
#     it as a Bearer header. Otherwise no auth headers are added.
#   - Fails fast: any URL still containing "TODO_FILL_IN" aborts with a
#     clear error pointing the user at the registry file to fix.
#
# Env vars:
#   SDCPP_BOOTSTRAP_ARCHITECTURES  comma-separated list (e.g. "z-image,flux")
#   SDCPP_VOLUME_ROOT              defaults to /workspace
#   SDCPP_BOOTSTRAP_REGISTRY       defaults to /opt/sdcpp/data/bootstrap_models.json
#   HF_TOKEN                       optional HuggingFace bearer token
#
# Exit codes:
#   0 success (or nothing to do)
#   1 registry missing / malformed / contains TODO placeholders
#   2 unknown architecture requested
#   3 download failed
#

set -euo pipefail

REGISTRY="${SDCPP_BOOTSTRAP_REGISTRY:-/opt/sdcpp/data/bootstrap_models.json}"
VOLUME_ROOT="${SDCPP_VOLUME_ROOT:-/workspace}"
ARCHITECTURES="${SDCPP_BOOTSTRAP_ARCHITECTURES:-}"
HF_TOKEN="${HF_TOKEN:-}"

log()  { printf '[bootstrap] %s\n' "$*"; }
warn() { printf '[bootstrap] WARN: %s\n' "$*" >&2; }
die()  { printf '[bootstrap] ERROR: %s\n' "$*" >&2; exit "${2:-1}"; }

if [[ -z "${ARCHITECTURES}" ]]; then
    log "SDCPP_BOOTSTRAP_ARCHITECTURES not set — nothing to bootstrap."
    exit 0
fi

[[ -f "${REGISTRY}" ]] || die "registry not found: ${REGISTRY}"
command -v jq    >/dev/null || die "jq is required but not installed"
command -v curl  >/dev/null || die "curl is required but not installed"

download_one() {
    local name="$1" dest="$2" url="$3" size_mb="$4" auth="$5"
    local target_dir="${VOLUME_ROOT}/models/${dest}"
    local target_path="${target_dir}/${name}"

    if [[ "${url}" == TODO_FILL_IN* ]]; then
        die "registry has placeholder URL for '${name}' — edit ${REGISTRY} and replace ${url}" 1
    fi

    mkdir -p "${target_dir}"

    if [[ -s "${target_path}" ]]; then
        local actual_mb
        actual_mb=$(( $(stat -c%s "${target_path}") / 1024 / 1024 ))
        log "skip ${name} (already present, ${actual_mb} MB on disk)"
        return 0
    fi

    log "downloading ${name} → ${target_path} (${size_mb} MB expected)"

    local -a curl_args=(
        --fail
        --location
        --continue-at -
        --retry 3
        --retry-delay 5
        --progress-bar
        --output "${target_path}"
    )
    if [[ "${auth}" == "hf" && -n "${HF_TOKEN}" ]]; then
        curl_args+=(--header "Authorization: Bearer ${HF_TOKEN}")
    elif [[ "${auth}" == "hf" && -z "${HF_TOKEN}" ]]; then
        warn "${name} requests HF auth but HF_TOKEN is unset — proceeding unauthenticated"
    fi

    if ! curl "${curl_args[@]}" "${url}"; then
        die "download failed for ${name} (curl exit $?)" 3
    fi

    log "ok ${name} ($(stat -c%s "${target_path}") bytes)"
}

bootstrap_architecture() {
    local arch="$1"
    log "=== architecture: ${arch} ==="

    if ! jq -e --arg a "${arch}" '.[$a]' "${REGISTRY}" >/dev/null; then
        die "architecture '${arch}' not found in ${REGISTRY}" 2
    fi

    local description
    description=$(jq -r --arg a "${arch}" '.[$a].description // ""' "${REGISTRY}")
    [[ -n "${description}" ]] && log "${description}"

    while IFS=$'\t' read -r name dest url size_mb auth; do
        download_one "${name}" "${dest}" "${url}" "${size_mb}" "${auth}"
    done < <(jq -r --arg a "${arch}" '
        .[$a].files[] | [.name, .dest, .url, (.size_mb // 0), (.auth // "")] | @tsv
    ' "${REGISTRY}")
}

IFS=',' read -ra arch_list <<< "${ARCHITECTURES}"
for arch in "${arch_list[@]}"; do
    arch="${arch// /}"   # strip whitespace
    [[ -n "${arch}" ]] && bootstrap_architecture "${arch}"
done

log "all bootstrap downloads complete."
