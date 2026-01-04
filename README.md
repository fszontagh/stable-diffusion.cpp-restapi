# SDCPP-RESTAPI

⚠️ WARNING: This is a vibe coded project!

C++20 REST API server for [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp), providing HTTP endpoints for AI-powered image and video generation.

## Features

- **Generation**: txt2img, img2img, txt2vid, upscaling (ESRGAN)
- **Model Architectures**: SD 1.x/2.x, SDXL, Flux, SD3, Wan, Z-Image, Qwen
- **Queue System**: Job queue with persistence and WebSocket progress updates
- **Live Preview**: Real-time preview during generation (TAE/VAE modes)
- **Web UI**: Integrated Vue.js interface
- **Model Downloads**: CivitAI and HuggingFace integration
- **LLM Assistant**: Ollama-compatible prompt enhancement and chat
- **GPU Backends**: CUDA, Vulkan, Metal, ROCm, OpenCL

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
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DSD_CUDA=ON
ninja
```

GPU backend options:
- `-DSD_CUDA=ON` - NVIDIA CUDA
- `-DSD_VULKAN=ON` - Vulkan (cross-platform)
- `-DSD_METAL=ON` - Apple Metal
- `-DSD_HIPBLAS=ON` - AMD ROCm
- `-DSD_OPENCL=ON` - OpenCL

### Configure

```bash
cp config.example.json config.json
# Edit config.json with your model paths
```

### Run

```bash
./bin/sdcpp-restapi
```

Server starts at `http://localhost:8080` by default.

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
| `GET /health` | Server status and loaded model info |
| `GET /models` | List available models |
| `POST /models/load` | Load a model |
| `POST /txt2img` | Text-to-image generation |
| `POST /img2img` | Image-to-image generation |
| `POST /txt2vid` | Text-to-video generation |
| `POST /upscale` | Image upscaling |
| `GET /queue/{id}` | Get job status |

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

## Documentation

- [API Reference](docs/API.md) - Complete endpoint documentation
- [Library Reference](docs/LIBRARY_REFERENCE.md) - Developer reference for underlying libraries

## License

MIT License
