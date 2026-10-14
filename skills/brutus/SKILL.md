---
name: brutus
description: Diagnose sustained macOS freezes, memory pressure, or resource exhaustion during agent work, and target an offending subprocess with Brutus. Use when the whole Mac is struggling, rather than a single slow command.
---

# Brutus

Keep unrelated agent jobs running by targeting a specific subprocess. Brutus is a small native macOS CLI; it samples on demand, with no background monitor.

1. Run `brutus --json` for a read-only, three-second sample and the top three candidates. Use this instead of the interactive menu in an agent harness.
2. Read pressure, active paging, RAM, swap, SSD space left, CPU, and process count together. Compare candidates' `swap_in_rate` and `memory_growth_rate`; null swap data means unavailable, not zero. Swap-ins identify readbacks, not who forced other pages out. A large existing swap total or the largest process alone doesn't establish the cause. If the Mac has recovered, leave processes running.
3. Run `brutus inspect PID` on a candidate. Use its command, working folder, and parent PID to identify the job. Refresh if time has passed; PIDs can be reused. Command arguments may contain secrets, so keep them local or redact them.
4. Within existing authorization to stop that job, run `brutus close PID` for the specific offending helper. Without that authorization, show the candidate and resource evidence to the user first. Preserve the harness, parent app, and unrelated work; don't use `pkill` or kill a process tree. Use `--force` only for an authorized target that survives TERM, rather than making it the default.
5. Summarize Brutus's before/after resource report and whether the PID stopped. Private JSON reports live in `~/.local/state/brutus/`. Recovery belongs to the surrounding agent or app; it isn't guaranteed, and swap may stay allocated after pressure improves. Don't repeatedly resample a struggling Mac without a reason.

`brutus close PID --dry-run` previews a close without signals. If Brutus is missing, use the [installation instructions](https://github.com/smashah/brutus#installation).
