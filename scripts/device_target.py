"""Pin one HDC device for the entire test; never switch targets silently."""
import os
import subprocess

HDC = '/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc'

def resolve_target():
    targets = subprocess.check_output([HDC, 'list', 'targets'], text=True, timeout=8)
    choices = [x.strip() for x in targets.splitlines() if x.strip() and x.strip() != '[Empty]']
    requested = os.environ.get('QISCREEN_DEVICE_TARGET')
    if requested:
        if requested not in choices:
            raise RuntimeError('The selected HDC device is no longer connected')
        return requested
    if len(choices) != 1:
        raise RuntimeError('Connect one device or set QISCREEN_DEVICE_TARGET explicitly')
    return choices[0]
