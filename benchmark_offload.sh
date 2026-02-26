#!/bin/bash
# Benchmark script for Dynamic VRAM Offloading configurations
# Tests different offload_mode and weight_type combinations

API_URL="http://127.0.0.1:8077"
RESULTS_FILE="/data/sdcpp-restapi/benchmark_results/offload_benchmark_$(date +%Y%m%d_%H%M%S).json"
mkdir -p /data/sdcpp-restapi/benchmark_results

# Test prompt - simple, consistent (sd.cpp lib test prompt)
TEST_PROMPT="a cat"
TEST_WIDTH=1024
TEST_HEIGHT=1024
TEST_STEPS=9
TEST_SEED=42

# Models to use
DIFFUSION_MODEL="z_image_turbo-Q8_0.gguf"
VAE_MODEL="ae_q8_0.gguf"
LLM_MODEL="Qwen3-4b-Z-Engineer-V2-Q8_0.gguf"

echo "Starting benchmark at $(date)"
echo "Results will be saved to: $RESULTS_FILE"

# Initialize results array
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
    echo "  offload_mode: $offload_mode"
    echo "  weight_type: $weight_type"
    echo "  reload_cond_stage: $reload_cond"
    echo "  reload_diffusion: $reload_diff"
    echo "========================================"

    # Unload current model first
    echo "Unloading current model..."
    curl -s -X POST "$API_URL/models/unload" > /dev/null 2>&1
    sleep 2

    # Get initial GPU memory
    gpu_mem_before=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')

    # Load model with test configuration
    echo "Loading model with config..."
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

    # Check if load succeeded
    load_success=$(echo "$load_result" | jq -r '.success // false')
    if [ "$load_success" != "true" ]; then
        echo "LOAD FAILED: $load_result"
        # Record failure
        if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
        first_test=false
        echo '{
            "test_name": "'"$test_name"'",
            "offload_mode": "'"$offload_mode"'",
            "weight_type": "'"$weight_type"'",
            "reload_cond_stage": '"$reload_cond"',
            "reload_diffusion": '"$reload_diff"',
            "status": "load_failed",
            "error": "'"$(echo "$load_result" | jq -r '.message // .error // "unknown"' | tr '"' "'")"'"
        }' >> "$RESULTS_FILE"
        return 1
    fi

    echo "Model loaded in ${load_time}s"
    sleep 2

    # Get GPU memory after load
    gpu_mem_after_load=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')
    echo "GPU memory after load: ${gpu_mem_after_load} MiB"

    # Run generation
    echo "Running generation..."
    gen_start=$(date +%s.%N)

    gen_result=$(curl -s -X POST "$API_URL/txt2img" \
        -H "Content-Type: application/json" \
        -d '{
            "prompt": "'"$TEST_PROMPT"'",
            "width": '"$TEST_WIDTH"',
            "height": '"$TEST_HEIGHT"',
            "steps": '"$TEST_STEPS"',
            "seed": '"$TEST_SEED"',
            "cfg_scale": 1.0,
            "sampler": "euler",
            "scheduler": "smoothstep"
        }')

    gen_end=$(date +%s.%N)
    gen_time=$(echo "$gen_end - $gen_start" | bc)

    # Check generation result
    gen_status=$(echo "$gen_result" | jq -r '.status // .error // "unknown"')
    job_id=$(echo "$gen_result" | jq -r '.job_id // "none"')

    if [ "$gen_status" != "queued" ] && [ "$gen_status" != "pending" ] && [ "$gen_status" != "completed" ]; then
        echo "GENERATION FAILED: $gen_result"
        if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
        first_test=false
        echo '{
            "test_name": "'"$test_name"'",
            "offload_mode": "'"$offload_mode"'",
            "weight_type": "'"$weight_type"'",
            "reload_cond_stage": '"$reload_cond"',
            "reload_diffusion": '"$reload_diff"',
            "status": "generation_failed",
            "error": "'"$(echo "$gen_result" | jq -r '.message // .error // "unknown"' | tr '"' "'")"'",
            "load_time_s": '"$load_time"',
            "gpu_mem_after_load_mib": '"$gpu_mem_after_load"'
        }' >> "$RESULTS_FILE"
        return 1
    fi

    # Wait for job completion
    echo "Waiting for job $job_id to complete..."
    max_wait=120
    waited=0
    while [ $waited -lt $max_wait ]; do
        job_status=$(curl -s "$API_URL/queue?job_id=$job_id" | jq -r '.items[0].status // "unknown"')
        if [ "$job_status" = "completed" ]; then
            break
        elif [ "$job_status" = "failed" ]; then
            echo "JOB FAILED"
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done

    # Get final timing from job
    job_info=$(curl -s "$API_URL/queue?job_id=$job_id" | jq '.items[0]')
    actual_gen_time=$(echo "$job_info" | jq -r '.timing.generation_time // .timing.total_time // 0')
    final_status=$(echo "$job_info" | jq -r '.status // "unknown"')

    # Get GPU memory after generation
    gpu_mem_after_gen=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | tr -d ' ')

    echo "Generation completed: status=$final_status, time=${actual_gen_time}s"
    echo "GPU memory after generation: ${gpu_mem_after_gen} MiB"

    # Record result
    if [ "$first_test" = false ]; then echo "," >> "$RESULTS_FILE"; fi
    first_test=false

    echo '{
        "test_name": "'"$test_name"'",
        "offload_mode": "'"$offload_mode"'",
        "weight_type": "'"$weight_type"'",
        "reload_cond_stage": '"$reload_cond"',
        "reload_diffusion": '"$reload_diff"',
        "status": "'"$final_status"'",
        "load_time_s": '"$load_time"',
        "generation_time_s": '"$actual_gen_time"',
        "total_request_time_s": '"$gen_time"',
        "gpu_mem_before_mib": '"$gpu_mem_before"',
        "gpu_mem_after_load_mib": '"$gpu_mem_after_load"',
        "gpu_mem_after_gen_mib": '"$gpu_mem_after_gen"'
    }' >> "$RESULTS_FILE"

    echo "Test $test_name completed"
    return 0
}

# Test configurations
# Format: run_test "name" "offload_mode" "weight_type" "reload_cond" "reload_diff"

# Test 1: No offload (baseline) - Q8_0
run_test "no_offload_q8" "none" "q8_0" "true" "true"

# Test 2: cond_only mode - Q8_0 - reload both
run_test "cond_only_q8_reload_both" "cond_only" "q8_0" "true" "true"

# Test 3: cond_only mode - Q8_0 - no reload
run_test "cond_only_q8_no_reload" "cond_only" "q8_0" "false" "false"

# Test 4: cond_only mode - Q8_0 - reload cond only
run_test "cond_only_q8_reload_cond" "cond_only" "q8_0" "true" "false"

# Test 5: cond_diffusion mode - Q8_0 - reload both
run_test "cond_diffusion_q8_reload_both" "cond_diffusion" "q8_0" "true" "true"

# Test 6: cond_diffusion mode - Q8_0 - no reload
run_test "cond_diffusion_q8_no_reload" "cond_diffusion" "q8_0" "false" "false"

# Test 7: aggressive mode - Q8_0 - reload both
run_test "aggressive_q8_reload_both" "aggressive" "q8_0" "true" "true"

# Test 8: aggressive mode - Q8_0 - no reload
run_test "aggressive_q8_no_reload" "aggressive" "q8_0" "false" "false"

# Close JSON
echo '
], "benchmark_end": "'$(date -Iseconds)'"}' >> "$RESULTS_FILE"

echo ""
echo "========================================"
echo "Benchmark complete!"
echo "Results saved to: $RESULTS_FILE"
echo "========================================"

# Print summary
echo ""
echo "Summary:"
jq -r '.tests[] | "\(.test_name): status=\(.status), gen_time=\(.generation_time_s // "N/A")s, gpu_after_load=\(.gpu_mem_after_load_mib // "N/A")MiB, gpu_after_gen=\(.gpu_mem_after_gen_mib // "N/A")MiB"' "$RESULTS_FILE"
