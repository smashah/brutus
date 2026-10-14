First public release of Brutus, a small native macOS CLI for finding and closing individual processes that may be slowing the machine.

- Three-interval sampling with up to three candidates by default.
- Color-coded pressure, RAM, CPU, swap, active transfers, and SSD headroom.
- Exact PID selection, launch commands, and scrollable process/system details.
- In-place refresh; preview mode; optional force close.
- Before/after system state and private JSON reports after a close request.
- Universal Apple silicon / Intel binary targeting macOS 12+, plus source and SHA-256 files.
- MIT license; no interpreter, helper processes, daemon, or telemetry.

Try the menu without sending signals:

```sh
brutus --dry-run
```

Runtime use and footprint observations are from Apple silicon. The Intel slice is included but has not been exercised on an Intel machine. Pressure/ranking thresholds and main-app detection are heuristics; helper recovery is controlled by its containing app. The binary is ad-hoc signed, not Developer ID signed or notarized.
