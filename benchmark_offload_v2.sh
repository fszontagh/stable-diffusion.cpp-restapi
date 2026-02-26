#!/bin/bash
# Benchmark script v2 - Fixed timing extraction
API_URL="http://127.0.0.1:8077"
RESULTS_FILE="/data/sdcpp-restapi/benchmark_results/offload_benchmark_$(date +%Y%m%d_%H%M%S).json"
mkdir -p /data/sdcpp-restapi/benchmark_results

# Test parameters
TEST_PROMPT="a cat"
TEST_WIDTH=1024
TEST_HEIGHT=1024
TEST_STEPS=9
TEST_SEED=42

# Models
DIFFUSION_MODEL="z_image_turbo-Q8_0.gguf"
VAE_MODEL="ae_q8_0.gguf"
LLM_MODEL="Qwen3-4b-Z-Engineer-V2-Q8_0.gguf"

echo "Starting benchmark at $(date)"
echo "Results will be saved to: $RESULTS_FILE"
echo '{"benchmark_start": "'$(date -Iseconds)'", "tests": [' > "$RESULTS_FILE"
first_test=true

run_test() {
    local test_name="$1"
    local offload_mode="$2"
    local weight_type="$3"
    local reload_cond="$4"
    local reload_diff="$5"

    echo ""
    echo "========================================"
    echo "Test: $test_name"
    echo "  offload_mode: $offload_mode, weight_type: $weight_type"
    echo "  reload_cond_stage: $reload_cond, reload_diffusion: $reload_diff"
    echo "========================================"

    # Unload current model
    curl -s -X POST "$API_URL/models/unload" > /dev/null 2>&1
    sleep 2

    gpu_mem_before=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')

    # Load model
    load_start=$(date +%s.%N)
    load_result=$(curl -s -X POST "$API_URL/models/load" \
        -H "Content-Type: application/json" \
        -d '{
            "model_name": "'"$DIFFUSION_MODEL"'",
            "model_type": "diffusion",
            "vae": "'"$VAE_MODEL"'",
            "llm": "'"$LLM_MODEL"'",
            "options": {
                "weight_type": "'"$weight_type"'",
                "flash_attn": true,
                "vae_decode_only": true,
                "offload_mode": "'"$offload_mode"'",
                "offload_cond_stage": true,
                "reload_cond_stage": '"$reload_cond"',
                "reload_diffusion": '"$reload_diff"',
                "log_offload_events": true
            }
        }')
    load_end=$(date +%s.%N)
    load_time=$(echo "$load_end - $load_start" | bc)

    load_success=$(echo "$load_result" | jq -r '.success // false')
    if [ "$load_success" != "true" ]; then
        echo "LOAD FAILED: $(echo "$load_result" | jq -r '.message // .error // "unknown"')"
        if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
        first_test=false
        echo '{"test_name":"'"$test_name"'","status":"load_failed","error":"'"$(echo "$load_result" | jq -r '.message // .error' | tr '"' "'")"'"}' >> "$RESULTS_FILE"
        return 1
    fi

    sleep 2
    gpu_mem_after_load=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')
    echo "Loaded in ${load_time}s, GPU: ${gpu_mem_after_load} MiB"

    # Run generation
    gen_result=$(curl -s -X POST "$API_URL/txt2img" \
        -H "Content-Type: application/json" \
        -d '{"prompt":"'"$TEST_PROMPT"'","width":'"$TEST_WIDTH"',"height":'"$TEST_HEIGHT"',"steps":'"$TEST_STEPS"',"seed":'"$TEST_SEED"',"cfg_scale":1.0,"sampler":"euler","scheduler":"smoothstep"}')

    job_id=$(echo "$gen_result" | jq -r '.job_id // "none"')
    if [ "$job_id" = "none" ]; then
        echo "GEN SUBMIT FAILED: $gen_result"
        if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
        first_test=false
        echo '{"test_name":"'"$test_name"'","status":"gen_submit_failed","load_time_s":'"$load_time"',"gpu_mem_after_load_mib":'"$gpu_mem_after_load"'}' >> "$RESULTS_FILE"
        return 1
    fi

    echo "Job submitted: $job_id"

    # Wait for completion
    max_wait=180
    waited=0
    while [ $waited -lt $max_wait ]; do
        job_data=$(curl -s "$API_URL/queue?job_id=$job_id")
        job_status=$(echo "$job_data" | jq -r '.items[0].status // "unknown"')
        if [ "$job_status" = "completed" ] || [ "$job_status" = "failed" ]; then
            break
        fi
        sleep 1
        waited=$((waited + 1))
        if [ $((waited % 10)) -eq 0 ]; then echo "  Waiting... ${waited}s"; fi
    done

    # Extract timing
    started_at=$(echo "$job_data" | jq -r '.items[0].started_at // ""')
    completed_at=$(echo "$job_data" | jq -r '.items[0].completed_at // ""')

    if [ -n "$started_at" ] && [ -n "$completed_at" ]; then
        start_epoch=$(date -d "$started_at" +%s 2>/dev/null || echo "0")
        end_epoch=$(date -d "$completed_at" +%s 2>/dev/null || echo "0")
        gen_time=$((end_epoch - start_epoch))
    else
        gen_time=0
    fi

    gpu_mem_after_gen=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')

    echo "Status: $job_status, Gen time: ${gen_time}s, GPU after: ${gpu_mem_after_gen} MiB"

    # Record result
    if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
    first_test=false
    echo '{
        "test_name":"'"$test_name"'",
        "offload_mode":"'"$offload_mode"'",
        "weight_type":"'"$weight_type"'",
        "reload_cond_stage":'"$reload_cond"',
        "reload_diffusion":'"$reload_diff"',
        "status":"'"$job_status"'",
        "load_time_s":'"$load_time"',
        "generation_time_s":'"$gen_time"',
        "gpu_mem_before_mib":'"$gpu_mem_before"',
        "gpu_mem_after_load_mib":'"$gpu_mem_after_load"',
        "gpu_mem_after_gen_mib":'"$gpu_mem_after_gen"'
    }' >> "$RESULTS_FILE"
}

# Run tests
run_test "no_offload_q8" "none" "q8_0" "true" "true"
run_test "cond_only_reload_both" "cond_only" "q8_0" "true" "true"
run_test "cond_only_no_reload" "cond_only" "q8_0" "false" "false"
run_test "cond_only_reload_cond" "cond_only" "q8_0" "true" "false"
run_test "cond_only_reload_diff" "cond_only" "q8_0" "false" "true"
run_test "cond_diffusion_reload_both" "cond_diffusion" "q8_0" "true" "true"
run_test "cond_diffusion_no_reload" "cond_diffusion" "q8_0" "false" "false"
run_test "aggressive_reload_both" "aggressive" "q8_0" "true" "true"
run_test "aggressive_no_reload" "aggressive" "q8_0" "false" "false"

# Close JSON
echo '
], "benchmark_end": "'$(date -Iseconds)'"}' >> "$RESULTS_FILE"

echo ""
echo "========================================"
echo "BENCHMARK COMPLETE"
echo "========================================"
echo ""
jq -r '.tests[] | "\(.test_name): \(.status) | load=\(.load_time_s | tostring | .[0:5])s | gen=\(.generation_time_s)s | gpu_load=\(.gpu_mem_after_load_mib)M | gpu_gen=\(.gpu_mem_after_gen_mib)M"' "$RESULTS_FILE"
