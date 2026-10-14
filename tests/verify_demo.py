"""Validate actual host-demo output; no external Python packages required."""
import json
import subprocess
import sys
run = subprocess.run([sys.argv[1]], text=True, capture_output=True, check=True)
frames = [json.loads(line) for line in run.stdout.splitlines()]
states = {frame['state'] for frame in frames}
assert {'NORMAL', 'WARNING', 'FAULT_LATCHED', 'RECOVERY_READY'} <= states
for frame in frames:
    if frame['state'] in ('FAULT_LATCHED', 'RECOVERY_READY'):
        assert not frame['motor'] and frame['alarm']
    if frame['motor']:
        assert frame['valid'] and not frame['active_faults'] and not frame['latched_faults']
assert any(frame['active_faults'] & 8 for frame in frames), 'Missing freshness fault'
assert any(frame['active_faults'] & 2 for frame in frames), 'Missing vibration fault'
assert any(frame['state'] == 'NORMAL' and frame['uptime_ms'] > 6000 for frame in frames), 'Missing recovery'
assert all(not frame['mqtt'] for frame in frames), 'Walkthrough should be offline'
print(f'Validated {len(frames)} JSON frames and offline fault/recovery walkthrough')
