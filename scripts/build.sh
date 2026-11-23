#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
PROJECT_ROOT="$PWD"
mkdir -p dist
export DEVECO_HOME="${DEVECO_HOME:-/Applications/DevEco-Studio.app/Contents}"
export NODE_HOME="$DEVECO_HOME/tools/node"
export JAVA_HOME="$DEVECO_HOME/jbr/Contents/Home"
export DEVECO_SDK_HOME="$DEVECO_HOME/sdk"
export PATH="$NODE_HOME/bin:$DEVECO_HOME/tools/ohpm/bin:$PATH"
if [ ! -f .native-deps/openssl/libcrypto.a ]; then
  python3 scripts/build_native_deps.py
fi
"$DEVECO_HOME/tools/ohpm/bin/ohpm" install --all
"$DEVECO_HOME/tools/hvigor/bin/hvigorw" --mode module -p product=default -p module=entry@default -p buildMode=debug assembleHap --no-daemon
find entry/build/default/outputs -name '*.hap' -exec cp {} "$PROJECT_ROOT/dist/" \;
