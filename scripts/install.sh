#!/bin/bash
#
# SDCPP-RESTAPI Installer Script
#
# This script installs sdcpp-restapi to /opt/sdcpp-restapi and configures
# a systemd service for automatic startup.
#
# Usage: sudo ./install.sh [options]
#
# Options:
#   --uninstall     Remove the installation
#   --update-webui  Only update the Web UI (quick update)
#   --user USER     Run service as USER (default: current user)
#   --no-service    Don't install systemd service
#   --help          Show this help message
#

set -e

# Configuration
INSTALL_DIR="/opt/sdcpp-restapi"
SERVICE_NAME="sdcpp-restapi"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
CONFIG_DIR="/etc/sdcpp-restapi"
CONFIG_FILE="${CONFIG_DIR}/config.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default options
INSTALL_SERVICE=true
UNINSTALL=false
UPDATE_WEBUI_ONLY=false
SERVICE_USER="${SUDO_USER:-$(whoami)}"

# Configuration values (will be set from existing config or prompts)
SERVER_HOST=""
SERVER_PORT=""
MODELS_DIR=""
OUTPUT_DIR=""

# Assistant configuration values
ASSISTANT_ENABLED=""
ASSISTANT_ENDPOINT=""
ASSISTANT_API_KEY=""
ASSISTANT_MODEL=""
ASSISTANT_TEMPERATURE=""
ASSISTANT_MAX_TOKENS=""
ASSISTANT_SYSTEM_PROMPT=""
ASSISTANT_TIMEOUT=""
ASSISTANT_MAX_HISTORY=""
ASSISTANT_PROACTIVE=""

#------------------------------------------------------------------------------
# Helper functions
#------------------------------------------------------------------------------

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

show_help() {
    cat << EOF
SDCPP-RESTAPI Installer

Usage: sudo $0 [options]

Options:
    --uninstall         Remove the installation completely
    --update-webui      Only update the Web UI files (quick update)
    --user USER         Run service as USER (default: $SERVICE_USER)
    --host HOST         Server listen address (default: 0.0.0.0)
    --port PORT         Server port (default: 8080)
    --output-dir DIR    Directory for generated images
    --models-dir DIR    Directory for model files
    --no-service        Don't install systemd service
    --help              Show this help message

If a config file already exists at ${CONFIG_FILE},
values from it will be used as defaults for the prompts.

Examples:
    sudo $0                                    # Install interactively
    sudo $0 --user sdcpp                       # Install as 'sdcpp' user
    sudo $0 --port 9000                        # Use port 9000
    sudo $0 --update-webui                     # Update only Web UI files
    sudo $0 --uninstall                        # Remove installation

EOF
}

# Read value from existing config file using jq
read_config_value() {
    local key="$1"
    local default="$2"

    if [[ -f "${CONFIG_FILE}" ]] && command -v jq &>/dev/null; then
        local value
        value=$(jq -r "$key // empty" "${CONFIG_FILE}" 2>/dev/null)
        if [[ -n "$value" && "$value" != "null" ]]; then
            echo "$value"
            return
        fi
    fi
    echo "$default"
}

# Prompt for a value with default
prompt_value() {
    local prompt_msg="$1"
    local default_val="$2"
    local var_name="$3"
    local result=""

    echo -en "${CYAN}[INPUT]${NC} ${prompt_msg} [${default_val}]: "
    read -r result

    # Use default if empty
    if [[ -z "$result" ]]; then
        result="$default_val"
    fi

    eval "$var_name=\"$result\""
}

# Prompt for directory with validation
prompt_directory() {
    local prompt_msg="$1"
    local default_val="$2"
    local var_name="$3"
    local result=""

    while true; do
        echo -en "${CYAN}[INPUT]${NC} ${prompt_msg} [${default_val}]: "
        read -r result

        # Use default if empty
        if [[ -z "$result" ]]; then
            result="$default_val"
        fi

        # Expand ~ to home directory
        result="${result/#\~/$HOME}"

        # Validate path (must be absolute)
        if [[ "$result" != /* ]]; then
            print_error "Please enter an absolute path (starting with /)"
            continue
        fi

        eval "$var_name=\"$result\""
        break
    done
}

# Prompt for yes/no with default
prompt_yes_no() {
    local prompt_msg="$1"
    local default_val="$2"  # "yes" or "no"
    local var_name="$3"
    local result=""

    local hint
    if [[ "$default_val" == "yes" ]]; then
        hint="Y/n"
    else
        hint="y/N"
    fi

    while true; do
        echo -en "${CYAN}[INPUT]${NC} ${prompt_msg} [${hint}]: "
        read -r result

        # Use default if empty
        if [[ -z "$result" ]]; then
            result="$default_val"
        fi

        # Normalize input
        result=$(echo "$result" | tr '[:upper:]' '[:lower:]')

        case "$result" in
            y|yes)
                eval "$var_name=true"
                return 0
                ;;
            n|no)
                eval "$var_name=false"
                return 0
                ;;
            *)
                print_error "Please enter 'yes' or 'no'"
                ;;
        esac
    done
}

# Prompt for optional value (can be empty)
prompt_optional() {
    local prompt_msg="$1"
    local default_val="$2"
    local var_name="$3"
    local result=""

    if [[ -n "$default_val" ]]; then
        echo -en "${CYAN}[INPUT]${NC} ${prompt_msg} [${default_val}]: "
    else
        echo -en "${CYAN}[INPUT]${NC} ${prompt_msg} (press Enter to skip): "
    fi
    read -r result

    # Use default if empty
    if [[ -z "$result" ]]; then
        result="$default_val"
    fi

    eval "$var_name=\"$result\""
}

#------------------------------------------------------------------------------
# Parse arguments
#------------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case $1 in
        --uninstall)
            UNINSTALL=true
            shift
            ;;
        --update-webui)
            UPDATE_WEBUI_ONLY=true
            shift
            ;;
        --user)
            SERVICE_USER="$2"
            shift 2
            ;;
        --host)
            SERVER_HOST="$2"
            shift 2
            ;;
        --port)
            SERVER_PORT="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --models-dir)
            MODELS_DIR="$2"
            shift 2
            ;;
        --no-service)
            INSTALL_SERVICE=false
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

#------------------------------------------------------------------------------
# Uninstall
#------------------------------------------------------------------------------

do_uninstall() {
    print_info "Uninstalling ${SERVICE_NAME}..."

    # Stop and disable service
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        print_info "Stopping service..."
        systemctl stop "${SERVICE_NAME}"
    fi

    if systemctl is-enabled --quiet "${SERVICE_NAME}" 2>/dev/null; then
        print_info "Disabling service..."
        systemctl disable "${SERVICE_NAME}"
    fi

    # Remove service file
    if [[ -f "${SERVICE_FILE}" ]]; then
        print_info "Removing service file..."
        rm -f "${SERVICE_FILE}"
        systemctl daemon-reload
    fi

    # Remove installation directory
    if [[ -d "${INSTALL_DIR}" ]]; then
        print_info "Removing installation directory..."
        rm -rf "${INSTALL_DIR}"
    fi

    # Note: We don't remove CONFIG_DIR as it may contain user data

    print_info "Uninstallation complete!"
    print_warn "Configuration in ${CONFIG_DIR} was preserved. Remove manually if needed."
}

#------------------------------------------------------------------------------
# Update Web UI Only
#------------------------------------------------------------------------------

do_update_webui() {
    # Find the source directory (where this script is located)
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    SOURCE_DIR="$(dirname "${SCRIPT_DIR}")"

    # Check if installation exists
    if [[ ! -d "${INSTALL_DIR}" ]]; then
        print_error "Installation not found at ${INSTALL_DIR}"
        print_error "Please run a full installation first: sudo $0"
        exit 1
    fi

    # Check if webui source exists
    WEBUI_SOURCE="${SOURCE_DIR}/build/webui"
    if [[ ! -d "${WEBUI_SOURCE}" ]]; then
        print_error "Web UI build not found at ${WEBUI_SOURCE}"
        print_error "Please build the project first:"
        print_error "  cd ${SOURCE_DIR}/build && ninja"
        exit 1
    fi

    WEBUI_DEST="${INSTALL_DIR}/webui"

    print_info "Updating Web UI..."
    print_info "  Source: ${WEBUI_SOURCE}"
    print_info "  Destination: ${WEBUI_DEST}"
    echo

    # Get the owner of the install directory to preserve permissions
    if [[ -d "${WEBUI_DEST}" ]]; then
        WEBUI_OWNER=$(stat -c '%U:%G' "${WEBUI_DEST}")
    else
        WEBUI_OWNER="${SERVICE_USER}:${SERVICE_USER}"
    fi

    # Remove old webui and copy new one
    print_info "Removing old Web UI files..."
    rm -rf "${WEBUI_DEST}"

    print_info "Installing new Web UI files..."
    mkdir -p "${WEBUI_DEST}"
    cp -r "${WEBUI_SOURCE}/"* "${WEBUI_DEST}/"

    # Restore ownership
    print_info "Setting permissions..."
    chown -R "${WEBUI_OWNER}" "${WEBUI_DEST}"

    echo
    print_info "Web UI update complete!"
    echo
    echo "=========================================="
    echo "  Web UI Updated Successfully"
    echo "=========================================="
    echo
    echo "  Location: ${WEBUI_DEST}"
    echo
    echo "  Note: No service restart required."
    echo "        Just refresh your browser to see changes."
    echo
}

#------------------------------------------------------------------------------
# Install
#------------------------------------------------------------------------------

do_install() {
    # Find the source directory (where this script is located)
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    SOURCE_DIR="$(dirname "${SCRIPT_DIR}")"

    # Check if binary exists
    BINARY_PATH="${SOURCE_DIR}/build/bin/sdcpp-restapi"
    if [[ ! -f "${BINARY_PATH}" ]]; then
        # Try alternative path
        BINARY_PATH="${SOURCE_DIR}/bin/sdcpp-restapi"
        if [[ ! -f "${BINARY_PATH}" ]]; then
            print_error "Binary not found. Please build the project first:"
            print_error "  cd ${SOURCE_DIR}/build && ninja"
            exit 1
        fi
    fi

    # Check if user exists
    if ! id "${SERVICE_USER}" &>/dev/null; then
        print_error "User '${SERVICE_USER}' does not exist"
        print_info "Create the user first: sudo useradd -r -s /bin/false ${SERVICE_USER}"
        exit 1
    fi

    # Read existing config values as defaults
    local DEFAULT_HOST DEFAULT_PORT DEFAULT_MODELS DEFAULT_OUTPUT
    local DEFAULT_ASSISTANT_ENABLED DEFAULT_ASSISTANT_ENDPOINT DEFAULT_ASSISTANT_API_KEY
    local DEFAULT_ASSISTANT_MODEL DEFAULT_ASSISTANT_TEMPERATURE DEFAULT_ASSISTANT_MAX_TOKENS
    local DEFAULT_ASSISTANT_SYSTEM_PROMPT DEFAULT_ASSISTANT_TIMEOUT DEFAULT_ASSISTANT_MAX_HISTORY
    local DEFAULT_ASSISTANT_PROACTIVE

    if [[ -f "${CONFIG_FILE}" ]]; then
        print_info "Found existing configuration, using as defaults..."
        DEFAULT_HOST=$(read_config_value '.server.host' '0.0.0.0')
        DEFAULT_PORT=$(read_config_value '.server.port' '8080')
        DEFAULT_MODELS=$(read_config_value '.paths.checkpoints' "${INSTALL_DIR}/models/checkpoints")
        # Get parent directory of checkpoints as models dir
        DEFAULT_MODELS=$(dirname "$DEFAULT_MODELS")
        DEFAULT_OUTPUT=$(read_config_value '.paths.output' "${INSTALL_DIR}/output")

        # Migration: remove deprecated "ollama" config section if present
        if command -v jq &>/dev/null; then
            if jq -e '.ollama' "${CONFIG_FILE}" &>/dev/null; then
                print_info "Migrating config: removing deprecated 'ollama' section..."
                jq 'del(.ollama)' "${CONFIG_FILE}" > "${CONFIG_FILE}.migrated"
                mv "${CONFIG_FILE}.migrated" "${CONFIG_FILE}"
            fi
        fi

        # Read Assistant config
        DEFAULT_ASSISTANT_ENABLED=$(read_config_value '.assistant.enabled' 'false')
        DEFAULT_ASSISTANT_ENDPOINT=$(read_config_value '.assistant.endpoint' 'http://localhost:11434')
        DEFAULT_ASSISTANT_API_KEY=$(read_config_value '.assistant.api_key' '')
        DEFAULT_ASSISTANT_MODEL=$(read_config_value '.assistant.model' 'llama3.2')
        DEFAULT_ASSISTANT_TEMPERATURE=$(read_config_value '.assistant.temperature' '0.7')
        DEFAULT_ASSISTANT_MAX_TOKENS=$(read_config_value '.assistant.max_tokens' '2000')
        DEFAULT_ASSISTANT_SYSTEM_PROMPT=$(read_config_value '.assistant.system_prompt' '')
        DEFAULT_ASSISTANT_TIMEOUT=$(read_config_value '.assistant.timeout_seconds' '120')
        DEFAULT_ASSISTANT_MAX_HISTORY=$(read_config_value '.assistant.max_history_turns' '20')
        DEFAULT_ASSISTANT_PROACTIVE=$(read_config_value '.assistant.proactive_suggestions' 'true')
    else
        DEFAULT_HOST="0.0.0.0"
        DEFAULT_PORT="8080"
        DEFAULT_MODELS="${INSTALL_DIR}/models"
        DEFAULT_OUTPUT="${INSTALL_DIR}/output"

        # Default Assistant config
        DEFAULT_ASSISTANT_ENABLED="false"
        DEFAULT_ASSISTANT_ENDPOINT="http://localhost:11434"
        DEFAULT_ASSISTANT_API_KEY=""
        DEFAULT_ASSISTANT_MODEL="llama3.2"
        DEFAULT_ASSISTANT_TEMPERATURE="0.7"
        DEFAULT_ASSISTANT_MAX_TOKENS="2000"
        DEFAULT_ASSISTANT_SYSTEM_PROMPT=""
        DEFAULT_ASSISTANT_TIMEOUT="120"
        DEFAULT_ASSISTANT_MAX_HISTORY="20"
        DEFAULT_ASSISTANT_PROACTIVE="true"
    fi

    # Prompt for configuration values if not provided via command line
    echo
    print_info "Server Configuration"
    echo "=============================================="
    echo

    if [[ -z "${SERVER_HOST}" ]]; then
        prompt_value "Server listen address" "$DEFAULT_HOST" "SERVER_HOST"
    fi

    if [[ -z "${SERVER_PORT}" ]]; then
        prompt_value "Server port" "$DEFAULT_PORT" "SERVER_PORT"
    fi

    echo
    print_info "Path Configuration"
    echo "=============================================="
    echo

    if [[ -z "${MODELS_DIR}" ]]; then
        prompt_directory "Models base directory" "$DEFAULT_MODELS" "MODELS_DIR"
    fi

    if [[ -z "${OUTPUT_DIR}" ]]; then
        prompt_directory "Output directory (generated images)" "$DEFAULT_OUTPUT" "OUTPUT_DIR"
    fi

    echo
    print_info "Assistant Configuration (AI Helper in WebUI)"
    echo "=============================================="
    echo
    echo "  The Assistant is an LLM-powered helper that can provide suggestions,"
    echo "  help troubleshoot errors, enhance prompts, and adjust generation settings."
    echo

    # Determine default yes/no for assistant enabled
    local assistant_default_yn="no"
    if [[ "$DEFAULT_ASSISTANT_ENABLED" == "true" ]]; then
        assistant_default_yn="yes"
    fi

    prompt_yes_no "Enable Assistant (AI helper in WebUI)?" "$assistant_default_yn" "ASSISTANT_ENABLED"

    if [[ "$ASSISTANT_ENABLED" == "true" ]]; then
        echo
        prompt_value "Assistant API endpoint" "$DEFAULT_ASSISTANT_ENDPOINT" "ASSISTANT_ENDPOINT"
        prompt_optional "API key (for cloud endpoints, leave empty for local)" "$DEFAULT_ASSISTANT_API_KEY" "ASSISTANT_API_KEY"
        prompt_value "Model name" "$DEFAULT_ASSISTANT_MODEL" "ASSISTANT_MODEL"
        prompt_value "Temperature (0.0-1.0)" "$DEFAULT_ASSISTANT_TEMPERATURE" "ASSISTANT_TEMPERATURE"
        prompt_value "Max tokens for response" "$DEFAULT_ASSISTANT_MAX_TOKENS" "ASSISTANT_MAX_TOKENS"
        prompt_value "Request timeout (seconds)" "$DEFAULT_ASSISTANT_TIMEOUT" "ASSISTANT_TIMEOUT"
        prompt_value "Max conversation history turns" "$DEFAULT_ASSISTANT_MAX_HISTORY" "ASSISTANT_MAX_HISTORY"
        # Proactive suggestions
        local proactive_default_yn="yes"
        if [[ "$DEFAULT_ASSISTANT_PROACTIVE" == "false" ]]; then
            proactive_default_yn="no"
        fi
        prompt_yes_no "Enable proactive suggestions?" "$proactive_default_yn" "ASSISTANT_PROACTIVE"
        # System prompt is not prompted - it can be multiline and complex
        ASSISTANT_SYSTEM_PROMPT="$DEFAULT_ASSISTANT_SYSTEM_PROMPT"
        echo
        echo -e "  ${YELLOW}Note:${NC} To customize the assistant system prompt, edit ${CONFIG_FILE}"
        echo "        after installation. A default prompt is provided."
    else
        # Set defaults even if disabled
        ASSISTANT_ENDPOINT="$DEFAULT_ASSISTANT_ENDPOINT"
        ASSISTANT_API_KEY="$DEFAULT_ASSISTANT_API_KEY"
        ASSISTANT_MODEL="$DEFAULT_ASSISTANT_MODEL"
        ASSISTANT_TEMPERATURE="$DEFAULT_ASSISTANT_TEMPERATURE"
        ASSISTANT_MAX_TOKENS="$DEFAULT_ASSISTANT_MAX_TOKENS"
        ASSISTANT_TIMEOUT="$DEFAULT_ASSISTANT_TIMEOUT"
        ASSISTANT_MAX_HISTORY="$DEFAULT_ASSISTANT_MAX_HISTORY"
        ASSISTANT_PROACTIVE="$DEFAULT_ASSISTANT_PROACTIVE"
        ASSISTANT_SYSTEM_PROMPT="$DEFAULT_ASSISTANT_SYSTEM_PROMPT"
    fi

    echo
    print_info "Installing ${SERVICE_NAME}..."
    print_info "  Install directory: ${INSTALL_DIR}"
    print_info "  Config directory:  ${CONFIG_DIR}"
    print_info "  Server:            ${SERVER_HOST}:${SERVER_PORT}"
    print_info "  Models directory:  ${MODELS_DIR}"
    print_info "  Output directory:  ${OUTPUT_DIR}"
    print_info "  Service user:      ${SERVICE_USER}"
    if [[ "$ASSISTANT_ENABLED" == "true" ]]; then
        print_info "  Assistant:         enabled (${ASSISTANT_ENDPOINT})"
    else
        print_info "  Assistant:         disabled"
    fi
    echo

    # Stop service if running (required to update binary)
    if systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
        print_info "Stopping running service..."
        systemctl stop "${SERVICE_NAME}"
        SERVICE_WAS_RUNNING=true
    else
        SERVICE_WAS_RUNNING=false
    fi

    # Create directories
    print_info "Creating directories..."
    mkdir -p "${INSTALL_DIR}/bin"
    mkdir -p "${INSTALL_DIR}/lib"
    mkdir -p "${CONFIG_DIR}"

    # Copy binary
    print_info "Installing binary..."
    cp "${BINARY_PATH}" "${INSTALL_DIR}/bin/"
    chmod 755 "${INSTALL_DIR}/bin/sdcpp-restapi"

    # Copy shared libraries if they exist
    if [[ -d "${SOURCE_DIR}/build/lib" ]]; then
        print_info "Installing libraries..."
        cp -r "${SOURCE_DIR}/build/lib/"* "${INSTALL_DIR}/lib/" 2>/dev/null || true
    fi

    # Copy Web UI files if they exist
    WEBUI_SOURCE="${SOURCE_DIR}/build/webui"
    WEBUI_DEST="${INSTALL_DIR}/webui"
    if [[ -d "${WEBUI_SOURCE}" ]]; then
        print_info "Installing Web UI..."
        mkdir -p "${WEBUI_DEST}"
        cp -r "${WEBUI_SOURCE}/"* "${WEBUI_DEST}/"
    fi

    # Create/update config file
    print_info "Creating configuration..."

    # Convert boolean to JSON boolean
    local ASSISTANT_ENABLED_JSON="false"
    if [[ "$ASSISTANT_ENABLED" == "true" ]]; then
        ASSISTANT_ENABLED_JSON="true"
    fi

    local ASSISTANT_PROACTIVE_JSON="true"
    if [[ "$ASSISTANT_PROACTIVE" == "false" ]]; then
        ASSISTANT_PROACTIVE_JSON="false"
    fi

    # Use jq to properly escape strings for JSON (handles newlines, quotes, etc.)
    local ASSISTANT_SYSTEM_PROMPT_JSON
    local ASSISTANT_API_KEY_JSON
    if command -v jq &>/dev/null; then
        ASSISTANT_SYSTEM_PROMPT_JSON=$(printf '%s' "$ASSISTANT_SYSTEM_PROMPT" | jq -Rs '.')
        ASSISTANT_API_KEY_JSON=$(printf '%s' "$ASSISTANT_API_KEY" | jq -Rs '.')
    else
        # Fallback: basic escaping (won't handle all cases but better than nothing)
        ASSISTANT_SYSTEM_PROMPT_JSON="\"$(printf '%s' "$ASSISTANT_SYSTEM_PROMPT" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr '\n' ' ')\""
        ASSISTANT_API_KEY_JSON="\"${ASSISTANT_API_KEY}\""
    fi

    # If an existing config exists and jq is available, merge settings to preserve
    # user's custom settings (taesd, preview, sd_defaults, etc.)
    if [[ -f "${CONFIG_FILE}" ]] && command -v jq &>/dev/null; then
        print_info "Merging with existing configuration to preserve custom settings..."

        # Create a temporary file with the new settings
        local TEMP_NEW_CONFIG
        TEMP_NEW_CONFIG=$(mktemp)

        cat > "${TEMP_NEW_CONFIG}" << CONFIGEOF
{
    "server": {
        "host": "${SERVER_HOST}",
        "port": ${SERVER_PORT}
    },
    "paths": {
        "checkpoints": "${MODELS_DIR}/checkpoints",
        "diffusion_models": "${MODELS_DIR}/diffusion_models",
        "vae": "${MODELS_DIR}/vae",
        "lora": "${MODELS_DIR}/loras",
        "clip": "${MODELS_DIR}/clip",
        "t5": "${MODELS_DIR}/t5xxl",
        "embeddings": "${MODELS_DIR}/embeddings",
        "controlnet": "${MODELS_DIR}/controlnet",
        "esrgan": "${MODELS_DIR}/esrgan",
        "llm": "${MODELS_DIR}/Text-encoder",
        "output": "${OUTPUT_DIR}"
    },
    "assistant": {
        "enabled": ${ASSISTANT_ENABLED_JSON},
        "endpoint": "${ASSISTANT_ENDPOINT}",
        "api_key": ${ASSISTANT_API_KEY_JSON},
        "model": "${ASSISTANT_MODEL}",
        "temperature": ${ASSISTANT_TEMPERATURE},
        "max_tokens": ${ASSISTANT_MAX_TOKENS},
        "system_prompt": ${ASSISTANT_SYSTEM_PROMPT_JSON},
        "timeout_seconds": ${ASSISTANT_TIMEOUT},
        "max_history_turns": ${ASSISTANT_MAX_HISTORY},
        "proactive_suggestions": ${ASSISTANT_PROACTIVE_JSON}
    }
}
CONFIGEOF

        # Deep merge: existing config as base, new config values override
        # This preserves: server.threads, paths.taesd, paths.webui, sd_defaults, preview, etc.
        jq -s '
            # Store originals for reference
            (.[0]) as $old |
            (.[1]) as $new |
            # Start with deep merge (new overrides old)
            ($old * $new) |
            # Restore values that should be preserved from old config
            .server.threads = ($old.server.threads // .server.threads // 8) |
            .paths.taesd = ($old.paths.taesd // .paths.taesd) |
            .paths.esrgan = ($old.paths.esrgan // .paths.esrgan) |
            .paths.webui = ($old.paths.webui // .paths.webui // "") |
            .sd_defaults = ($old.sd_defaults // .sd_defaults // {}) |
            .preview = ($old.preview // .preview // {}) |
            # Remove deprecated ollama config if present
            del(.ollama)
        ' "${CONFIG_FILE}" "${TEMP_NEW_CONFIG}" > "${CONFIG_FILE}.new"

        mv "${CONFIG_FILE}.new" "${CONFIG_FILE}"
        rm -f "${TEMP_NEW_CONFIG}"
    else
        # No existing config or no jq - create fresh config with defaults
        cat > "${CONFIG_FILE}" << CONFIGEOF
{
    "server": {
        "host": "${SERVER_HOST}",
        "port": ${SERVER_PORT},
        "threads": 8
    },
    "paths": {
        "checkpoints": "${MODELS_DIR}/checkpoints",
        "diffusion_models": "${MODELS_DIR}/diffusion_models",
        "vae": "${MODELS_DIR}/vae",
        "lora": "${MODELS_DIR}/loras",
        "clip": "${MODELS_DIR}/clip",
        "t5": "${MODELS_DIR}/t5xxl",
        "embeddings": "${MODELS_DIR}/embeddings",
        "controlnet": "${MODELS_DIR}/controlnet",
        "esrgan": "${MODELS_DIR}/esrgan",
        "llm": "${MODELS_DIR}/Text-encoder",
        "taesd": "${MODELS_DIR}/taesd",
        "output": "${OUTPUT_DIR}",
        "webui": ""
    },
    "sd_defaults": {
        "n_threads": 10,
        "keep_clip_on_cpu": false,
        "keep_vae_on_cpu": false,
        "flash_attn": true,
        "offload_to_cpu": false
    },
    "assistant": {
        "enabled": ${ASSISTANT_ENABLED_JSON},
        "endpoint": "${ASSISTANT_ENDPOINT}",
        "api_key": ${ASSISTANT_API_KEY_JSON},
        "model": "${ASSISTANT_MODEL}",
        "temperature": ${ASSISTANT_TEMPERATURE},
        "max_tokens": ${ASSISTANT_MAX_TOKENS},
        "system_prompt": ${ASSISTANT_SYSTEM_PROMPT_JSON},
        "timeout_seconds": ${ASSISTANT_TIMEOUT},
        "max_history_turns": ${ASSISTANT_MAX_HISTORY},
        "proactive_suggestions": ${ASSISTANT_PROACTIVE_JSON}
    },
    "preview": {
        "enabled": true,
        "mode": "tae",
        "interval": 1,
        "max_size": 256,
        "quality": 75
    }
}
CONFIGEOF
    fi

    # Create model subdirectories
    print_info "Creating model subdirectories..."
    mkdir -p "${MODELS_DIR}"/{checkpoints,diffusion_models,vae,loras,clip,t5xxl,embeddings,controlnet,esrgan,Text-encoder,taesd}
    mkdir -p "${OUTPUT_DIR}"

    # Set ownership
    print_info "Setting permissions..."
    chown -R "${SERVICE_USER}:${SERVICE_USER}" "${INSTALL_DIR}"
    chown -R "${SERVICE_USER}:${SERVICE_USER}" "${CONFIG_DIR}"

    # Set ownership on custom directories if different from install dir
    if [[ "${MODELS_DIR}" != "${INSTALL_DIR}/models" ]]; then
        if [[ -d "${MODELS_DIR}" ]]; then
            print_info "Setting permissions on models directory..."
            chown -R "${SERVICE_USER}:${SERVICE_USER}" "${MODELS_DIR}"
        fi
    fi
    if [[ "${OUTPUT_DIR}" != "${INSTALL_DIR}/output" ]]; then
        if [[ -d "${OUTPUT_DIR}" ]]; then
            print_info "Setting permissions on output directory..."
            chown -R "${SERVICE_USER}:${SERVICE_USER}" "${OUTPUT_DIR}"
        fi
    fi

    # Install systemd service
    if [[ "${INSTALL_SERVICE}" == true ]]; then
        print_info "Installing systemd service..."
        cat > "${SERVICE_FILE}" << SERVICEEOF
[Unit]
Description=SDCPP REST API Server
Documentation=https://github.com/anthropics/sdcpp-restapi
After=network.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_USER}
WorkingDirectory=${INSTALL_DIR}
ExecStart=${INSTALL_DIR}/bin/sdcpp-restapi --config ${CONFIG_FILE}
Restart=on-failure
RestartSec=10

# Security hardening
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadWritePaths=${OUTPUT_DIR} ${CONFIG_DIR}
ReadOnlyPaths=${MODELS_DIR} ${INSTALL_DIR}/webui

# Resource limits (adjust as needed)
# LimitNOFILE=65535
# MemoryMax=32G

# Environment
Environment=LD_LIBRARY_PATH=${INSTALL_DIR}/lib
Environment=SDCPP_WEBUI_PATH=${INSTALL_DIR}/webui

[Install]
WantedBy=multi-user.target
SERVICEEOF

        # Reload systemd
        print_info "Reloading systemd..."
        systemctl daemon-reload

        print_info "Enabling service..."
        systemctl enable "${SERVICE_NAME}"
    fi

    # Print summary
    echo
    print_info "Installation complete!"
    echo
    echo "=========================================="
    echo "  SDCPP-RESTAPI Installation Summary"
    echo "=========================================="
    echo
    echo "  Binary:      ${INSTALL_DIR}/bin/sdcpp-restapi"
    echo "  Config:      ${CONFIG_FILE}"
    echo "  Server:      http://${SERVER_HOST}:${SERVER_PORT}"
    if [[ -d "${WEBUI_DEST}" ]]; then
        echo "  Web UI:      http://${SERVER_HOST}:${SERVER_PORT}/ui/"
    fi
    echo "  Models:      ${MODELS_DIR}/"
    echo "  Output:      ${OUTPUT_DIR}/"
    echo "  Service:     ${SERVICE_USER}"
    if [[ "$ASSISTANT_ENABLED" == "true" ]]; then
        echo "  Assistant:   enabled (${ASSISTANT_ENDPOINT}, model: ${ASSISTANT_MODEL})"
    else
        echo "  Assistant:   disabled"
    fi
    echo
    echo "  Model subdirectories created:"
    echo "    - checkpoints/       (SD1.x, SD2.x, SDXL models)"
    echo "    - diffusion_models/  (Flux, SD3, Wan models)"
    echo "    - vae/"
    echo "    - loras/"
    echo "    - clip/"
    echo "    - t5xxl/"
    echo "    - embeddings/"
    echo "    - controlnet/"
    echo "    - esrgan/"
    echo "    - taesd/             (Tiny AutoEncoder for previews)"
    echo "    - Text-encoder/"
    echo
    if [[ "${INSTALL_SERVICE}" == true ]]; then
        echo "  Service commands:"
        echo "    sudo systemctl start ${SERVICE_NAME}    # Start the service"
        echo "    sudo systemctl stop ${SERVICE_NAME}     # Stop the service"
        echo "    sudo systemctl status ${SERVICE_NAME}   # Check status"
        echo "    sudo journalctl -u ${SERVICE_NAME} -f   # View logs"
        echo
    fi

    # Restart service if it was running before update
    if [[ "${SERVICE_WAS_RUNNING}" == true ]]; then
        print_info "Restarting service..."
        systemctl start "${SERVICE_NAME}"
        echo
        echo "  Service has been restarted automatically."
    else
        echo "  Next steps:"
        echo "    1. Add your models to ${MODELS_DIR}/"
        echo "    2. Start the service: sudo systemctl start ${SERVICE_NAME}"
    fi
    echo
}

#------------------------------------------------------------------------------
# Main
#------------------------------------------------------------------------------

check_root

if [[ "${UNINSTALL}" == true ]]; then
    do_uninstall
elif [[ "${UPDATE_WEBUI_ONLY}" == true ]]; then
    do_update_webui
else
    do_install
fi
