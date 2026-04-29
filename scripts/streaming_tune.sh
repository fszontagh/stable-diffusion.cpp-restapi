#!/usr/bin/env bash
# streaming_tune.sh — find the best layer streaming config for your model + GPU.
#
# Usage:
#   scripts/streaming_tune.sh [--quick] [--prefetch "1,4,8,16"] [--steps N]
#                             [--width W] [--height H] [--api URL]
#                             [--model NAME] [--vae NAME] [--llm NAME]
#                             [--apply]
#
# Modes:
#   default — runs `txt2img` once per prefetch value, prints timings, recommends best.
#   --quick — only prints VRAM/model heuristic recommendation, no generation.
#   --apply — writes the best prefetch into the user's settings (use with caution).
#
# Requirements: a running sdcpp-restapi service, jq, curl, nvidia-smi.

set -euo pipefail

API_URL="http://127.0.0.1:8077"
PREFETCH_LIST="1,4,8,12,16"
STEPS=8
WIDTH=688
HEIGHT=1024
PROMPT="a cat sitting on a chair, photorealistic"
SEED=42
MODEL=""
VAE=""
LLM=""
QUICK=0
APPLY=0

# ── arg parsing ───────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)   QUICK=1; shift ;;
        --apply)   APPLY=1; shift ;;
        --prefetch) PREFETCH_LIST="$2"; shift 2 ;;
        --steps)   STEPS="$2"; shift 2 ;;
        --width)   WIDTH="$2"; shift 2 ;;
        --height)  HEIGHT="$2"; shift 2 ;;
        --api)     API_URL="$2"; shift 2 ;;
        --model)   MODEL="$2"; shift 2 ;;
        --vae)     VAE="$2"; shift 2 ;;
        --llm)     LLM="$2"; shift 2 ;;
        --prompt)  PROMPT="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

for cmd in jq curl nvidia-smi; do
    command -v "$cmd" >/dev/null || { echo "missing: $cmd" >&2; exit 1; }
done

# ── GPU + model probe ─────────────────────────────────────────────
gpu_total_mib=$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits | head -1 | tr -d ' ')
gpu_free_mib=$(nvidia-smi --query-gpu=memory.free  --format=csv,noheader,nounits | head -1 | tr -d ' ')

echo "─── GPU ────────────────────────────────────"
echo "total VRAM:   ${gpu_total_mib} MiB"
echo "free VRAM:    ${gpu_free_mib} MiB"

# Detect currently loaded model if one is loaded.
loaded=$(curl -fsS "${API_URL}/models" 2>/dev/null | jq -r '.loaded // empty' || true)
if [[ -z "$MODEL" && -n "$loaded" ]]; then
    MODEL=$(echo "$loaded" | jq -r '.model_name // empty')
    VAE=${VAE:-$(echo "$loaded" | jq -r '.vae // empty')}
    LLM=${LLM:-$(echo "$loaded" | jq -r '.llm // empty')}
fi

echo
echo "─── Model ──────────────────────────────────"
echo "diffusion:    ${MODEL:-<not set, pass --model>}"
echo "vae:          ${VAE:-<not set>}"
echo "llm:          ${LLM:-<not set>}"

# Estimate model bytes by stat'ing the diffusion file under known model dirs.
model_bytes=0
if [[ -n "$MODEL" ]]; then
    for dir in /data/SD_MODELS/diffusion_models /data/SD_MODELS/checkpoints; do
        if [[ -f "$dir/$MODEL" ]]; then
            model_bytes=$(stat -c %s "$dir/$MODEL")
            break
        fi
    done
fi
model_mib=$((model_bytes / 1024 / 1024))
echo "model size:   ${model_mib} MiB"

# Heuristic: ~30 layers for DiT models, ~390 MB/layer for Z-Image bf16.
# Above ~8 prefetched, returns diminish — PCIe is already saturated and you're
# just hogging VRAM. Cap at 12 unless you have a huge GPU.
# Reserve ~1.5 GB for compute buffers + active layer + headroom.
if [[ $model_mib -gt 0 ]]; then
    per_layer_mib=$(( model_mib / 30 ))
    [[ $per_layer_mib -lt 1 ]] && per_layer_mib=1
    avail_for_prefetch=$((gpu_total_mib - 1500))
    rec_prefetch=$(( avail_for_prefetch / per_layer_mib ))
    [[ $rec_prefetch -lt 1 ]]  && rec_prefetch=1
    [[ $rec_prefetch -gt 12 ]] && rec_prefetch=12
else
    per_layer_mib=0
    rec_prefetch=8
fi

echo
echo "─── Heuristic recommendation ───────────────"
echo "per-layer:    ~${per_layer_mib} MiB  (assuming 30 layers)"
echo "prefetch:     ${rec_prefetch}  (clamped 1..12)"

if [[ $QUICK -eq 1 ]]; then
    echo
    echo "Quick mode — to apply, set 'streaming_prefetch_layers: ${rec_prefetch}'"
    echo "in your model load options or pass --apply on a real benchmark run."
    exit 0
fi

# ── benchmark loop ────────────────────────────────────────────────
if [[ -z "$MODEL" ]]; then
    echo "ERROR: a model must be loaded or --model must be passed for benchmarking" >&2
    exit 1
fi

IFS=',' read -r -a prefetch_arr <<< "$PREFETCH_LIST"

reload_with_prefetch() {
    local pf="$1"
    local payload
    payload=$(jq -n \
        --arg model "$MODEL" \
        --arg vae "$VAE" \
        --arg llm "$LLM" \
        --argjson pf "$pf" \
        '{model_name: $model, model_type: "diffusion",
          vae: ($vae // ""), llm: ($llm // ""),
          options: {
            offload_mode: "layer_streaming",
            streaming_prefetch_layers: $pf,
            offload_cond_stage: true,
            reload_cond_stage: true,
            free_params_immediately: false,
            flash_attn: true,
            log_offload_events: true
          }}' )
    curl -fsS -X POST "${API_URL}/models/load" \
         -H 'Content-Type: application/json' \
         -d "$payload" >/dev/null
}

run_one_gen() {
    local payload
    payload=$(jq -n \
        --arg prompt "$PROMPT" \
        --argjson seed "$SEED" \
        --argjson steps "$STEPS" \
        --argjson w "$WIDTH" \
        --argjson h "$HEIGHT" \
        '{prompt: $prompt, seed: $seed, steps: $steps,
          width: $w, height: $h, batch_count: 1,
          cfg_scale: 1.0, sampler: "euler", scheduler: "smoothstep"}' )
    local resp
    resp=$(curl -fsS -X POST "${API_URL}/txt2img" \
                -H 'Content-Type: application/json' \
                -d "$payload")
    local job_id
    job_id=$(echo "$resp" | jq -r '.job_id')
    [[ -z "$job_id" || "$job_id" == "null" ]] && return 1

    local waited=0 max=900
    while (( waited < max )); do
        local s
        s=$(curl -fsS "${API_URL}/queue/${job_id}" | jq -r '.status // "unknown"')
        case "$s" in
            completed) echo "$job_id"; return 0 ;;
            failed|cancelled) return 1 ;;
        esac
        sleep 1
        waited=$((waited+1))
    done
    return 1
}

job_duration() {
    local job_id="$1"
    local data started ended
    data=$(curl -fsS "${API_URL}/queue/${job_id}")
    started=$(echo "$data" | jq -r '.started_at // ""')
    ended=$(echo "$data"   | jq -r '.completed_at // ""')
    if [[ -n "$started" && -n "$ended" ]]; then
        local s e
        s=$(date -d "$started" +%s%3N 2>/dev/null || echo 0)
        e=$(date -d "$ended"   +%s%3N 2>/dev/null || echo 0)
        echo $(( e - s ))
    else
        echo 0
    fi
}

echo
echo "─── Benchmark ──────────────────────────────"
echo "prefetch list: ${PREFETCH_LIST}"
echo "test:          ${WIDTH}×${HEIGHT}, ${STEPS} steps, seed=${SEED}"
echo

results_json='[]'
best_pf=0
best_ms=999999999

for pf in "${prefetch_arr[@]}"; do
    pf=$(echo "$pf" | tr -d ' ')
    printf "  prefetch=%-3s  reloading model… " "$pf"
    if ! reload_with_prefetch "$pf"; then
        echo "FAILED to load"
        continue
    fi
    printf "running gen… "
    if ! job_id=$(run_one_gen); then
        echo "GEN FAILED"
        continue
    fi
    ms=$(job_duration "$job_id")
    sec=$(awk "BEGIN { printf \"%.2f\", $ms/1000 }")
    echo "${sec}s"

    results_json=$(echo "$results_json" | jq --argjson pf "$pf" --argjson ms "$ms" '. + [{prefetch: $pf, ms: $ms}]')

    if (( ms > 0 && ms < best_ms )); then
        best_ms=$ms
        best_pf=$pf
    fi
done

echo
echo "─── Result ─────────────────────────────────"
echo "$results_json" | jq -r '.[] | "  prefetch=\(.prefetch | tostring | (. + "   ")[:4]) → \(.ms/1000 | tostring | .[0:5])s"'
if (( best_pf > 0 )); then
    echo
    echo "  best prefetch: ${best_pf}  (took $(awk "BEGIN { printf \"%.2f\", $best_ms/1000 }")s)"
fi

# ── optional: apply ──────────────────────────────────────────────
if [[ $APPLY -eq 1 && $best_pf -gt 0 ]]; then
    settings=/mnt/http/static/sdapi/user_settings.json
    if [[ -f "$settings" ]]; then
        tmp=$(mktemp)
        jq --argjson pf "$best_pf" '.streaming_prefetch_layers = $pf' "$settings" > "$tmp" && mv "$tmp" "$settings"
        echo "  written streaming_prefetch_layers=${best_pf} to ${settings}"
    else
        echo "  --apply requested but ${settings} not found; skipping write"
    fi
fi
