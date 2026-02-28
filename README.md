# SDCPP-RESTAPI

C++20 REST API server for [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp), providing HTTP endpoints for AI-powered image and video generation.

## Features

- **Generation**: txt2img, img2img, txt2vid, upscaling (ESRGAN)
- **Model Architectures**: SD 1.x/2.x, SDXL, Flux, SD3, Wan, Z-Image, Qwen, Chroma
- **Queue System**: Job queue with persistence and WebSocket progress updates
- **Live Preview**: Real-time preview during generation (TAE/VAE modes)
- **Web UI**: Integrated Vue.js interface with memory monitoring
- **Model Downloads**: CivitAI and HuggingFace integration
- **LLM Assistant**: Ollama-compatible prompt enhancement and chat
- **GPU Backends**: CUDA, Vulkan, Metal, ROCm, OpenCL
- **Memory Monitoring**: Real-time GPU/system memory usage in status bar
- **Experimental Features**: Dynamic tensor offloading for low-VRAM GPUs

## Quick Start

### Prerequisites

- CMake 3.20+
- Ninja build system
- C++20 compiler (GCC 11+ / Clang 14+)
- OpenSSL, ZLIB
- GPU drivers (CUDA, Vulkan, Metal, or ROCm)
- Node.js 16+ (for Web UI, optional)

### Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSD_CUDA=ON
cmake --build build
```

### Build Options

**GPU Backends:**
- `-DSD_CUDA=ON` - NVIDIA CUDA
- `-DSD_VULKAN=ON` - Vulkan (cross-platform)
- `-DSD_METAL=ON` - Apple Metal
- `-DSD_HIPBLAS=ON` - AMD ROCm
- `-DSD_OPENCL=ON` - OpenCL

**Optional Features:**
- `-DSDCPP_WEBUI=OFF` - Disable Web UI (default: ON)
- `-DSDCPP_WEBSOCKET=OFF` - Disable WebSocket server (default: ON)
- `-DSDCPP_ASSISTANT=OFF` - Disable LLM Assistant (default: ON)
- `-DSD_EXPERIMENTAL_OFFLOAD=ON` - Enable experimental dynamic tensor offloading (default: OFF)

### Configure

```bash
cp config.example.json config.json
# Edit config.json with your model paths
```

### Run

```bash
./build/bin/sdcpp-restapi
```

Server starts at `http://localhost:8080` by default. Web UI available at `http://localhost:8080/ui`.

## Installation (Linux with systemd)

For production deployment on Linux, use the install script which sets up a systemd service:

```bash
# Build first (see Quick Start above)
cmake --build build

# Install as system service
sudo ./scripts/install.sh
```

### Install Script Features

- **Systemd Integration**: Automatic startup, restart on failure
- **Interactive Setup**: Prompts for server port, model paths, LLM configuration
- **Ollama/LLM Config**: Configure prompt enhancement and AI assistant endpoints
- **Directory Structure**: Creates organized model subdirectories
- **Config Preservation**: Updates preserve existing custom settings
- **Security Hardening**: Service runs with restricted permissions

### Install Options

```bash
sudo ./scripts/install.sh [options]

Options:
  --user USER       Run service as USER (default: current user)
  --host HOST       Server listen address (default: 0.0.0.0)
  --port PORT       Server port (default: 8080)
  --models-dir DIR  Directory for model files
  --output-dir DIR  Directory for generated images
  --no-service      Don't install systemd service
  --update-webui    Quick update of Web UI files only
  --uninstall       Remove the installation
```

### Service Management

```bash
sudo systemctl start sdcpp-restapi    # Start
sudo systemctl stop sdcpp-restapi     # Stop
sudo systemctl status sdcpp-restapi   # Status
sudo journalctl -u sdcpp-restapi -f   # View logs
```

### Installation Paths

| Path | Description |
|------|-------------|
| `/opt/sdcpp-restapi/` | Binary and Web UI |
| `/etc/sdcpp-restapi/config.json` | Configuration file |
| `<models-dir>/` | Model subdirectories (checkpoints, vae, lora, etc.) |

## Configuration

Edit `config.json` to set:
- **server**: Host, port, thread count
- **paths**: Model directories (checkpoints, VAE, LoRA, CLIP, T5, etc.)
- **sd_defaults**: Default generation settings
- **preview**: Live preview mode and interval
- **ollama**: LLM prompt enhancement settings
- **assistant**: LLM chat assistant settings

See `config.example.json` for all options.

## API

Full API documentation: [docs/API.md](docs/API.md)

### Key Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /health` | Server status, loaded model info, and feature flags |
| `GET /memory` | System and GPU memory usage |
| `GET /models` | List available models |
| `POST /models/load` | Load a model with options |
| `POST /models/unload` | Unload current model |
| `POST /txt2img` | Text-to-image generation |
| `POST /img2img` | Image-to-image generation |
| `POST /txt2vid` | Text-to-video generation |
| `POST /upscale` | Image upscaling (ESRGAN) |
| `GET /queue/{id}` | Get job status |
| `GET /architectures` | Get model architecture presets |

### WebSocket

Real-time updates on port 8081:
- Job progress and previews
- Model loading status
- Server heartbeat

## Web UI

Access the integrated web interface at `http://localhost:8080/ui`

### Screenshots

#### File manager
<img width="1893" height="940" alt="Képernyőkép_20260104_111732" src="https://github.com/user-attachments/assets/825db2af-126e-4bf8-8bc7-1b8560d8a23b" />

#### Webui
 - Dashboard 
<img width="1904" height="941" alt="Képernyőkép_20260104_111600" src="https://github.com/user-attachments/assets/e2508dea-e3e3-47f6-b4a8-f41ee1024fad" />

 - Model management
<img width="1904" height="941" alt="Képernyőkép_20260104_111554" src="https://github.com/user-attachments/assets/5073e913-6e1d-4523-bc08-e72ca966329b" />

 - Queue
<img width="1902" height="950" alt="Képernyőkép_20260104_112318" src="https://github.com/user-attachments/assets/7072fb9d-985b-4155-960b-538fc11e071b" />

 - Assistant (ollama)
<img width="1904" height="941" alt="Képernyőkép_20260104_111611" src="https://github.com/user-attachments/assets/0aabacdf-1483-491e-970a-1b00d8cb7052" />

## Experimental Features

### Dynamic Tensor Offloading

For GPUs with limited VRAM, the experimental dynamic tensor offloading feature allows running large models by temporarily moving model components to CPU memory during generation.

**Enable at build time:**
```bash
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DSD_CUDA=ON -DSD_EXPERIMENTAL_OFFLOAD=ON
```

This enables:
- **Offload Modes**: `cond_only`, `cond_diffusion`, `aggressive`
- **VRAM Estimation**: Automatic calculation of required GPU memory
- **Component Management**: Offload/reload LLM, CLIP, and diffusion models as needed

When enabled, the Web UI shows a "VRAM Offloading" section in model load settings.

**Note:** This feature requires a [forked version of stable-diffusion.cpp](https://github.com/fszontagh/stable-diffusion.cpp/tree/feature/dynamic-tensor-offloading) with offloading support. The fork is automatically fetched when `SD_EXPERIMENTAL_OFFLOAD=ON`.

## Documentation

- [API Reference](docs/API.md) - Complete endpoint documentation
- [Library Reference](docs/LIBRARY_REFERENCE.md) - Developer reference for underlying libraries

## License

MIT License
