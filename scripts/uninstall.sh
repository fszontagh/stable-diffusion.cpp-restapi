#!/bin/bash
#
# SDCPP-RESTAPI Uninstaller
#
# Convenience wrapper for: install.sh --uninstall
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/install.sh" --uninstall "$@"
