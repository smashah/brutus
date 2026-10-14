# Measured footprint

These measurements were taken on 2026-10-14 from the development build used before the public release packaging was added.

| Item | Observation |
| --- | --- |
| Machine | MacBookPro18,3; Apple silicon; 8 logical cores; 16 GiB RAM |
| macOS | 26.6.2, build 25G83 |
| Native binary | 73,192 bytes / 71.5 KiB |
| Binary SHA-256 | `0e21d211701187a54387e92c55b9f9232f696b6e7da47af649111a4e1da00f0c` |
| Idle physical footprint | About 2.1 MiB |
| Peak physical footprint | 3,326,312 bytes / 3.2 MiB |
| Peak resident memory | 6,979,584 bytes / 6.7 MiB, including resident mapped/shared pages |
| Initial check CPU time | 65.9 ms over approximately 3.1 seconds |
| Refresh CPU time | 37.3 ms over approximately 3.0 seconds |
| Five-second idle observation | Zero CPU-time increase, zero interrupt wakeups, zero disk I/O increase |
| Disk writes observed | Zero during checking, refresh, and idle measurements |

The running command was `brutus --dry-run`, including the interactive menu, a refresh, and the info view. `/usr/bin/time -l` recorded final peak resident memory and physical footprint. Native `proc_pid_rusage(RUSAGE_INFO_V4)` readings recorded current memory, peak footprint, CPU counters, wakeups, and process-attributed I/O. The cumulative CPU counters were converted with `mach_timebase_info`; idle observations compared counters before and after waiting.

The initial check and refresh used about 1.2–2.1% of one core averaged over their sample windows, or roughly 0.15–0.27% of this Mac's eight-core capacity. Individual short bursts can be higher. A universal distribution binary contains two architecture slices, so its file size is larger than the native development binary above.

These describe one real session on a pressured Mac. Process count, argument length, OS version, architecture, and workload affect the result. At the time of these measurements, closing a process also wrote the private before/after report, so the zero-write observation does not cover a close operation. Saving reports now requires `--save-report` and is off by default.
