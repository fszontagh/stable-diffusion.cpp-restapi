#!/bin/bash

# Dynamic VRAM Offloading Benchmark Script
# Tests various configurations with Z-Image Q8 model

API_URL="http://localhost:8077"
OUTPUT_DIR="/data/sdcpp-restapi/benchmark_results"
IMAGES_DIR="$OUTPUT_DIR/images"
RESULTS_FILE="$OUTPUT_DIR/benchmark_results.md"

# Model configuration
MODEL_NAME="z_image_turbo-Q8_0.gguf"
VAE_NAME="ae_q8_0.gguf"
LLM_NAME="Qwen3-4b-Z-Engineer-V2-Q8_0.gguf"

# Generation params
PROMPT="a cat"
WIDTH=1024
HEIGHT=1024
STEPS=9
SEED=42

# Track test number
TEST_NUM=0

# Results array
declare -a RESULTS

log() {
    echo "[$(date '+%H:%M:%S')] $1"
}

wait_for_model_loaded() {
    log "Waiting for model to load..."
    while true; do
        HEALTH=$(curl -s "$API_URL/health")
        LOADED=$(echo "$HEALTH" | jq -r '.model_loaded')
        LOADING=$(echo "$HEALTH" | jq -r '.model_loading')
        ERROR=$(echo "$HEALTH" | jq -r '.last_error')

        if [ "$ERROR" != "null" ] && [ -n "$ERROR" ]; then
            log "Load error: $ERROR"
            return 1
        fi

        if [ "$LOADED" = "true" ] && [ "$LOADING" = "false" ]; then
            log "Model loaded successfully"
            return 0
        fi

        sleep 1
    done
}

wait_for_job() {
    local JOB_ID=$1
    local MAX_WAIT=600
    local WAITED=0

    log "Waiting for job $JOB_ID..."
    while [ $WAITED -lt $MAX_WAIT ]; do
        STATUS=$(curl -s "$API_URL/queue/$JOB_ID" | jq -r '.status')

        if [ "$STATUS" = "completed" ]; then
            return 0
        elif [ "$STATUS" = "failed" ]; then
            return 1
        fi

        sleep 1
        WAITED=$((WAITED + 1))
    done

    log "Job timed out"
    return 2
}

wait_for_model_unloaded() {
    log "Waiting for model to unload..."
    local MAX_WAIT=60
    local WAITED=0
    while [ $WAITED -lt $MAX_WAIT ]; do
        HEALTH=$(curl -s "$API_URL/health")
        LOADED=$(echo "$HEALTH" | jq -r '.model_loaded')
        LOADING=$(echo "$HEALTH" | jq -r '.model_loading')

        if [ "$LOADED" = "false" ] && [ "$LOADING" = "false" ]; then
            log "Model unloaded successfully"
            return 0
        fi

        sleep 1
        WAITED=$((WAITED + 1))
    done

    log "Timeout waiting for model to unload"
    return 1
}

unload_model() {
    log "Unloading model..."
    local RESPONSE=$(curl -s -X POST "$API_URL/models/unload")
    log "Unload response: $RESPONSE"
    wait_for_model_unloaded
    return $?
}

load_model() {
    local OFFLOAD_MODE=$1
    local VAE_TILING=$2
    local KEEP_CLIP_CPU=$3
    local KEEP_VAE_CPU=$4

    log "Loading model with offload_mode=$OFFLOAD_MODE, vae_tiling=$VAE_TILING, keep_clip_cpu=$KEEP_CLIP_CPU, keep_vae_cpu=$KEEP_VAE_CPU"

    local OPTIONS=$(cat <<EOF
{
    "model_name": "$MODEL_NAME",
    "model_type": "diffusion",
    "vae": "$VAE_NAME",
    "llm": "$LLM_NAME",
    "options": {
        "weight_type": "q8_0",
        "flash_attn": true,
        "vae_decode_only": true,
        "vae_conv_direct": true,
        "rng_type": "cuda",
        "lora_apply_mode": "runtime",
        "offload_mode": "$OFFLOAD_MODE",
        "vae_tiling": $VAE_TILING,
        "keep_clip_on_cpu": $KEEP_CLIP_CPU,
        "keep_vae_on_cpu": $KEEP_VAE_CPU
    }
}
EOF
)

    local RESPONSE=$(curl -s -X POST "$API_URL/models/load" \
        -H "Content-Type: application/json" \
        -d "$OPTIONS")
    log "Load response: $RESPONSE"

    wait_for_model_loaded
    return $?
}

run_generation() {
    local TEST_NAME=$1
    local EXTRA_PARAMS=$2

    TEST_NUM=$((TEST_NUM + 1))
    local IMAGE_FILE="$IMAGES_DIR/test_${TEST_NUM}_$(echo $TEST_NAME | tr ' ' '_').png"

    log "Running test $TEST_NUM: $TEST_NAME"

    local GEN_PARAMS=$(cat <<EOF
{
    "prompt": "$PROMPT",
    "width": $WIDTH,
    "height": $HEIGHT,
    "steps": $STEPS,
    "seed": $SEED,
    "sampler": "euler",
    "scheduler": "smoothstep",
    "cfg_scale": 1.0,
    "distilled_guidance": 3.5
    $EXTRA_PARAMS
}
EOF
)

    local START_TIME=$(date +%s.%N)

    # Submit job
    local RESPONSE=$(curl -s -X POST "$API_URL/txt2img" \
        -H "Content-Type: application/json" \
        -d "$GEN_PARAMS")

    local JOB_ID=$(echo "$RESPONSE" | jq -r '.job_id')
    local ERROR=$(echo "$RESPONSE" | jq -r '.error')

    if [ "$JOB_ID" = "null" ] || [ -z "$JOB_ID" ]; then
        log "Failed to submit job: $ERROR"
        RESULTS+=("| $TEST_NUM | $TEST_NAME | SUBMIT_FAILED | - | $ERROR |")
        return 1
    fi

    # Wait for completion
    if wait_for_job "$JOB_ID"; then
        local END_TIME=$(date +%s.%N)
        local DURATION=$(echo "$END_TIME - $START_TIME" | bc)

        # Get job details
        local JOB_DETAILS=$(curl -s "$API_URL/queue/$JOB_ID")
        local JOB_DURATION=$(echo "$JOB_DETAILS" | jq -r '.duration_ms // 0')
        JOB_DURATION=$(echo "scale=2; $JOB_DURATION / 1000" | bc)

        # Download image
        local OUTPUT_URL=$(echo "$JOB_DETAILS" | jq -r '.output_urls[0]')
        if [ "$OUTPUT_URL" != "null" ] && [ -n "$OUTPUT_URL" ]; then
            curl -s "$API_URL$OUTPUT_URL" -o "$IMAGE_FILE"
            log "Saved image to $IMAGE_FILE"

            # Check for black image using ImageMagick
            if command -v identify &> /dev/null; then
                local MEAN=$(identify -verbose "$IMAGE_FILE" 2>/dev/null | grep "mean:" | head -1 | awk '{print $2}')
                if [ -n "$MEAN" ]; then
                    # If mean pixel value is very low, it's likely a black image
                    local MEAN_INT=${MEAN%.*}
                    if [ "$MEAN_INT" -lt 5 ] 2>/dev/null; then
                        log "WARNING: Test $TEST_NUM produced BLACK IMAGE (mean=$MEAN)"
                        RESULTS+=("| $TEST_NUM | $TEST_NAME | BLACK_IMAGE | ${JOB_DURATION}s | Image is mostly black |")
                        return 1
                    fi
                fi
            fi
        fi

        log "Test $TEST_NUM completed in ${JOB_DURATION}s"
        RESULTS+=("| $TEST_NUM | $TEST_NAME | SUCCESS | ${JOB_DURATION}s | - |")
        return 0
    else
        # Check for OOM or other errors
        local JOB_DETAILS=$(curl -s "$API_URL/queue/$JOB_ID")
        local JOB_ERROR=$(echo "$JOB_DETAILS" | jq -r '.error // "Unknown error"')

        if echo "$JOB_ERROR" | grep -qi "out of memory\|OOM\|cudaMalloc"; then
            log "Test $TEST_NUM failed with CUDA OOM"
            RESULTS+=("| $TEST_NUM | $TEST_NAME | CUDA_OOM | - | Memory exhausted |")
        else
            log "Test $TEST_NUM failed: $JOB_ERROR"
            RESULTS+=("| $TEST_NUM | $TEST_NAME | FAILED | - | $JOB_ERROR |")
        fi
        return 1
    fi
}

get_model_sizes() {
    # Get model file sizes
    local MODELS_INFO=$(curl -s "$API_URL/models")

    local MODEL_SIZE=$(echo "$MODELS_INFO" | jq -r ".diffusion_models[] | select(.name == \"$MODEL_NAME\") | .size_bytes")
    local VAE_SIZE=$(echo "$MODELS_INFO" | jq -r ".vae[] | select(.name == \"$VAE_NAME\") | .size_bytes")
    local LLM_SIZE=$(echo "$MODELS_INFO" | jq -r ".llm[] | select(.name == \"$LLM_NAME\") | .size_bytes")

    # Convert to human readable
    MODEL_SIZE_HR=$(numfmt --to=iec-i --suffix=B $MODEL_SIZE 2>/dev/null || echo "$MODEL_SIZE bytes")
    VAE_SIZE_HR=$(numfmt --to=iec-i --suffix=B $VAE_SIZE 2>/dev/null || echo "$VAE_SIZE bytes")
    LLM_SIZE_HR=$(numfmt --to=iec-i --suffix=B $LLM_SIZE 2>/dev/null || echo "$LLM_SIZE bytes")

    echo "| Diffusion Model | $MODEL_NAME | $MODEL_SIZE_HR |"
    echo "| VAE | $VAE_NAME | $VAE_SIZE_HR |"
    echo "| LLM (cond_stage) | $LLM_NAME | $LLM_SIZE_HR |"
}

generate_report() {
    log "Generating report..."

    cat > "$RESULTS_FILE" << 'HEADER'
# Dynamic VRAM Offloading Benchmark Results

## Test Environment

- **GPU**: NVIDIA (12GB VRAM)
- **Resolution**: 1024x1024
- **Prompt**: "a cat"
- **Steps**: 9
- **Sampler**: euler
- **Scheduler**: smoothstep
- **Seed**: 42 (fixed for reproducibility)

## Model Configuration

| Component | Model Name | Size |
|-----------|------------|------|
HEADER

    get_model_sizes >> "$RESULTS_FILE"

    cat >> "$RESULTS_FILE" << 'MIDDLE'

## Test Matrix

The following configurations were tested:

| # | Test Configuration | Result | Duration | Notes |
|---|-------------------|--------|----------|-------|
MIDDLE

    for result in "${RESULTS[@]}"; do
        echo "$result" >> "$RESULTS_FILE"
    done

    cat >> "$RESULTS_FILE" << 'FOOTER'

## Configuration Legend

- **offload_mode**:
  - `none` - No dynamic offloading
  - `cond_only` - Offload cond_stage (LLM) after conditioning
- **vae_tiling**: Enable tiled VAE decode to reduce VRAM usage
- **keep_clip_cpu**: Keep CLIP/LLM on CPU (slower conditioning)
- **keep_vae_cpu**: Keep VAE on CPU (slower decode)

## Analysis

### Key Findings

1. **Without Dynamic Offloading + No VAE Tiling**: Expected to OOM at 1024x1024 with Q8 models
2. **With Dynamic Offloading (cond_only)**: Should work by offloading ~4GB LLM after conditioning
3. **VAE Tiling**: Reduces peak VRAM during decode by processing in tiles

### Recommendations

- For 12GB GPU with large Q8 models: Use `offload_mode=cond_only` + `vae_tiling=true`
- For maximum speed (if fits): Use `offload_mode=none` with smaller resolution
- Trade-off: Dynamic offloading adds ~1.3s reload time per generation

---
*Generated on: $(date)*
FOOTER

    log "Report saved to $RESULTS_FILE"
}

# Main test execution
main() {
    log "Starting Dynamic VRAM Offloading Benchmark"
    log "=========================================="

    # Test 1: No offloading, no tiling (baseline - expect OOM)
    unload_model
    if load_model "none" "false" "false" "false"; then
        run_generation "Baseline (no offload, no tiling)" ""
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Baseline (no offload, no tiling) | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 2: No offloading, with VAE tiling
    unload_model
    if load_model "none" "true" "false" "false"; then
        run_generation "No offload + VAE tiling" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | No offload + VAE tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 3: cond_only offloading, no tiling
    unload_model
    if load_model "cond_only" "false" "false" "false"; then
        run_generation "Dynamic offload (cond_only), no tiling" ""
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Dynamic offload (cond_only), no tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 4: cond_only offloading + VAE tiling
    unload_model
    if load_model "cond_only" "true" "false" "false"; then
        run_generation "Dynamic offload (cond_only) + VAE tiling" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Dynamic offload (cond_only) + VAE tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 5: keep_clip_on_cpu (traditional CPU offload)
    unload_model
    if load_model "none" "true" "true" "false"; then
        run_generation "Keep CLIP on CPU + VAE tiling" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Keep CLIP on CPU + VAE tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 6: keep_clip_on_cpu + keep_vae_on_cpu
    unload_model
    if load_model "none" "true" "true" "true"; then
        run_generation "Keep CLIP+VAE on CPU + VAE tiling" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Keep CLIP+VAE on CPU + VAE tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 7: Dynamic offload + keep_vae_on_cpu (hybrid)
    unload_model
    if load_model "cond_only" "true" "false" "true"; then
        run_generation "Dynamic offload + VAE on CPU + tiling" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Dynamic offload + VAE on CPU + tiling | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Test 8: Run test 4 again (dynamic offload + tiling) to verify consistency
    unload_model
    if load_model "cond_only" "true" "false" "false"; then
        run_generation "Dynamic offload + VAE tiling (repeat)" ", \"vae_tiling\": true"
    else
        RESULTS+=("| $((TEST_NUM + 1)) | Dynamic offload + VAE tiling (repeat) | LOAD_FAILED | - | Could not load model |")
        TEST_NUM=$((TEST_NUM + 1))
    fi

    # Generate final report
    generate_report

    log "=========================================="
    log "Benchmark complete! Results saved to $RESULTS_FILE"
}

main "$@"
