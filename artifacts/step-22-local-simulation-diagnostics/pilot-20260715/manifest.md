# Step-22 local simulation diagnostics manifest

- Tier: `pilot`
- Source revision at launch: `3a0c7f70c4413d69cdfe0d5189ea4f950bac5658`
- Source worktree dirty at launch: `no`
- Simulator SHA-256 at launch: `d00398906c146e9f0b86011c40e1a7d3186f667fd65a9fcbcfb545bd1b5cd9c4`
- Run start (UTC): `2026-07-15T14:41:30Z`
- Python/platform: `3.10.12` / `Linux-6.6.87.2-microsoft-standard-WSL2-x86_64-with-glibc2.35`
- Profiles: `21`
- Raw rows: `1680`
- Warmup cohorts: `[500]`
- Measurement cohorts: `[5000]`
- Seeds: `[11, 23, 37, 47, 59]`
- Anchor points: `[('W2', 0.85), ('W3', 0.7), ('W3', 0.85), ('W3', 0.9)]`

## Evidence boundary

- This directory is a post-freeze diagnostic artifact, not Step-21 evidence.
- Flow-affine placement is an explicit simulator model, not measured RSS behavior.
- Configured control costs are accounting-only and do not advance simulated time.
- Bootstrap intervals are descriptive and do not rerun the corrected paper gate.
- No profile in this matrix establishes physical overhead or deployability.
