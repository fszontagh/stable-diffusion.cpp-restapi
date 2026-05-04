# Deploying sdcpp-restapi on RunPod

This guide walks through the full path from local repository to a running
sdcpp-restapi pod on RunPod, including model auto-bootstrap. It captures the
exact steps that produced the working deployment in `axk54v1gd0l5k7`
(EU-RO-1, RTX A4500, network volume `k5r2rz6nar`).

## Prerequisites

- Docker 24+ on a build host (CUDA toolchain not required on the host —
  the build runs inside `nvidia/cuda:12.4.0-devel`)
- A Docker Hub account (or any other public registry)
- A RunPod account with:
  - Sufficient balance for at least the monthly volume cost (~$5)
  - SSH public key added under Settings → SSH Keys (account-level, not per-pod)
  - An API key (Settings → API Keys) for scripted deploys

## What gets shipped

| Component | Where |
|---|---|
| `sdcpp-restapi` binary | `/opt/sdcpp/bin/sdcpp-restapi` |
| WebUI dist | `/opt/sdcpp/share/sdcpp-restapi/webui` |
| Docs (served at `/docs`) | `/opt/sdcpp/share/sdcpp-restapi/docs` |
| Bootstrap downloader | `/opt/sdcpp/bin/bootstrap-models.sh` |
| Bootstrap registry | `/opt/sdcpp/data/bootstrap_models.json` |
| Architecture presets | `/opt/sdcpp/data/model_architectures.json` |
| Container config | `/etc/sdcpp-restapi/config.json` |
| Entrypoint | `/usr/local/bin/entrypoint.sh` |

The image is built with `SD_CUDA=ON` and `SD_EXPERIMENTAL_OFFLOAD=ON`. The
experimental flag is required for fresh CMake builds — without it,
upstream sd.cpp's pinned short SHA fails shallow-clone.

## Build and push the image

```bash
# From the repo root
docker build \
    --build-arg CMAKE_CUDA_ARCHITECTURES=89 \
    -t fszontagh/sdcpp-restapi:cuda \
    .

# Tag with version for immutable reference
docker tag fszontagh/sdcpp-restapi:cuda fszontagh/sdcpp-restapi:0.2.0-cuda

# Push both
docker login
docker push fszontagh/sdcpp-restapi:cuda
docker push fszontagh/sdcpp-restapi:0.2.0-cuda
```

`CMAKE_CUDA_ARCHITECTURES`:

- `89` — RTX 4090, RTX 4080, RTX PRO 4500, L40 (single arch, fastest build)
- `86;89` — Adds Ampere (RTX 3090, A4000/4500/5000/6000, A40, A100 80G excluded)
- `80` — A100 datacenter
- `90` — H100, H200
- `80;86;89;90` — Covers nearly all GPUs RunPod offers (slowest build)

For a single-target deploy, narrow to one arch. The Dockerfile defaults to
the broad list.

## Deploy via RunPod web console

Quickest path if you don't want scripting.

1. **Create a network volume** (one-time, persists across pod restarts):
   - RunPod → Storage → Network Volumes → New
   - Name: `sdcpp-models` (any name; referenced by ID later)
   - Region: choose nearest with GPUs you want — see "Choosing a data center"
   - Size: 50 GB (room for the full Z-Image profile + outputs + a second arch)

2. **Deploy a pod**:
   - Pods → Deploy → Custom Container
   - Container Image: `fszontagh/sdcpp-restapi:cuda`
   - GPU: pick one with ≥ 12 GB VRAM (see "Choosing a GPU")
   - Network Volume: attach the one created above; mount path `/workspace`
   - Container Disk: 10 GB (default)
   - Expose HTTP Ports: `8080`
   - Expose TCP Ports: `22` (for SSH)
   - Environment Variables:
     ```
     SDCPP_BOOTSTRAP_ARCHITECTURES=z-image-nvfp4
     ```
     (or `z-image` for full bf16 if your GPU has ≥ 24 GB VRAM)
   - Click Deploy

3. **Wait for first boot** (5–10 min):
   - Image pull: 1–2 min on a fresh host (often cached on RunPod hosts)
   - Bootstrap downloads ~10.5 GB (NVFP4) or ~21 GB (full bf16)
   - Server start: instantaneous after bootstrap

4. **Open the WebUI**:
   - Pod → Connect → "Connect to HTTP Service [Port 8080]"
   - Direct URL: `https://<pod-id>-8080.proxy.runpod.net`

## Deploy via GraphQL API

For scripting or repeatable deploys.

```bash
RUNPOD_API_KEY="rpa_..."
DC="EU-RO-1"
GPU_TYPE="NVIDIA RTX A4500"
IMAGE="fszontagh/sdcpp-restapi:cuda"
BOOTSTRAP_PROFILE="z-image-nvfp4"

# 1. Create the network volume (skip if reusing existing)
VOLUME_ID=$(curl -s -X POST https://api.runpod.io/graphql \
    -H "Authorization: Bearer $RUNPOD_API_KEY" \
    -H "Content-Type: application/json" \
    -d "{\"query\":\"mutation { createNetworkVolume(input:{name:\\\"sdcpp-models\\\", size:50, dataCenterId:\\\"$DC\\\"}) { id } }\"}" \
    | jq -r '.data.createNetworkVolume.id')

# 2. Deploy the pod
curl -s -X POST https://api.runpod.io/graphql \
    -H "Authorization: Bearer $RUNPOD_API_KEY" \
    -H "Content-Type: application/json" \
    -d "{
        \"query\": \"mutation deployPod(\$input: PodFindAndDeployOnDemandInput!) { podFindAndDeployOnDemand(input: \$input) { id machine { podHostId } } }\",
        \"variables\": {
            \"input\": {
                \"cloudType\": \"ALL\",
                \"gpuCount\": 1,
                \"containerDiskInGb\": 10,
                \"minVcpuCount\": 2,
                \"minMemoryInGb\": 15,
                \"gpuTypeId\": \"$GPU_TYPE\",
                \"name\": \"sdcpp-restapi\",
                \"imageName\": \"$IMAGE\",
                \"ports\": \"8080/http,22/tcp\",
                \"volumeMountPath\": \"/workspace\",
                \"env\": [{\"key\": \"SDCPP_BOOTSTRAP_ARCHITECTURES\", \"value\": \"$BOOTSTRAP_PROFILE\"}],
                \"networkVolumeId\": \"$VOLUME_ID\",
                \"dataCenterId\": \"$DC\"
            }
        }
    }" | jq .
```

## Choosing a GPU

The image supports any CUDA-capable GPU but only architectures matching
`CMAKE_CUDA_ARCHITECTURES` at build time. Match the GPU to the model size:

| Profile | Total weights | Min VRAM | GPU |
|---|---|---|---|
| `z-image-nvfp4` | ~10.5 GB | 12 GB | RTX 3060 / 4060 Ti / A4000 / A4500 / 4080 |
| `z-image` (bf16) | ~21 GB | 24 GB | RTX 3090 / 4090 / A5000 / RTX PRO 4500 / 5090 |

Cheapest >12 GB cards observed in EU-RO-1 (2026-05):

| GPU | VRAM | $/hr | Stock |
|---|---|---|---|
| RTX A4500 | 20 | $0.19 | Low |
| RTX 4000 Ada | 20 | $0.20 | Low |
| RTX 3090 (EU-CZ-1) | 24 | $0.22 | Low |
| RTX 4090 | 24 | $0.34 | Low |
| RTX PRO 4500 | 32 | $0.34 | High ← reliable |

Stock varies by hour. The PRO 4500 is the only EU GPU with consistently
High stock; everything else can fail to deploy at peak times.

To check current availability for a GPU type:

```bash
curl -s -X POST https://api.runpod.io/graphql \
    -H "Authorization: Bearer $RUNPOD_API_KEY" \
    -d '{"query":"query { gpuTypes { displayName memoryInGb lowestPrice(input:{gpuCount:1, dataCenterId:\"EU-RO-1\"}) { uninterruptablePrice stockStatus } } }"}' \
    -H "Content-Type: application/json" \
    | jq '[.data.gpuTypes[] | select(.lowestPrice.uninterruptablePrice != null)] | sort_by(.lowestPrice.uninterruptablePrice) | .[]'
```

## Choosing a data center

Network volumes are pinned to a single data center — pick before creating
the volume. EU-RO-1, EU-CZ-1, EU-NL-1, EUR-IS-1, EUR-IS-3, EUR-NO-1 are
all in Europe; latency from Hungary is best at EU-CZ-1 / EU-RO-1.

For US users, US-CA-2 / US-IL-1 / US-TX-3 / US-WA-1 / US-MO-2 are common
choices.

## Bootstrap profiles

Defined in `data/bootstrap_models.json` (built into the image). To add a
new architecture, edit that file and rebuild the image:

```json
"flux": {
  "description": "Flux.1 dev + ae VAE + T5-XXL + CLIP-L",
  "files": [
    {
      "name":    "flux1-dev.safetensors",
      "dest":    "diffusion_models",
      "url":     "https://...",
      "size_mb": 23800,
      "auth":    "hf"
    },
    ...
  ]
}
```

Then on the pod, set `SDCPP_BOOTSTRAP_ARCHITECTURES=z-image,flux` (comma-
separated, downloads sequentially).

`auth: "hf"` sends the `HF_TOKEN` env var as a Bearer header — required
for gated models on Hugging Face. Set `HF_TOKEN` on the pod env if needed.

The bootstrap is idempotent and resumable: re-running it on an existing
volume skips files already present, and `curl --continue-at -` resumes
partial downloads from byte offset. Safe to run on every pod start.

## Connecting

### WebUI (HTTP)

`https://<pod-id>-8080.proxy.runpod.net`

The proxy URL stays stable for the lifetime of the pod. RunPod handles
HTTPS termination — your container just sees plain HTTP on `:8080`.

### SSH

```bash
ssh root@<pod-id>.runpod.io
```

Uses your account-level SSH key (added under Settings → SSH Keys). The
pod's `pubKey` field auto-populates with your key on deploy.

If your account-level `pubKey` is null in the API response:

```bash
curl -s -X POST https://api.runpod.io/graphql \
    -H "Authorization: Bearer $RUNPOD_API_KEY" \
    -H "Content-Type: application/json" \
    -d '{"query":"query { myself { pubKey } }"}'
```

… you've added the key to a different RunPod account than the one your
API key belongs to. Sort that out before deploying — the pod will spin up
without a working SSH key otherwise.

### MCP

```
POST https://<pod-id>-8080.proxy.runpod.net/mcp
```

Standard JSON-RPC 2.0 / Streamable HTTP transport. See `docs/MCP.md`.

## Stopping, restarting, deleting

| Action | What persists | Cost |
|---|---|---|
| Stop pod (RunPod console) | Volume + image cache | Volume only ($5/mo for 50 GB) |
| Restart stopped pod | Same `pod-id`, same proxy URL | Resumes hourly |
| Terminate pod | Volume only | Volume only |
| Delete network volume | Nothing | Stops |

Stop the pod when not actively generating. The volume keeps your models,
queue state, and outputs. Restarting reuses everything in seconds —
bootstrap detects existing files and skips.

## Cost worksheet

```
GPU                  $/hr      40 h/mo cost
RTX A4500 ($0.19)    $0.19     $7.60
RTX A4500 (realized) $0.25     $10.00
RTX 4000 Ada         $0.20     $8.00
RTX 3090             $0.22     $8.80
RTX 4090             $0.34     $13.60
RTX PRO 4500         $0.34     $13.60

Network volume: $0.10/GB/mo
50 GB volume:   $5.00/mo
100 GB volume:  $10.00/mo

Example total (A4500, 40 h/mo, 50 GB volume): $12.60/mo
```

The "lowest" price reported by the GraphQL API is a floor; actual realized
hourly varies by host machine within a GPU class (`costPerHr` on the pod
object is the authoritative number once deployed).

## Troubleshooting

### Pod stuck in "Initializing" for >15 min

Image pull may have timed out. Check the pod's container logs in the web
console; if there's no progress, terminate and redeploy. Often a different
host gets assigned and the pull succeeds.

### `failed to deploy` / `no GPUs available`

The GPU type ran out of stock between query and deploy. Either:

- Retry — RunPod releases GPUs continuously
- Pick a GPU with "High" stock (see availability query above)
- Pick a different data center

### Bootstrap fails with "URL has placeholder"

The `bootstrap_models.json` baked into the image has `TODO_FILL_IN_*`
URLs for an architecture you requested. Edit `data/bootstrap_models.json`
in the repo, rebuild the image, push the new tag, redeploy.

### Bootstrap fails with HTTP 401/403

The model URL is gated and `HF_TOKEN` is unset (or wrong). Add `HF_TOKEN`
to the pod env vars — your token must have permission for the model
(accept license on HuggingFace first).

### `libcuda.so.1: cannot open shared object file` (only when running locally)

You're missing `--gpus all` on the `docker run` command. RunPod always
mounts the host driver automatically; this only happens locally without
nvidia-container-toolkit configured.

### `model_architectures.json: not found`

Should never happen with this image — `architecture_manager.cpp` searches
the exe-relative path `/opt/sdcpp/bin/../data/` which is populated by the
Dockerfile's `cp /src/data/model_architectures.json /opt/sdcpp/data/`
step. If it does occur, rebuild the image; the COPY likely failed.

## Reference: this deployment

Saved here for the record:

```
Date:           2026-05-04
Account email:  szontagh.ferenc@gmail.com
Account ID:     user_2kYBTtg5ONWGs1zrxGHP9mPpmc2
Network volume: k5r2rz6nar  (sdcpp-models, 50 GB, EU-RO-1)
Pod ID:         axk54v1gd0l5k7
GPU:            RTX A4500 (20 GB, EU-RO-1)
Realized $/hr:  $0.25
Image:          fszontagh/sdcpp-restapi:0.2.0-cuda
Bootstrap:      z-image-nvfp4
WebUI URL:      https://axk54v1gd0l5k7-8080.proxy.runpod.net
SSH:            ssh root@axk54v1gd0l5k7.runpod.io
```
