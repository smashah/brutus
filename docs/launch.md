# Show HN launch brief

Publication target: `https://github.com/smashah/brutus`

This is a fact sheet and release handoff. Write the HN title and opening comment in your own words: [HN asks for human-written text and no automated posting](https://news.ycombinator.com/newsguidelines.html). [Show HN expects a working thing people can try](https://news.ycombinator.com/showhn.html), so publish the repository and downloadable release before submitting the link.

## Facts for your introduction

- The trigger was a 16 GiB Mac seizing up under memory pressure, large swap usage, and many development/helper processes.
- The goal was to find and close one specific helper, while giving the containing app a chance to recover.
- Three intervals are sampled; the default offers the top three individual PIDs rather than a single CPU spike or a whole app tree.
- The main screen shows RAM, active swapping, swap used, SSD space remaining, CPU, the candidates, and their launch commands.
- The info key exposes the full command and technical data; refresh replaces the existing view.
- The utility is native C, using libproc, Mach, sysctl, and IOKit without helper commands, an interpreter, or a daemon.
- The development build measured 3.2 MiB peak physical footprint, about 37–66 ms of CPU work per check, and no measurable idle CPU. The universal release has two slices; do not use the native build's 71.5 KiB as its download size.
- Development was assisted by Codex, with the behavior and interface iterated against the actual Mac workload.
- MIT licensed. Mac-only for now. Intel is included in the binary but has not been exercised on an Intel machine.

Useful feedback areas: process labeling, pressure thresholds, terminal readability, and real-world behavior when apps restart their helpers.

## Keep the claims precise

- Candidates suggest sustained resource use; the tool cannot prove which PID caused swapping or a freeze.
- Main-app protection is a heuristic. Signals go to only the chosen PID, and start identity is rechecked.
- Closing a helper can interrupt its work or window; recovery is controlled by the app.
- Swap totals may remain high after pressure improves. SSD free space already excludes existing swap use.
- No cross-platform claims, fabricated before/after screenshot, or Intel runtime claim.

## Local release artifacts

`make dist` produces the universal binary archive, source archive, and a SHA-256 file for each under `dist/`. The binary archive includes Brutus, README, and MIT license. It is ad-hoc signed and targets macOS 12 or newer.

The publishing commands below are intentionally manual. Preparing this folder does not publish the source, create a GitHub repository, or submit to HN.

```sh
gh repo create smashah/brutus --public --source . --remote origin
git push -u origin main
git push origin v0.1.0
gh release create v0.1.0 \
  dist/brutus-v0.1.0-macos-universal.tar.gz \
  dist/brutus-v0.1.0-macos-universal.tar.gz.sha256 \
  dist/brutus-v0.1.0-source.tar.gz \
  dist/brutus-v0.1.0-source.tar.gz.sha256 \
  --verify-tag --title 'Brutus 0.1.0' --notes-file docs/release-v0.1.0.md
```

Once the release is public, the README's installer and Homebrew URLs become live. Actual remote installation and Intel runtime behavior remain unexercised until those paths are run. No CI, lint, typecheck, or automated test workflows were added.
