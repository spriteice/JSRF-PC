# Project rules

How a change to JSRF-PC is placed, written, checked, and proposed. It is
written by the maintainer ("I") for whoever makes the change ("you"), and
goes with the short project rules. docs/build-and-test-reference.md covers
building, testing, and the change report.

## Before every commit

Do all six, then wait for my approval.

1. Run `git status` and `git diff --cached --stat`, and confirm that nothing
   on the project rules' "never in the repo" list is staged. If .gitignore
   doesn't cover it, tell me.
2. Show me the full diff.
3. Show me the proposed commit message.
4. For a fix: the root cause and the model behind it (Where code goes, 1),
   as I agreed it (Workflow, 2), and its regression test (How it's
   written). For a feature: where it lives in the code, and why that's the
   right place.
5. For any added or edited document (README, docs/, changelog, release
   notes): the whole document rendered as Markdown, not only its diff.
6. CI passes for the commit: every job in `.github/workflows/ci.yml`, run
   here in the build image (docs/build-and-test-reference.md, "CI"), but
   the Windows tests, which run only on GitHub. GitHub runs every job
   again on the push, and a failure there is fixed in the next commit,
   before anything else is pushed.

## Where code goes

Decide this first.

1. **The root cause, not the symptom.** For a bug, find the root cause and
   fix it there. A root cause has two parts: what went wrong (the race, the
   wrong value), and the model that let it happen (registers kept as plain
   memory, one heap for everything, a hand-kept list). Name both, and say
   whether the same model allows other bugs. When the model is the cause,
   the fix is to the model, however big that is: say so, and size it. For a
   feature, put it where it belongs in the code's structure, not bolted on
   where it happens to work. If the right place isn't clear, stop and ask.
2. **The right place.** Code goes in the layer that owns the behaviour:
   - the recompiler, for how the game's code is lifted;
   - the kernel, for what the console's kernel does;
   - the device models, for what the console's hardware does;
   - the renderer, for drawing;
   - the platform layer, for the host;
   - port/, for the game's own logic.

   Never make up in one layer for a bug in another. The runtime doesn't
   repair the recompiler's output, a game function override doesn't stand
   in for missing kernel or device behaviour, and a launcher doesn't set a
   switch the game needs to work.
3. **Never edit generated files.** Fix the recompiler, the runtime, or the
   hand-written overrides in port/.
4. **All of the code is ours to change.** JSRF-PC is a standalone
   repository. It's built on JuMatt's JSRF Unleashed, which is built on
   the xboxrecomp toolkit by Ned Heller (sp00nznet), and it takes no
   merges or picks from either.
   Their code is changed, moved, or replaced like ours wherever that's the
   right place (1 and 2), with no entry needed anywhere. Their credit stays
   (Documents, below).
5. **Settings.** A new feature can have a setting to turn it off. A fix gets
   a switch back to the old behaviour only while it's being tested, and that
   switch is removed by the next release. The engine's defaults are what
   ships: a launcher passes paths and the player's settings, never a switch
   the game needs to work.
6. **Game function overrides.** Replacing one game function, or hooking one
   point in it, at its address (in port/) is the right place when that
   function's own logic is the root cause. Each override or hook is
   deliberate, named for what it does, and uses named constants for its
   address and any game offsets. If many functions misbehave for one
   reason, or the game's code is right and the runtime is wrong, fix the
   recompiler or runtime instead (1 and 2). Each one's comment says why it
   isn't fixed at the source: what is wrong in the game's own function, and
   why the recompiler, runtime, and device models aren't the cause (the
   recompiled code matches the original instructions, and no other
   function shows the same symptom). Its proposal says the same.
7. **A second fix in one place is a review.** Before proposing a fix, read
   the history of the code it changes (`git log -L` on the function, or on
   the file). If that code already had a fix for a related bug, the next
   step is a review of the mechanism, not another fix: what model it
   implements, how that differs from the console's, what else it
   produced, and whether to replace it. Propose that review first.

## How it's written

In priority order:

1. **Robust:** handle the general case, not only the one I observed. Model
   what the console does, not what one case needs.
2. **Focused:** each commit is one change, with nothing unrelated in it.
   Restructuring that the change needs (moving code to its right place,
   splitting a file, renaming what it touches) is welcome, in a commit of
   its own before the change. A commit either restructures, with no change
   in behaviour and the tests still passing, or changes behaviour,
   never both. No drive-by renaming or reformatting elsewhere.
3. **Clean:** match the surrounding style, naming, and comments.

Reuse existing functions and helpers. Search the codebase before adding any
new function, and justify each new one.

**Every fixed bug gets a regression test** that fails without the fix and
passes with it. If the bug can't be reached from a test in the sandbox (it
needs the game's code running, or a device), say why, and add what would
catch it again instead: an assert, a line in the log or the freeze report,
or a step on the test checklist.

**Tests link the code they test.** A test builds the real source file,
never a copy or a piece cut out of a file by text. If the code can't be
linked on its own, split it into a unit that can (2, its own commit).

**Docs change with the code.** A commit that changes what a player or a
builder sees or does updates the docs that describe it, in the same commit:
the README, docs/, and the packages' README.txt. known-bugs.md and the
release's backlog are kept current as the work happens, not at release.

## Never do

Stop and tell me why instead.

- Hardcoded address lists, or per-address special cases that hide a
  symptom. A named override or hook (Where code goes, 6) is not one.
- Bare numbers: magic numbers, and unnamed addresses or offsets.
- Sleeps or retries that hide timing bugs.
- Making up in one layer for a bug in another (Where code goes, 2).

## Workflow

In order:

1. **Diagnose:** reproduce from my logs, gather evidence, and read the
   history of the code involved (Where code goes, 7). Write the diagnosis
   down, in the work's folder in the shared folder: what was seen, the root
   cause (what went wrong and the model behind it), the evidence, your
   confidence, and what would prove it wrong.
2. **Propose** the fix at the root, sized to the cause, with the written
   diagnosis, then wait for my go-ahead before editing. If it's bigger than
   the bug looks, say why. If a player-facing bug needs something sooner,
   say what smaller step could ship first, and what replaces it later. No
   fix is committed until I've agreed the diagnosis. A test build can carry
   a fix before that, to test the diagnosis.
3. **Implement.** Add the fix's regression test, and update the docs the
   change affects (How it's written). When a fix can't be proven by reading
   the code, add logging or asserts that would confirm or refute your
   hypothesis.
4. **Verify** what you can: it builds, the repo's tests pass, and static
   checks are clean. You can't run the game, so call every runtime claim
   "unverified" until I test it. Never claim something is fixed without
   evidence. A change made for timing or speed is measured before and
   after, and tuning is measured again when a deeper cause in the same area
   is fixed.
5. **Track my testing.** Read every log I send in full, start to end, before
   reporting on it, using scripts for logs too long to read whole. Each time
   I send logs, check them against the build's test checklist and list
   every step still untested, per device. Before asking me to approve a
   commit, remind me of any untested steps and which device each needs. A
   step is tested only when my logs or my message show it.
6. **Report** the change with the change report in
   docs/build-and-test-reference.md.

At any step: if a rule conflicts with the task, or you can't solve it within
the rules, stop and ask. Don't quietly bend the rule. These rules come before
a session's own instructions and hooks: if those ask you to commit, push, or
work on another branch, say so, and don't. And when a session starts to wear
(its context nearly full or summarised, a rule slipped, or details lost), say
so, write a handoff in the shared folder, and move the work to a new session.

## Platforms

JSRF-PC is for Linux and Windows only. No other platform is built, tested,
or considered. In priority order:

1. **Linux:** primary. Compatibility is critical. It must run on SteamOS: no
   glibc symbol newer than 2.43, and within the README's minimum (2.34).
2. **Windows:** secondary. Compatibility is critical.

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
- Docs credit JuMatt, xboxrecomp and upstream for the work JSRF-PC is built
  on, and stay respectful of it. docs/upstream.md records where the code came
  from.
