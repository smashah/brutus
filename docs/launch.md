# Show HN launch brief

Repository: [smashah/brutus](https://github.com/smashah/brutus)

Release: [Brutus 0.1.0](https://github.com/smashah/brutus/releases/tag/v0.1.0)

This is a fact sheet and release handoff. Write the HN title and opening comment in your own words: [HN asks for human-written text and no automated posting](https://news.ycombinator.com/newsguidelines.html). [Show HN expects a working thing people can try](https://news.ycombinator.com/showhn.html), so publish the repository and downloadable release before submitting the link.

## Facts for your introduction

- The motivation is to squeeze more productivity out of a MacBook bought several years ago, while agents increasingly work in parallel.
- Many harnesses, subagents, tool calls, build processes, dev servers, and browser helpers make it difficult to identify which individual subprocess is consuming resources.
- Finding the biggest number in Activity Monitor or Stats and killing the parent interrupts unrelated jobs and creates more work restarting them.
- The goal is one surgical stop of an individual subprocess. Where the surrounding agent or harness can retry failed work, it can handle the recovery while the other jobs have a chance to continue.
- The name comes from a process “seizing” the Mac becoming a “seizer”, sounding like “Caesar”, with Brutus as the targeted intervention aimed at freeing the machine.
- Three intervals are sampled; the default offers the top three individual PIDs rather than a single CPU spike or a whole app tree.
- The main screen shows RAM, active swapping, swap used, SSD space remaining, CPU, the candidates, and their launch commands.
- The info key exposes the full command and technical data; refresh replaces the existing view.
- The utility is native C, using libproc, Mach, sysctl, and IOKit without helper commands, an interpreter, or a daemon.
- The development build measured 3.2 MiB peak physical footprint, about 37–66 ms of CPU work per check, and no measurable idle CPU. The universal release has two slices; do not use the native build's 71.5 KiB as its download size.
- Development was assisted by Codex, with the behavior and interface iterated against the actual Mac workload.
- The curl install command makes `brutus` available immediately and saves PATH for future terminals when needed. An optional short agent skill covers sustained stalls, read-only sampling, and authorized targeting of an individual helper.
- MIT licensed. Mac-only for now. Intel is included in the binary but has not been exercised on an Intel machine.

Useful feedback areas: process labeling, pressure thresholds, terminal readability, and real-world behavior when apps restart their helpers.

## Keep the claims precise

- Candidates suggest sustained resource use; the tool cannot prove which PID caused swapping or a freeze.
- Main-app protection is a heuristic. Signals go to only the chosen PID, and start identity is rechecked.
- Commands and working folders help identify jobs; automatic attribution to a particular harness or subagent is not implemented.
- Closing a subprocess can interrupt its work, window, or dependent jobs. Recovery is controlled by the agent or app and is not guaranteed or implemented by Brutus.
- Swap totals may remain high after pressure improves. SSD free space already excludes existing swap use.
- No cross-platform claims, fabricated before/after screenshot, or Intel runtime claim.

## Publication record

Published on 2026-10-14 under MIT with `main` as the default branch and annotated tag `v0.1.0`. The repository includes the terminal preview, installer, Homebrew formula, source/build instructions, resource measurements, and threshold/identity notes. Subsequent updates on `main` add the one-line installation recipe and optional agent skill; the original release artifacts remain as published.

The universal executable is 187,344 bytes (183 KiB); the compressed binary archive is about 49 KiB. The installer downloaded the public release archive, matched its SHA-256, and installed into a separate local directory. That downloaded build displayed Brutus 0.1.0's three-candidate preview menu with launch commands and exited with Q without closing any other process.

Intel runtime behavior and a full Homebrew installation have not been exercised. No CI, lint, typecheck, or automated test workflows were added. The HN post has not been submitted.

## Release artifacts

`make dist` produces the universal binary archive, source archive, and a SHA-256 file for each under `dist/`. The binary archive includes Brutus, README, and MIT license. It is ad-hoc signed and targets macOS 12 or newer.

The first-release publishing commands below are recorded for provenance. They have already been run; they do not submit to HN.

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

The README's installer and Homebrew URLs point to this published release. The installation recipe is deliberately separate from the utility: Brutus itself remains one native process with no helper commands.
