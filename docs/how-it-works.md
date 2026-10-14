# How Brutus reads your Mac

Brutus reads an initial snapshot followed by three snapshots about one second apart. Only processes observed across all three intervals can become candidates. It averages CPU time and takes the median memory footprint, which reduces the influence of a brief spike.

## The counters

- **Process CPU and memory:** libproc `proc_pid_rusage`, including physical footprint, resident memory, and cumulative CPU time. CPU ticks are converted with the Mach timebase.
- **Per-process swap:** a read-only task name port supplies `TASK_VM_INFO`'s `ledger_swapins` and compressed-memory accounting. Brutus releases each port immediately, computes swap-in bytes/second over three intervals, and reports unavailable counters as unknown.
- **Identity:** libproc BSD process information supplies owner, parent PID, and native start seconds/microseconds.
- **System CPU and VM:** Mach host counters plus sysctl memory pressure, RAM capacity, swap usage, and core count.
- **Storage:** IOKit block-storage byte counters provide read/write rates. `statfs` on the Data volume provides space remaining.
- **Launch commands:** `KERN_PROCARGS2` supplies argc and argument strings. Brutus stops after argc, excluding environment data from the display, and quotes arguments so spaces and control characters remain understandable.

Commands are fetched only for the few offered candidates. Full commands stay in memory for the current selection and are freed on refresh or exit. The info view stores only the visible lines while allowing the rest of a long command to be scrolled.

## Ranking

For each eligible PID:

```text
score = median footprint / installed RAM
      + CPU weight × average CPU / (100 × logical cores)
      + pressure-only swap-in rate / (32 MiB/s)
      + pressure-only positive footprint growth / (64 MiB/s)
```

CPU weight is 0.25 when memory pressure is elevated or active swap transfers reach 32 MiB/s; otherwise it is 1. The maximum list is three processes, and candidates with a score below 0.01 are omitted. The list may contain fewer than three when too few eligible processes stand out.

The two pressure-only terms apply under elevated memory pressure or at least 32 MiB/s of swap transfers. Swap-in rates require three valid intervals; footprint growth is the net byte change divided by the sample duration. These weights prioritize swap readers and new memory demand alongside existing footprint and CPU use.

**Swap in** measures the process reclaiming previously swapped pages, in logical bytes before compression. It does not measure its total SSD swap allocation or identify which process forced other pages out. Compressed-memory accounting also uses logical bytes and can include pages held in compressed RAM or on disk. These are evidence for choosing a candidate, rather than proof of blame. See Apple's [task VM counters](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/task_info.h), [swap-in ledger](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/task.c), and [compressor accounting](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/vm/vm_compressor.c).

The display's processor percentage is a share of the whole Mac. The info view also exposes CPU as a share of one core, which can exceed 100% for a multithreaded process.

## Traffic lights

| Level | Any qualifying condition |
| --- | --- |
| Amber | Warning memory pressure; ≥32 MiB/s swap transfers; ≥85% CPU busy; SSD space <20 GiB |
| Red | Critical pressure; ≥95% CPU busy; SSD space <5 GiB; or ≥128 MiB/s swap transfers together with warning pressure or ≥30% kernel CPU |

“Thrashing likely” means at least 32 MiB/s of swap transfers alongside elevated memory pressure or kernel CPU. These thresholds are heuristics. High occupied RAM or a large existing swap allocation alone does not prove active thrashing.

The screen uses “GB” and “MB” for the familiar labels; quantities are computed using binary GiB and MiB. “Memory in use” is RAM minus the kernel free-page count, so it includes cache and reclaimable pages and is not the same as Activity Monitor's memory-used figure.

## Closing a process

Brutus excludes processes owned by other users, detected main GUI app processes, and its own ancestors. Main-app detection is a heuristic based on launch parent, app executable path, and selected app names; it is not a system-wide promise to recognize every app configuration.

An interactive selection carries its owner and start timestamp into the close operation. Brutus rechecks both immediately before sending `SIGTERM`. With `--force`, it waits three seconds and sends `SIGKILL` only if the same selected process remains alive. It never signals a group, walks descendants to close them, or uses `pkill`.

Direct `brutus close PID` commands take a fresh snapshot of the PID supplied; they do not remember an identity from an earlier invocation. The command will refuse a target identified as protected in that snapshot.

Before and after snapshots measure the resulting system state. Memory and swap may take time to settle, and another app can restart its helper. The terminal summary is always shown. Saving a JSON report requires `--save-report` and is off by default. The file records before/after metrics, target PIDs, observed survivors, skipped identities, errors, and sent signals. Report files are created with mode 0600 under `~/.local/state/brutus/`; command arguments are not stored. Saved reports have no automatic expiry.

## What Brutus doesn't do

Brutus measures per-process swap readbacks, but does not trace the cause of every system swap-out. It correlates system pressure with sustained per-process resource usage to offer candidates. It does not delete files or caches, manage launch services, prevent helpers from restarting, or run in the background after it exits.
