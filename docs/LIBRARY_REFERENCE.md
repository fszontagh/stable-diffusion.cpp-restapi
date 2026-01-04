# Library Reference

This document provides usage reference for the external libraries used in sdcpp-restapi.

---

## Table of Contents

1. [stable-diffusion.cpp API](#stable-diffusioncpp-api)
2. [cpp-httplib Usage](#cpp-httplib-usage)
3. [nlohmann/json Usage](#nlohmannjson-usage)

---

## stable-diffusion.cpp API

### Header Include

```cpp
#include "stable-diffusion.h"
```

### Core Enumerations

#### Sampling Methods
```cpp
enum sample_method_t {
    EULER_SAMPLE_METHOD,
    EULER_A_SAMPLE_METHOD,
    HEUN_SAMPLE_METHOD,
    DPM2_SAMPLE_METHOD,
    DPMPP2S_A_SAMPLE_METHOD,
    DPMPP2M_SAMPLE_METHOD,
    DPMPP2Mv2_SAMPLE_METHOD,
    IPNDM_SAMPLE_METHOD,
    IPNDM_V_SAMPLE_METHOD,
    LCM_SAMPLE_METHOD,
    DDIM_TRAILING_SAMPLE_METHOD,
    TCD_SAMPLE_METHOD,
    SAMPLE_METHOD_COUNT
};
```

#### Schedulers
```cpp
enum scheduler_t {
    DISCRETE_SCHEDULER,
    KARRAS_SCHEDULER,
    EXPONENTIAL_SCHEDULER,
    AYS_SCHEDULER,
    GITS_SCHEDULER,
    SGM_UNIFORM_SCHEDULER,
    SIMPLE_SCHEDULER,
    SMOOTHSTEP_SCHEDULER,
    LCM_SCHEDULER,
    SCHEDULER_COUNT
};
```

#### Data Types
```cpp
enum sd_type_t {
    SD_TYPE_F32  = 0,
    SD_TYPE_F16  = 1,
    SD_TYPE_Q4_0 = 2,
    SD_TYPE_Q4_1 = 3,
    SD_TYPE_Q5_0 = 6,
    SD_TYPE_Q5_1 = 7,
    SD_TYPE_Q8_0 = 8,
    SD_TYPE_Q4_K = 12,
    SD_TYPE_Q5_K = 13,
    SD_TYPE_Q6_K = 14,
    SD_TYPE_Q8_K = 15,
    SD_TYPE_BF16 = 30,
    // ... more types
};
```

### Core Structures

#### Image Structure
```cpp
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t channel;  // Usually 3 for RGB
    uint8_t* data;     // Raw pixel data (RGB, row-major)
} sd_image_t;
```

#### Context Parameters
```cpp
typedef struct {
    const char* model_path;               // Main checkpoint path
    const char* clip_l_path;              // CLIP-L text encoder
    const char* clip_g_path;              // CLIP-G text encoder (SDXL, SD3)
    const char* t5xxl_path;               // T5-XXL text encoder (Flux, SD3, Wan)
    const char* diffusion_model_path;     // Separate diffusion model (DiT/UNet)
    const char* vae_path;                 // VAE path
    const char* lora_model_dir;           // Directory for LoRA models
    bool vae_decode_only;                 // Only load VAE decoder
    int n_threads;                        // Number of CPU threads
    enum sd_type_t wtype;                 // Weight type override
    enum rng_type_t rng_type;             // RNG type
    bool keep_clip_on_cpu;                // Keep CLIP on CPU
    bool keep_vae_on_cpu;                 // Keep VAE on CPU
    bool diffusion_flash_attn;            // Enable flash attention
    bool offload_params_to_cpu;           // Offload to CPU when not in use
    float flow_shift;                     // Flow shift parameter
} sd_ctx_params_t;
```

#### Image Generation Parameters
```cpp
typedef struct {
    const sd_lora_t* loras;
    uint32_t lora_count;
    const char* prompt;
    const char* negative_prompt;
    int clip_skip;             // -1 for default
    sd_image_t init_image;     // For img2img (set data=NULL for txt2img)
    sd_image_t mask_image;     // For inpainting
    int width;
    int height;
    sd_sample_params_t sample_params;
    float strength;            // img2img strength (0.0-1.0)
    int64_t seed;              // -1 for random
    int batch_count;
    sd_image_t control_image;  // ControlNet input
    float control_strength;
} sd_img_gen_params_t;
```

#### Video Generation Parameters
```cpp
typedef struct {
    const sd_lora_t* loras;
    uint32_t lora_count;
    const char* prompt;
    const char* negative_prompt;
    sd_image_t init_image;     // Start frame for I2V
    sd_image_t end_image;      // End frame for FLF2V
    int width;
    int height;
    sd_sample_params_t sample_params;
    float strength;
    int64_t seed;
    int video_frames;          // Number of output frames
} sd_vid_gen_params_t;
```

### Core API Functions

#### Initialization
```cpp
// Initialize parameters with defaults
void sd_ctx_params_init(sd_ctx_params_t* params);
void sd_sample_params_init(sd_sample_params_t* params);
void sd_img_gen_params_init(sd_img_gen_params_t* params);
void sd_vid_gen_params_init(sd_vid_gen_params_t* params);
```

#### Context Management
```cpp
// Create SD context (loads model into memory)
sd_ctx_t* new_sd_ctx(const sd_ctx_params_t* params);

// Free SD context (unloads model)
void free_sd_ctx(sd_ctx_t* ctx);

// Get optimal defaults for loaded model
enum sample_method_t sd_get_default_sample_method(const sd_ctx_t* ctx);
enum scheduler_t sd_get_default_scheduler(const sd_ctx_t* ctx);
```

#### Generation
```cpp
// Generate images (txt2img if init_image.data is NULL, img2img otherwise)
// Returns array of batch_count images, caller must free
sd_image_t* generate_image(sd_ctx_t* ctx, const sd_img_gen_params_t* params);

// Generate video frames
// Returns array of frames, num_frames_out is set to actual count
sd_image_t* generate_video(sd_ctx_t* ctx, const sd_vid_gen_params_t* params, int* num_frames_out);
```

#### Callbacks
```cpp
// Logging callback
typedef void (*sd_log_cb_t)(enum sd_log_level_t level, const char* text, void* data);
void sd_set_log_callback(sd_log_cb_t cb, void* data);

// Progress callback (called each sampling step)
typedef void (*sd_progress_cb_t)(int step, int steps, float time, void* data);
void sd_set_progress_callback(sd_progress_cb_t cb, void* data);
```

#### Utilities
```cpp
int32_t sd_get_num_physical_cores();
const char* sd_get_system_info();

// String conversion
const char* sd_sample_method_name(enum sample_method_t method);
enum sample_method_t str_to_sample_method(const char* str);
const char* sd_scheduler_name(enum scheduler_t scheduler);
enum scheduler_t str_to_scheduler(const char* str);
```

### Usage Examples

#### Loading SD1.x/SDXL Model
```cpp
sd_ctx_params_t params;
sd_ctx_params_init(&params);

params.model_path = "/path/to/model.safetensors";
params.vae_path = "/path/to/vae.safetensors";  // Optional, may be bundled
params.n_threads = sd_get_num_physical_cores();
params.vae_decode_only = true;  // txt2img only needs decoder

sd_ctx_t* ctx = new_sd_ctx(&params);
if (!ctx) {
    // Handle error
}
```

#### Loading Flux Model
```cpp
sd_ctx_params_t params;
sd_ctx_params_init(&params);

params.diffusion_model_path = "/path/to/flux1-dev.gguf";
params.vae_path = "/path/to/ae.safetensors";
params.clip_l_path = "/path/to/clip_l.safetensors";
params.t5xxl_path = "/path/to/t5xxl_fp16.safetensors";
params.keep_clip_on_cpu = true;
params.diffusion_flash_attn = true;
params.n_threads = sd_get_num_physical_cores();

sd_ctx_t* ctx = new_sd_ctx(&params);
```

#### Loading Wan Video Model
```cpp
sd_ctx_params_t params;
sd_ctx_params_init(&params);

params.diffusion_model_path = "/path/to/wan2.2_ti2v_5B_fp16.safetensors";
params.vae_path = "/path/to/wan_2.1_vae.safetensors";
params.t5xxl_path = "/path/to/umt5-xxl-encoder-Q8_0.gguf";
params.offload_params_to_cpu = true;
params.diffusion_flash_attn = true;
params.flow_shift = 3.0f;

sd_ctx_t* ctx = new_sd_ctx(&params);
```

#### Text-to-Image Generation
```cpp
sd_img_gen_params_t gen_params;
sd_img_gen_params_init(&gen_params);

gen_params.prompt = "a lovely cat holding a sign";
gen_params.negative_prompt = "blurry, low quality";
gen_params.width = 512;
gen_params.height = 512;
gen_params.seed = 42;  // -1 for random
gen_params.batch_count = 1;

gen_params.sample_params.sample_steps = 20;
gen_params.sample_params.guidance.txt_cfg = 7.0f;
gen_params.sample_params.sample_method = EULER_A_SAMPLE_METHOD;
gen_params.sample_params.scheduler = DISCRETE_SCHEDULER;

sd_image_t* images = generate_image(ctx, &gen_params);
if (images) {
    // images[0].data contains RGB pixel data
    // images[0].width, images[0].height, images[0].channel
    
    // Save using stb_image_write
    stbi_write_png("output.png", images[0].width, images[0].height, 
                   images[0].channel, images[0].data, 0);
    
    // Free memory
    free(images[0].data);
    free(images);
}
```

#### Image-to-Image Generation
```cpp
// Load input image
int w, h, c;
uint8_t* input_data = stbi_load("input.png", &w, &h, &c, 3);

sd_img_gen_params_t gen_params;
sd_img_gen_params_init(&gen_params);

gen_params.prompt = "watercolor painting";
gen_params.init_image.width = w;
gen_params.init_image.height = h;
gen_params.init_image.channel = 3;
gen_params.init_image.data = input_data;
gen_params.strength = 0.75f;  // How much to change (0=no change, 1=full)
gen_params.width = w;
gen_params.height = h;
gen_params.sample_params.sample_steps = 20;
gen_params.sample_params.guidance.txt_cfg = 7.0f;
gen_params.seed = 42;
gen_params.batch_count = 1;

sd_image_t* images = generate_image(ctx, &gen_params);

stbi_image_free(input_data);
```

#### Video Generation
```cpp
sd_vid_gen_params_t vid_params;
sd_vid_gen_params_init(&vid_params);

vid_params.prompt = "a cat walking through garden";
vid_params.negative_prompt = "static, blurry";
vid_params.width = 832;
vid_params.height = 480;
vid_params.video_frames = 33;  // (frames-1) must be divisible by 4
vid_params.seed = 42;

vid_params.sample_params.sample_steps = 30;
vid_params.sample_params.guidance.txt_cfg = 6.0f;
vid_params.sample_params.sample_method = EULER_SAMPLE_METHOD;

int num_frames;
sd_image_t* frames = generate_video(ctx, &vid_params, &num_frames);

if (frames) {
    for (int i = 0; i < num_frames; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "frame_%04d.png", i);
        stbi_write_png(filename, frames[i].width, frames[i].height,
                       frames[i].channel, frames[i].data, 0);
        free(frames[i].data);
    }
    free(frames);
}
```

#### Using LoRA
```cpp
// Method 1: In prompt
gen_params.prompt = "a lovely cat<lora:my_lora:0.8>";

// Method 2: Via parameters
sd_lora_t loras[1] = {
    {false, 0.8f, "/path/to/my_lora.safetensors"}
};
gen_params.loras = loras;
gen_params.lora_count = 1;
```

#### Progress Callback
```cpp
void progress_callback(int step, int steps, float time, void* data) {
    int* job_progress = (int*)data;
    *job_progress = (step * 100) / steps;
    printf("Progress: %d/%d (%.2fs)\n", step, steps, time);
}

// Set before generation
int progress = 0;
sd_set_progress_callback(progress_callback, &progress);
```

---

## cpp-httplib Usage

### Header Include

```cpp
#include "httplib.h"
```

### Basic Server Setup

```cpp
httplib::Server svr;

// Simple GET endpoint
svr.Get("/hello", [](const httplib::Request& req, httplib::Response& res) {
    res.set_content("Hello World!", "text/plain");
});

// Start server (blocking)
svr.listen("0.0.0.0", 8080);
```

### Handling JSON Requests

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

svr.Post("/api/generate", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = json::parse(req.body);
        
        std::string prompt = body.value("prompt", "");
        int width = body.value("width", 512);
        int height = body.value("height", 512);
        
        // Process request...
        
        json response = {
            {"status", "success"},
            {"job_id", "abc123"}
        };
        res.set_content(response.dump(), "application/json");
        
    } catch (const json::exception& e) {
        res.status = 400;
        json error = {{"error", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
});
```

### Path Parameters

```cpp
// Named parameters with :param syntax
svr.Get("/queue/:job_id", [](const httplib::Request& req, httplib::Response& res) {
    auto job_id = req.path_params.at("job_id");
    // Use job_id...
});

// Multiple parameters
svr.Get("/models/hash/:type/:name", [](const httplib::Request& req, httplib::Response& res) {
    auto type = req.path_params.at("type");
    auto name = req.path_params.at("name");
});
```

### Static File Serving

```cpp
// Mount /output URL to /data/output directory
svr.set_mount_point("/output", "/data/output");
```

### Thread Safety

```cpp
#include <mutex>

std::mutex data_mutex;
std::map<std::string, Data> shared_data;

svr.Post("/data", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(data_mutex);
    // Safe to access shared_data
});
```

### Error Handling

```cpp
svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
    json error = {
        {"error", "Not Found"},
        {"path", req.path},
        {"status", res.status}
    };
    res.set_content(error.dump(), "application/json");
});

svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                              std::exception_ptr ep) {
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& e) {
        json error = {{"error", e.what()}};
        res.set_content(error.dump(), "application/json");
    }
    res.status = 500;
});
```

### Graceful Shutdown

```cpp
// From another thread or signal handler
svr.stop();
```

---

## nlohmann/json Usage

### Basic Operations

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Create JSON
json j = {
    {"name", "test"},
    {"value", 42},
    {"array", {1, 2, 3}},
    {"nested", {{"key", "value"}}}
};

// Serialize to string
std::string str = j.dump();        // Compact
std::string pretty = j.dump(4);    // Pretty-printed with 4 spaces

// Parse from string
json parsed = json::parse(str);

// Access values
std::string name = j["name"];
int value = j["value"];

// Safe access with default
std::string opt = j.value("optional", "default");

// Check existence
if (j.contains("name")) { }

// Iterate
for (auto& [key, value] : j.items()) {
    std::cout << key << ": " << value << "\n";
}
```

### File I/O

```cpp
// Read from file
std::ifstream file("config.json");
json config = json::parse(file);

// Write to file
std::ofstream out("output.json");
out << j.dump(4);
```

### Type Checking

```cpp
if (j["value"].is_number()) { }
if (j["name"].is_string()) { }
if (j["array"].is_array()) { }
if (j["nested"].is_object()) { }
if (j["maybe"].is_null()) { }
```

---

## Image I/O with stb_image

### Reading Images

```cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int width, height, channels;
uint8_t* data = stbi_load("input.png", &width, &height, &channels, 3);  // Force RGB

if (data) {
    // Use data...
    stbi_image_free(data);
}
```

### Writing Images

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// PNG (lossless)
stbi_write_png("output.png", width, height, channels, data, width * channels);

// JPEG (lossy, quality 0-100)
stbi_write_jpg("output.jpg", width, height, channels, data, 90);
```

### Base64 Encoding/Decoding

For API image transfer, implement base64 encoding:

```cpp
#include <string>
#include <vector>

// Base64 encoding table
static const char* b64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (data[i] << 16);
        if (i + 1 < len) n |= (data[i + 1] << 8);
        if (i + 2 < len) n |= data[i + 2];
        
        result += b64_chars[(n >> 18) & 0x3F];
        result += b64_chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? b64_chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? b64_chars[n & 0x3F] : '=';
    }
    return result;
}
```
