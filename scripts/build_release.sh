#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

release_mode="${1:-signed}"
if [[ "$release_mode" != signed && "$release_mode" != --unsigned ]]; then
  echo 'Usage: bash scripts/build_release.sh [--unsigned]' >&2
  exit 1
fi

export DEVECO_HOME="${DEVECO_HOME:-/Applications/DevEco-Studio.app/Contents}"
export NODE_HOME="$DEVECO_HOME/tools/node"
export JAVA_HOME="$DEVECO_HOME/jbr/Contents/Home"
export DEVECO_SDK_HOME="$DEVECO_HOME/sdk"
export PATH="$NODE_HOME/bin:$DEVECO_HOME/tools/ohpm/bin:$PATH"

# Fail before building a store submission with a debugging profile.
if [[ "$release_mode" == signed ]]; then
  python3 - <<'PY'
import json
import subprocess

with open('build-profile.json5') as stream:
    config = json.load(stream)['app']
product = next(item for item in config['products'] if item['name'] == 'default')
signing_name = product.get('signingConfig')
signing = next((item for item in config.get('signingConfigs', [])
                if item['name'] == signing_name), None)
if not signing:
    raise SystemExit('Configure the default product with a release signing profile first.')
profile = signing['material']['profile']
result = subprocess.run(['openssl', 'cms', '-verify', '-inform', 'DER',
                         '-in', profile, '-noverify'], capture_output=True, check=True)
data = json.loads(result.stdout)
if data.get('type') != 'release':
    raise SystemExit('The configured profile is for debugging. A release profile is required.')
with open('AppScope/app.json5') as stream:
    bundle = json.load(stream)['app']['bundleName']
if data.get('bundle-info', {}).get('bundle-name') != bundle:
    raise SystemExit('The release profile bundle name does not match this application.')
PY
fi

if [[ ! -f .native-deps/openssl/libcrypto.a ]]; then
  python3 scripts/build_native_deps.py
fi
"$DEVECO_HOME/tools/ohpm/bin/ohpm" install --all
"$DEVECO_HOME/tools/hvigor/bin/hvigorw" --mode project -p product=default \
  -p buildMode=release assembleApp --no-daemon

python3 - "$release_mode" <<'PY'
import io
import json
from pathlib import Path
import shutil
import sys
from zipfile import ZipFile

unsigned = sys.argv[1] == '--unsigned'
suffix = 'unsigned' if unsigned else 'signed'
source = Path('build/outputs/default') / f'{Path.cwd().name}-default-{suffix}.app'
with ZipFile(source) as package:
    with ZipFile(io.BytesIO(package.read('entry-default.hap'))) as module:
        app = json.loads(module.read('module.json'))['app']
        if app.get('debug') is not False:
            raise SystemExit('The output module is not a Release build.')
target = Path('dist/release') / f'QiScreen-{app["versionName"]}-{suffix}.app'
target.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, target)
print(target.resolve())
if unsigned:
    print('Release build prepared. Apply a release signing profile before submission.')
PY
