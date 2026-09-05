---
name: steward
description: Repo-specific conventions for driving pull requests in KrX3D/WLED (branch layout, build/test commands, known CI flake).
---

# KrX3D/WLED PR conventions

This fork tracks upstream WLED on `main` and carries a separate long-running
branch per usermod/feature the owner has added. Keep these in mind before
acting on CI or review events for a PR in this repo.

## Branch layout

- `main` - tracks upstream WLED, kept close to the official releases.
- One branch per usermod/feature the owner maintains, e.g. `hour_effect_v2`,
  `button_relay_toggle_UM`, `nixieclock_v2_UM`,
  `usermod_seven_segment_display_reloaded`, `syslog`, `ntp`,
  `compile_defines`. Each of these is its own long-running line of
  development, not a short-lived PR branch - don't delete or rebase it away.
- A fix/review pass for one of these lands on a new branch (naming pattern
  used so far: `fix/<usermod>-review-bugs` or `codex/<slug>`) branched *from*
  the relevant feature branch, and its PR's base is that feature branch -
  **not** `main`. Diffing a usermod branch against `main` directly is noisy
  and often misleading, since several of these branches were forked from an
  older point in WLED's history and were never rebased onto current `main`;
  it will show large, unrelated upstream deltas alongside the real change.
  Prefer `git diff origin/main..origin/<feature-branch>` only to get initial
  bearings, then do the real review against the usermod's own files/history
  on that branch.
- Repo-wide changes (workflows, `.claude/`, docs) belong on `main`.

## Build/test

- Firmware: PlatformIO. `platformio.ini` defines ~20+ `[env:...]` build
  targets (ESP8266/ESP32 variants). There's no hardware in CI, so a usermod
  change is only build-verified, not functionally verified - call this out
  explicitly in PR descriptions rather than claiming behavioral testing.
- Web tooling: `npm ci && npm test` runs `tools/cdata-test.js` and friends
  (the build scripts that inline `wled00/data/*` into `html_*.h` headers).

## CI is already wired up

`.github/workflows/wled-ci.yml` runs `build.yml` (all PlatformIO
environments) plus the npm test suite on every `push` and `pull_request`,
regardless of base branch. `usermods.yml` additionally builds per-usermod
environments, but only for PRs from external forks
(`head.repo.full_name != base.repo.full_name`) - it's expected to show
"skipped" on same-repo branch-to-branch PRs like the fix branches above.
Don't add a redundant workflow; extend the existing ones if something is
genuinely missing.

## Known flaky test

`tools/cdata-test.js`'s "should build if" > "script was executed with -f or
--force" subtest asserts that `wled00/html_ui.h` was touched by a forced
rebuild, based on file mtime. It has intermittently failed on runs where the
diff never touched `wled00/data/`, `tools/`, or the html/js build inputs at
all. If this specific subtest is the only failure and the diff doesn't touch
those paths, treat it as a flake candidate: re-run once per the standard
CI-red flake rule before treating it as this PR's problem.
