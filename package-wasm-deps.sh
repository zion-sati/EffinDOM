#!/usr/bin/env bash
# Packages the verified browser dependency SDK for EffinDOM-Native-Deps.
#
# Usage:
#   ./package-wasm-deps.sh
#   ./package-wasm-deps.sh --skip-build
#   ./package-wasm-deps.sh --output-repo /path/to/EffinDOM-Native-Deps
#
# The default output repository is a sibling EffinDOM-Native-Deps checkout.
# In the source monorepo, the local ../zion-sati/EffinDOM-Native-Deps checkout
# is used when that direct sibling is not present.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec node "${REPO_ROOT}/scripts/wasm-deps/package.mjs" "$@"
