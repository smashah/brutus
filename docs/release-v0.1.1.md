Brutus 0.1.1 replaces the selection arrow with 🗡️ in the process list and More commands menu, with matching spacing and box alignment.

Interactive closes play a short dagger sweep, red particle burst, and GAME OVER banner after the target stops. `--dry-run` previews the animation without signals; `--no-animation` skips it. The resource report now includes the net change in free RAM, CPU percentage points, and total processes alongside the before/after values.

Each candidate now shows its sampled Swap in rate. Under pressure, the ranking considers actual per-process swap readbacks and growing memory demand. The info view exposes compressed-memory accounting and the distinction between swap readbacks and the cause of system swap-outs. The JSON report includes these counters, with unavailable values represented as null.

The README now includes the Paper Mono artwork, simpler installation instructions, and a short agent skill. Install with the existing curl command to get this release.
