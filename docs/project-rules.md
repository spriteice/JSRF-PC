# Project rules

How a change to JSRF-PC is placed, written, checked, and proposed. It is
written by the maintainer ("I") for whoever makes the change ("you"), and
goes with the short project rules. docs/build-and-test-reference.md covers
building, testing, and the change report.

## Before every commit

Do all five, then wait for my approval.

1. Run `git status` and `git diff --cached --stat`, and confirm that nothing
   on the project rules' "never in the repo" list is staged. If .gitignore
   doesn't cover it, tell me.
2. Show me the full diff.
3. Show me the proposed commit message.
4. For a fix: the root cause. For a feature: where it lives in the code, and
   why that's the right place.
5. For any added or edited document (README, docs/, changelog, release
   notes): the whole document rendered as Markdown, not only its diff.

## Where code goes

Decide this first.

1. **No band-aids.** For a bug, find the root cause and fix it there, not
   the symptom. For a feature, put it where it belongs in the code's
   structure (recompiler, runtime, renderer, or port/ overrides), not bolted
   on where it happens to work. If the right place isn't clear, stop and
   ask.
2. **The least invasive correct place.** Prefer port/ overrides, our own new
   files, settings, and existing hooks. Change shared engine or upstream
   code only when that's where the change truly belongs.
3. **Never edit generated files.** Fix the recompiler, the runtime, or the
   hand-written overrides in port/.
4. **Upstream files.** Deviating from upstream is fine where it serves our
   platforms (below), kept as small and visible as possible. The right place
   still comes first (1 and 2): never put code somewhere else only to avoid
   an upstream file. An upstream file is one the `upstream` branch has,
   wherever it is: port/main.c and port/recomp_manual.c are upstream files,
   port/freecam.c is ours. When a change edits or removes one (a port/
   override in recomp_manual.c included):
   - Say so in the proposal: which upstream files it touches, and how that
     affects future cherry-picks from upstream.
   - Add or update its entry in docs/upstream.md ("Where we differ from
     upstream") in the same commit.

   Adding a new file of our own needs no entry.
5. **A setting to turn it off.** Where it makes sense, a new feature can be
   turned off, with a setting that defaults to the original behaviour.
6. **Game function overrides.** Replacing one game function, or hooking one
   point in it, at its address (in port/) is the right place when that
   function's own logic is the root cause. Each override or hook is
   deliberate, named for what it does, and uses named constants for its
   address and any game offsets. If many functions misbehave for one
   reason, fix the recompiler or runtime instead (1). For a bug, the
   proposal says why the recompiler and runtime aren't the cause: the
   recompiled code matches the original instructions, and no other function
   shows the same symptom.

## How it's written

In priority order:

1. **Robust:** handle the general case, not only the one I observed.
2. **Minimal:** the fewest lines that do it properly. No unrelated
   refactors, renaming, or reformatting.
3. **Clean:** match the surrounding style, naming, and comments.

Reuse existing functions and helpers. Search the codebase before adding any
new function, and justify each new one.

## Never do

Stop and tell me why instead.

- Hardcoded address lists, or per-address special cases that hide a
  symptom. A named override or hook (Where code goes, 6) is not one.
- Bare numbers: magic numbers, and unnamed addresses or offsets.
- Sleeps or retries that hide timing bugs.

## Workflow

In order:

1. **Diagnose:** reproduce from my logs, gather evidence, and state the root
   cause and your confidence.
2. **Propose** the smallest fix, then wait for my go-ahead before editing.
3. **Implement.** Add a regression test where the repo's test setup allows
   it. When a fix can't be proven by reading the code, add logging or
   asserts that would confirm or refute your hypothesis.
4. **Verify** what you can: it builds, the repo's tests pass, and static
   checks are clean. You can't run the game, so call every runtime claim
   "unverified" until I test it. Never claim something is fixed without
   evidence.
5. **Track my testing.** Read every log I send in full, start to end, before
   reporting on it, using scripts for logs too long to read whole. Each time
   I send logs, check them against the build's test checklist and list
   every step still untested, per device. Before asking me to approve a
   commit, remind me of any untested steps and which device each needs. A
   step is tested only when my logs or my message show it.
6. **Report** the change with the change report in
   docs/build-and-test-reference.md.

At any step: if a rule conflicts with the task, or you can't solve it within
the rules, stop and ask. Don't quietly bend the rule.

## Platforms

In priority order:

1. **Linux:** primary. Compatibility is critical. It must run on SteamOS: no
   glibc symbol newer than 2.43, and within the README's minimum (2.34).
2. **Windows:** secondary. Compatibility is critical.
3. **macOS, then iOS and Android:** kept compatible where possible, as a
   courtesy to JuMatt. We don't build for them.

- Put platform-specific code behind the repo's existing abstractions.
- For every change, say which platforms you built or verified and which you
  could not.
- I test in Desktop Mode (KDE, and I'll say X11 or Wayland), but builds
  must also work in Game Mode, under gamescope. Don't assume a desktop
  window manager, mouse, or keyboard (fullscreen, input, and window handling
  especially), and flag any change that could behave differently there.

## Documents

- The README, the documents it links to, the changelog, and the release
  notes are human readable first, simple and concise while holding
  everything relevant, and in one consistent format.
- Version names, tags, the changelog, and release notes follow
  docs/versioning.md.
- The in-game menu follows docs/menu-format.md. Every new engine switch also
  goes under Developer > Engine switches, every new logging switch under
  Developer > Logging, and every new experiment under Developer >
  Experimental.
- Upstream, and where we differ from it, follow docs/upstream.md. Docs stay
  respectful of JuMatt and upstream: they describe how we take changes
  neutrally, never as criticism.
