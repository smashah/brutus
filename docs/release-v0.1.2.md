Brutus now keeps report files off by default. Closing a process still shows the before/after resource summary in your terminal, without creating a JSON file.

If you want a saved copy, use `brutus close 12345 --save-report`, or start the interactive menu with `brutus --save-report`. Private JSON files go to `~/.local/state/brutus/` and remain until you delete them. Existing reports are left in place. `--json` continues to print a read-only sample to stdout without saving a file.

The installer, Homebrew formula, documentation, and agent skill have been updated for 0.1.2. Install with the existing curl command to get this release.
