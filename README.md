# Brutus

**Find the processes slowing your Mac. Close the helper you choose.**

Brutus samples your Mac for about three seconds, shows the top three individual processes, and waits for your choice. RAM, swap, SSD headroom, and active memory transfers stay visible. Press **I** for the full launch command and technical details.

![Brutus showing memory pressure, swap and SSD space, three process choices, and keyboard controls](assets/readme/brutus.png)

*Captured from the running CLI in preview mode. Local project names and paths are redacted; resource values are real.*

## Try it

macOS only. The release binary contains Apple silicon and Intel slices and targets macOS 12 or newer. Runtime measurements were taken on Apple silicon; an Intel machine has not been exercised yet.

Download the installer so you can read it before running it:

```sh
curl -fsSL https://raw.githubusercontent.com/smashah/brutus/main/install.sh -o install-brutus.sh
sh install-brutus.sh
~/.local/bin/brutus --dry-run
```

The installer checks the release archive's SHA-256 and installs into `~/.local/bin` without sudo. Preview mode lets you try every menu action without closing anything. Run `brutus` when you're ready to close a process. Set `BRUTUS_INSTALL_DIR` to choose another directory.

Prefer Homebrew?

```sh
brew tap smashah/brutus https://github.com/smashah/brutus
brew install smashah/brutus/brutus
```

Homebrew may ask you to trust the formula. The binary is ad-hoc signed, not Developer ID signed or notarized. You can also [build from source](#build-from-source).

## Choose, inspect, close

| Key | Action |
| --- | --- |
| **↑ / ↓** | Choose an individual process |
| **Enter** | Close the selected process; preview it in `--dry-run` |
| **I** | Show its full command, resource samples, paths, and system details |
| **R** | Take a new sample in the same screen |
| **M** | More commands, including closing all shown processes |
| **Q** | Quit without closing anything |

In the info view, arrows scroll and **I** or **Esc** returns to the list. More commands can show one or three candidates, refresh, or force the selected helper to close.

```sh
brutus                          # Top three, sampled over three intervals
brutus 1                        # Only the top candidate
brutus --dry-run                # Try the menu without sending signals
brutus scan                     # Print a report and exit
brutus inspect 12345            # Details for a specific PID
brutus close 12345              # Close only this PID
brutus close 12345 --force      # TERM, then KILL if it remains after 3 seconds
brutus --json                   # Read-only JSON report
brutus --version
```

## What the lights mean

- **Green:** No heavy pressure detected. Closing a helper is optional.
- **Amber:** Memory is tight, transfers or CPU use are elevated, or SSD space is below 20 GiB.
- **Red:** Critical pressure, heavy sustained swapping, CPU nearly full, or less than 5 GiB of SSD space.

Swap used and SSD free space are shown together. Existing swap has already consumed disk space; “SSD free” is the space that remains for files, apps, and further swap growth. A large swap total alone does not make the status red.

The detailed view includes swap in/out, compression, disk throughput, kernel CPU, load averages, total processes, and the three per-process samples. [How the ranking and thresholds work →](docs/how-it-works.md)

## One process at a time

Brutus sends signals only to the PIDs you choose, never to a process group or application tree. It excludes other users' processes, detected main GUI processes, and its own ancestors. Before each signal, it checks the process's owner and native start timestamp again.

After a close request, it measures the Mac again and reports before/after pressure, memory, swap, transfers, CPU, process count, and SSD space. A private JSON report is saved under `~/.local/state/brutus/`; launch commands are not saved in these reports. If a process remains, Brutus says so and shows how to force that PID to close.

The candidates are leads, not proof of which process caused a freeze. Closing a helper can interrupt that helper's work or window, and its app decides whether to restart it. Brutus does not delete files, clear caches, disable services, or promise instant swap reclamation.

## Small on purpose

One native C process reads macOS counters directly through libproc, Mach, sysctl, and IOKit. There is no interpreter, daemon, telemetry, or helper command. The menu blocks on keyboard input while idle.

The development build measured roughly **2.1 MiB idle / 3.2 MiB peak** physical memory footprint, with **37–66 ms of CPU work** per three-second check. Idle CPU was unmeasurable during a five-second observation. [Measurement details and build identity →](docs/footprint.md)

## Build from source

Requires Apple's Command Line Tools (`xcode-select --install`) and `make`.

```sh
git clone https://github.com/smashah/brutus.git
cd brutus
make
make install
```

Default installation: `~/.local/bin/brutus`. To choose another prefix, use `make install PREFIX=/your/prefix`. To remove it, use the matching `make uninstall PREFIX=/your/prefix`. These commands leave saved reports alone.

## Contributing

For a bug report, include `brutus --version`, macOS version, CPU architecture, the visible symptom, and a screenshot or redacted output. Don't include a full command line if it contains private arguments. Improvements to process labeling, pressure heuristics, and accessibility are welcome.

## License

[MIT](LICENSE).
