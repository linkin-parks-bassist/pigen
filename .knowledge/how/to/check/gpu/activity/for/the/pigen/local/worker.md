---
status: "unverified"
created_at: "2026-09-14T19:25:23+10:00"
scope: "local"
source: "Codex /root authorized host sysfs GPU samples 2026-09-14; host ps/cwd checks; existing local worker monitoring leaf"
---

On the Linux AMD host, sample /sys/class/drm/card[0-9]/device/gpu_busy_percent repeatedly from an authorized host-visible command. The Codex sandbox process namespace cannot establish host worker liveness. A running OpenCode PID is not evidence of active GPU inference. On 2026-09-14 at 19:25:04, 19:25:06 and 19:25:08 Australia/Sydney, card1 reported 0 percent GPU busy in all three samples. This establishes idle GPU samples, not task completion or exclusive attribution to Qwen. If inference activity remains uncertain, inspect the exact existing OpenCode session and backend metrics without launching a second worker. The general host procedure belongs in personal tooling knowledge; this leaf records the Pigen status-check context because global writes are outside current authority.
