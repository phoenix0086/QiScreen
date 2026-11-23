#!/usr/bin/env python3
"""Cross-build the pinned OpenSSL source with the local HarmonyOS NDK."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
studio = Path(os.environ.get('DEVECO_HOME', '/Applications/DevEco-Studio.app/Contents'))
ndk = studio / 'sdk/default/openharmony/native'
source = root / 'third_party/openssl'
out = root / '.native-deps/openssl'
out.mkdir(parents=True, exist_ok=True)
if not (source / 'Configure').is_file():
    raise SystemExit('Missing third_party/openssl; fetch the pinned openssl-3.5.9 sources first.')
env = os.environ.copy()
env['CC'] = str(ndk / 'llvm/bin/clang')
env['AR'] = str(ndk / 'llvm/bin/llvm-ar')
env['RANLIB'] = str(ndk / 'llvm/bin/llvm-ranlib')
env['CFLAGS'] = f'--target=aarch64-linux-ohos --sysroot={ndk / "sysroot"} -fPIC -O2'
env['LDFLAGS'] = f'--target=aarch64-linux-ohos --sysroot={ndk / "sysroot"}'
if not (out / 'Makefile').exists():
    subprocess.run(['perl', str(source / 'Configure'), 'linux-aarch64', 'no-shared', 'no-tests',
                    'no-apps', 'no-docs', 'no-module', 'no-dso', 'no-engine', 'no-asm'],
                   cwd=out, env=env, check=True)
subprocess.run(['make', '-j8', 'build_libs'], cwd=out, env=env, check=True)
print(f'Native crypto dependency ready: {out / "libcrypto.a"}')
