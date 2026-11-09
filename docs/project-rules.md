# Project rules

How a change to JSRF-PC is placed, written, checked, and proposed. It is
written by the maintainer ("I") for whoever makes the change ("you"), and
goes with the short project rules: the project instructions in Project
settings, which every thread is given. A session on my PC isn't given them,
so its brief says what it needs from them. docs/build-and-test-reference.md
covers building, testing, and the change report. Every choice or approval
these rules leave to me is asked for on a card in the thread (the project
instructions, section 1), never only in a reply's text. A path these docs
name that isn't in the repository (known-bugs.md, `release-prep/`,
`test-builds/`, `building-on-pc/` and the work's folders) is in the
project's shared folder, which isn't published with it. The PC's claim,
PC-IN-USE.md and PC-HISTORY.md, is the exception: it is in
`building-on-pc\` in my Claude folder on the PC, where other projects that
use the PC read it too (docs/build-and-test-reference.md, When the PC is
busy).

## At a glance: a commit

In order, with where each step is said in full:

1. **Diagnose,** and write down the root cause and its model (Workflow 1).
2. **Propose** the fix at the root, and wait for my go-ahead, or post it in
   a pre-approved thread (Workflow 2).
3. **Implement** it in the layer that owns it, with its regression test and
   the docs it changes (Where code goes, How it's written).
4. **Test build and my testing,** for a change to what the game runs
   (below; docs/build-and-test-reference.md).
5. **The six checks,** in one review pack (Before every commit).
6. **My approval,** or a pre-approval's tests all passing (Pre-approved
   threads).
7. **Push** `main` only when I say so, or under a pre-approval, on its
   turn (Workflow, Taking turns on `main`).

**What the game runs** is what goes into the packages or the generated
code, or builds them: `src/`, `port/`, `include/`, `third_party/`,
`templates/runtime/`, `launcher/`, the CMake files outside `tests/`, and
the parts of `tools/` the pipeline runs (xiso, xbe_parser, disasm,
func_id, abi_analysis and recomp, with `tools/__init__.py`). A change to
anything else, such as the docs, `tests/`, `.github/` or the other tools,
needs no test build. When unsure, it counts. A change to what the game runs
that leaves the engine the same, byte for byte, in every way it's built
needs no test build either: its review pack shows the comparison instead
(docs/build-and-test-reference.md, The same engine). A change to what makes
the generated code (the parts of `tools/` the pipeline runs, and
`port/recomp_manual.c`) can't show that, so it still gets one.

## Before every commit

Do all six, in one review pack (docs/build-and-test-reference.md, "Review
pack"), then wait for my approval. A pre-approved thread doesn't wait
(Pre-approved threads).

1. Run `git status` and `git diff --cached --stat`, and confirm that nothing
   on the project rules' "never in the repo" list is staged. If .gitignore
   doesn't cover it, tell me. The list is for what is committed: the
   history already holds Claude session links in 131 commit messages, 124
   of them the toolkit's own from August and September and 7 of ours from
   21 and 22 October, none since. Every tag from b1 on holds them, so they
   stay.
2. Show me the full diff.
3. Show me the proposed commit message.
4. For a fix: the root cause and the model behind it (Where code goes, 1),
   as I agreed it (Workflow, 2) or, in a pre-approved thread, as posted,
   and its regression test (How it's written). For a feature: where it
   lives in the code, why that's the right place, and its tests (How it's
   written).
5. For any added or edited document (README, docs/, changelog, release
   notes): the whole document rendered as Markdown, not only its diff.
6. CI passes for the commit's exact tree, or for its tree before it was moved
   onto commits that change only Markdown files (Workflow, Several items at
   once): every job in `.github/workflows/ci.yml`, those that run in the build
   images in the cloud, and the Windows tests on my Windows PC
   (docs/build-and-test-reference.md, "CI"). GitHub runs every job again on
   the push, and a failure there is fixed in the next commit, before anything
   else is pushed. A job that fails before its checks run, because a download
   failed (a server down: GitHub's registry or GitHub itself, a toolchain or
   library download, or Ubuntu's archives for a new build image), is not the
   commit's failure. The thread that pushed runs the failed jobs again
   (GitHub's Re-run failed jobs, not the whole run) until the server is back,
   however long that takes, and nothing else is pushed until they pass. A
   commit that changes only Markdown files (`*.md`) is the exception: no job's
   result depends on them, so GitHub's run on its push is its CI. Its review
   pack says so, and the run is added when it ends.

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
   a switch back to the old behaviour only while it's being tested: the
   switch ships in one release at most, and is removed in the next
   (docs/release-checklist.md, step 2). The engine's defaults are what
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
catch it again instead: a check with `RECOMP_CHECK`
(src/platform/recomp_check.h), a line in the log or the freeze report, or a
step on the test checklist. Not `assert()`: CI, the test builds and the
releases are all built with `NDEBUG`, so an assert is in none of them.
`RECOMP_CHECK` is tested in every build, says once in the log where it
failed, and lets the code carry on.

**Every feature gets tests** of the parts of it that link on their own,
such as its maths, its settings and its menu rows, as tests/menu and
tests/freecam do. Once saved runs exist, it also gets one in the smoke set
(docs/build-and-test-reference.md, The smoke set). If no part of it can be
reached from a test, say why.

**Tests link the code they test.** A test builds the real source file,
never a copy. If the code can't be linked on its own, split it into a unit
that can (2, its own commit). The one exception, until the big files are
split by job (the model rewrites' first step): a test may build a piece cut
out by text of a file that can't be linked on its own, when the split is
bigger than its change. Its run.py says so in its first lines, with a line
that starts `Cut by text:` naming each file it cuts from and anything it
changes in what it cuts (a stand-in, or a second version to compare), and
why. `grep -l "Cut by text:" tests/*/run.py` lists them (23 today, most
from nv2a_gl.c). Once a file is split, its tests link the unit instead.

**A test that starts threads or measures time** passes 20 runs in a row
before the commit that adds or changes it (docs/build-and-test-reference.md,
Repeated runs).

**Docs change with the code.** A commit updates the docs that describe what
it changes, in the same commit: the README, docs/, the READMEs in src/,
port/ and tools/, and the packages' README.txt. known-bugs.md and the
release's backlog are kept current as the work happens, not at release.

## Never do

Stop and tell me why instead.

- Hardcoded address lists, or per-address special cases that hide a
  symptom. A named override or hook (Where code goes, 6) is not one.
- Bare numbers: magic numbers, and unnamed addresses or offsets.
- Sleeps or retries that hide timing bugs.
- Making up in one layer for a bug in another (Where code goes, 2).
- The recompiler's bring-up probes (`--force-return`, `--entry-stubs` and
  `--trace-functions`) anywhere but a diagnosis. A test build that carries
  one says so in TESTING.md, and none is ever in
  docs/building-on-linux.md's command, a release, or a commit.

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
   a fix before that, to test the diagnosis. A pre-approved thread posts
   the diagnosis and the proposal, and goes on (Pre-approved threads).
3. **Implement.** Add the fix's regression test, and update the docs the
   change affects (How it's written). When a fix can't be proven by reading
   the code, add logging or checks (`RECOMP_CHECK`) that would confirm or
   refute your hypothesis.
4. **Verify** what you can: it builds, the repo's tests pass, and the static
   checks pass: CI's count of the compilers' warnings in our code
   (docs/build-and-test-reference.md, CI, Warnings). You don't play the
   game: on my PC you run only the packaged engine's own checks and its runs
   with nobody at the pad, the idle run and, once they exist, the smoke
   set's saved runs (docs/build-and-test-reference.md, "On my PC"). So call
   every runtime claim "unverified" until I test it, but for what those
   runs' logs show. Never claim something is fixed without evidence. A
   change made for timing or speed is measured before and after, and tuning
   is measured again when a deeper cause in the same area is fixed.
5. **Track my testing.** Read every log I send in full, start to end, before
   reporting on it, using scripts for logs too long to read whole. Each time
   I send logs, check them against the build's test checklist and list
   every step still untested, per device. Before asking me to approve a
   commit, remind me of any untested steps and which device each needs. A
   step is tested only when my logs or my message show it.
6. **Report** the change with the change report in
   docs/build-and-test-reference.md.

**Several items at once.** Backlog items that don't change the same code can
be worked at the same time, each in its own thread, from diagnosis to test
build, and their testing can share one build
(docs/build-and-test-reference.md, "Batches"). Commits still go onto `main`
one at a time, each push on its turn (Taking turns on `main`, below), and each
starts from the latest `origin/main`. A thread may propose its own next commit
on top of its own unpushed one, with that commit's six checks on its own exact
tree, and its commits are pushed in order; it never builds on another thread's
unpushed commits without asking me. If `main` has moved since an item's
checks, its CI runs again on its new tree, unless the commits in between
change only Markdown files (`*.md`): no job's result depends on them (Before
every commit, 6), so its CI stands, and its review pack names its new tree and
those commits. If the commits in between change the code it changes, its test
build is made again and the steps they could affect are tested again. Each
thread changes only its own rows in the backlog and known-bugs.md, and reads
the file again just before each change.

**Switching to `main`, and scratch clones.** A thread's sandbox starts on a
branch its session makes. To commit, the thread may switch it to `main` and
fast-forward `main` to `origin/main` without asking, as long as `main` has
no unpushed commits from another thread; if it has, it asks me first.
Nothing is committed to the session's own branch or pushed to it. A scratch
clone or worktree of the thread's own in the sandbox, for staging and
checking trees, may be made, reset and removed without asking: nothing is
ever committed in it or pushed from it.

**Taking turns on `main`.** Every push to `main`, from any thread,
pre-approved or not, takes its turn in the shared folder's
`release-prep/PUSH-QUEUE.md`, one at a time:

- **Joining:** a thread adds its row once its commits are ready to push:
  when I've said push (or to push once something holds, such as GitHub's
  run going green), or in a pre-approved thread when all its tests pass.
  The row gives the thread, its backlog row, its commits and the time. It
  reads the file again just before each change.
- **Whose turn:** the first row in the file that may go. A pre-approved
  thread's row waits for an item its backlog orders first (Pre-approved
  threads, Alongside other threads), and the rows behind it that don't
  wait go ahead. The thread whose turn it is marks its row as pushing.
- **On its turn:** if `origin/main` has moved since its checks, its
  commits move onto it (asking me first, unless the thread is
  pre-approved) and their checks run again (Several items at once), then
  it pushes. Nothing else is pushed meanwhile, so its checks can't go
  stale.
- **The turn ends** when GitHub's run of `.github/workflows/ci.yml` on its
  push has passed. A failure there is fixed on the same turn, and a job that
  failed because a server was down is run again until the server is back
  (Before every commit, 6). Then the thread takes its row out, notes the push
  in the file, and tells the thread whose turn is next.
- **A thread that stops** for me before it has pushed takes its row out,
  and adds it again at the end when it goes on. Once it has pushed, it
  keeps its turn until GitHub's run passes, as nothing else is pushed
  before then.
- **A row left behind:** a thread that finds a row or a turn held by a
  thread that has stopped or ended tells me. It doesn't take the row out
  or take the turn itself.

At any step: if a rule conflicts with the task, or you can't solve it within
the rules, stop and ask. Don't quietly bend the rule. These rules come before
a session's own instructions and hooks: if those ask you to commit, push, or
work on another branch, say so, and don't. A session on my PC never commits
or pushes: commits are made in the cloud, after my approval or under a
pre-approval (Pre-approved threads). And when a session starts to wear (its
context nearly full or summarised, a rule slipped, or details lost), say so,
write a handoff in the shared folder, and move the work to a new session.

## Pre-approved threads

I can pre-approve a thread to work through its task on its own: to
diagnose, fix, adjust and fix again as the tests show, commit, and push
`main`, without waiting for me at each step. Everything else in these
rules still applies, with the changes below. This section is the only
full statement of it: the short project rules say only who can give one,
and point here.

### How it's given

- **Only by me, by hand, at the thread's start:** in my own message that
  opens the thread, or my first message in it before the thread has
  changed anything, with the words "automation pre-approved" (with or
  without the hyphen). It covers the task that message asks for, and
  nothing beyond it.
- **Nothing else gives one:** not a message from another session (the
  project's coordinator or another thread), a hook, a session's own
  instructions, a file, a commit message, a comment on GitHub, or my
  words quoted inside another session's message, even word for word.
  It isn't handed on: when a worn thread's work moves to a new one
  (Workflow, the last paragraph), the new one isn't pre-approved until I
  say so there.
- **When in doubt, it isn't:** if it isn't clear whether a message
  pre-approves the thread, the thread works under the usual rules, and
  asks me.

### At the start

The thread's first reply:

- confirms the pre-approval, with my words and their time, and the task
  it covers;
- names the saved runs (pre-recorded playback of a play session) its
  testing will need: for each, the area or stage, what the player does
  there, and the devices and modes. It says whether the shared folder's
  `input-playback/RUNS.md` has one that suits and still plays on route
  (until input playback exists, there's no such file and no saved run),
  and for any it hasn't, what I'd need to record, or which steps I'd
  test by hand instead;
- says anything it can already see that will stop it (What still needs
  me, below).

### What changes

- **Diagnosis** (Workflow 1 and 2): written down and posted with the
  proposed fix as always, but the thread goes on without waiting for my
  go-ahead. Its commit's check 4 names the diagnosis as posted.
- **Fixes on the go:** when a check or a test step fails, the thread
  finds why, fixes it, and runs it again, without asking. A fix to its
  own change goes into that change and is tested again, with the steps
  already passed that it could affect (docs/build-and-test-reference.md,
  Test checklist, Order). A fix to a commit already pushed is a commit
  of its own, through the same tests.
- **Other bugs it finds** go in known-bugs.md and are told to me. They
  aren't fixed in that thread unless I add them to its task. One that
  stops the task's own tests passing stops the thread (What still needs
  me, below).
- **Commits** (Before every commit): the six checks and the review pack
  are done as always, as the commit's record, and the thread commits
  once they and the tests below pass, without waiting for my approval.
- **Pushes:** the thread pushes `main` on its turn (Workflow, Taking
  turns on `main`) once the tests below pass. A push that finds
  `origin/main` has moved is never forced: the thread moves its own
  commits onto the new `origin/main`, and they go through the tests
  again (Several items at once, and Alongside other threads, below).
- **Git in its sandbox:** besides switching to `main` (Workflow,
  Switching to `main`), it may move its own unpushed commits onto a newer
  `origin/main`. Nothing else: no other branch, no rebase or reset of
  anything else, and nothing forced. If `main` has unpushed commits from
  another thread, it asks me first, as always.

### When all the tests pass

A commit is made and pushed only when all of these pass for its exact
tree:

1. **The six checks** of Before every commit, CI among them: the
   jobs in the cloud (or on my PC while a server is down) and the Windows
   tests on my PC, or, for a commit that changes only Markdown files,
   GitHub's run on its push.
2. **For a fix, its regression test,** failing without the fix and
   passing with it (How it's written).
3. **For a change to what the game runs** that needs one (at the top of
   this file): a test build of that tree, with its compatibility checks
   (docs/build-and-test-reference.md, Compatibility checks), and every step
   of its test checklist passed on its device (Workflow 5). A saved run that
   satisfies a step counts as that step's pass, where these rules let the
   thread run one: its log, read in full, shows what the step says I should
   see. Otherwise the step passes on my own results and logs, as does how
   something looks or feels, which a log can't show. A step I marked Skip,
   or left unmarked, hasn't passed. A change in several commits (a
   restructuring before its fix, or a batch's items) is tested on its last
   commit's tree (docs/build-and-test-reference.md, Batches), and pushed
   together.

After the push, the thread reads GitHub's run of
`.github/workflows/ci.yml` on the pushed commit when it ends. A failure
there is fixed in the next commit, before anything else is pushed
(Before every commit, 6).

### Alongside other threads

When other threads work on `main` at the same time, pre-approved or not,
the thread also:

- **Keeps to the backlog's commit order:** where its backlog orders the
  items' commits (in its notes, as b1.4's "commits go 2, 6, 8, then 7, 9,
  10 and 11"), it doesn't push ahead of an item ordered before its own
  that isn't pushed yet. It commits once its tests pass, joins the queue
  (Workflow, Taking turns on `main`), and waits there for that item's
  push. If that item has stopped, or no thread is working on it, it asks
  me whether to go first. The table's row order isn't a commit order,
  and items the backlog doesn't order go in the queue's order.
- **Checks for overlap once its fix is planned** (Workflow 2), against
  the items in progress in other threads (the backlogs' rows started and
  not yet pushed): whether either change could change what the other's
  steps test (below). If one could, one of them waits for the other's
  push before its test build, so no step is tested twice: the one the
  backlog orders later or, if it orders neither, the one that finds the
  overlap. It says so in a reply, and tells the other thread if that one
  is to wait.
- **Checks both ways again on its turn,** if `origin/main` has moved
  since its tests: whether the commits it now goes onto could change what
  its own steps tested, and whether its change could change what theirs
  tested (their test builds' TESTING.md, where they had one). A change
  could if it touches the same code, code that calls or is called by the
  other's, state both use, or the timing of a path a step tests; when
  unsure, it could. If either could, the new tree gets a test build, and
  those steps pass on it (When all the tests pass, 3) before the push. If
  neither could, CI on the new tree is enough. The review pack says
  which, and why.

### What still needs me

A pre-approval never covers these. The thread stops, says why, keeps
what it hasn't pushed, and goes on only once I've answered. The
pre-approval then holds again for the rest of the task.

- **Releases:** tags, releases, publishing, and every step of
  docs/release-checklist.md. While a release is under way (its backlog's
  Release row says it has reached a step), the thread commits but
  doesn't push, as that checklist pushes nothing before step 9.
- **Git beyond its own commits on `main`:** pushing anything else, a
  force push, or changing history already pushed. A pushed commit I
  don't want is undone by a new commit (`git revert`), on my say.
- **`upstream`,** and the project rules' "Never in the repo".
- **The rules and the checks:** the short project rules, this file,
  docs/build-and-test-reference.md, docs/release-checklist.md,
  docs/versioning.md, docs/menu-format.md, `.github/`, and any line
  added to `tests/known-failures.txt`, `tests/tsan.supp` or
  `tests/asan-known.txt`. A fix still takes its own bug's lines out
  (docs/build-and-test-reference.md, CI).
- **Tests made weaker:** skipping, disabling or loosening a test, or
  changing what it expects so that it passes, unless the task is that
  test's own fix.
- **Work beyond its task:** a backlog item or a feature it wasn't given,
  another thread's rows, moving an item between releases, going ahead of
  the backlog's commit order (Alongside other threads), or deciding
  whether a bug is critical (docs/release-checklist.md, The gate).
- **The decisions these rules give me:** where a fix goes, when that
  isn't clear or the fix is to a model and bigger than the task (Where
  code goes, 1); a second fix in one place, whose review comes first
  (Where code goes, 7); anything on the Never do list; and a rule that
  conflicts with the task.
- **Failures it can't fix:** a check or step that still fails after two
  fixes, a failure it can't explain, or one its change didn't cause
  (a known failure that changed, or a test that comes and goes).
- **Anything new installed,** on the PC or in the sandbox, or anything
  changed on a device beyond what docs/build-and-test-reference.md
  lists.
- **Anything sent outside the project:** a GitHub issue, comment or pull
  request, or a change to another repository.

### Records

Kept as the usual rules keep them, and:

- **The review pack** for each commit (docs/build-and-test-reference.md,
  Review pack) is written before the commit, as its record, and says
  under which pre-approval it was made. GitHub's run on the push is
  added when it ends.
- **The backlog row and known-bugs.md:** "automation pre-approved" when the
  work starts, with my message's time; each commit when it's made, with its
  hash and review pack; each wait for another item, with which item and
  why (Alongside other threads); and each push when it's done, with its
  time and GitHub's run. Each thread changes only its own rows, and reads
  the file again just before each change (Several items at once).
- **Replies:** the first one (At the start); one when a test build is
  ready for me; one when it starts to wait for another item, so I can
  say whether it goes first; one for each push, with its commits, review
  packs and GitHub's run; and one each time the thread stops. Progress
  in between goes on the thread's checklist, not in replies.

### When it ends

When I say so in the thread, when its task is pushed and GitHub's run on
it has passed, or when the work moves to a new thread. From then on the
usual rules apply to anything not yet pushed.

## Changing the rules

The rules are the short project rules, this file,
docs/build-and-test-reference.md, docs/release-checklist.md,
docs/versioning.md and docs/menu-format.md: the rule docs that What still
needs me lists (Pre-approved threads).

- **Gathered, then made together:** a change to them is proposed in the
  current release's backlog, under Rule changes, with who asked for it
  and why. They are made at that release's rules review
  (docs/release-checklist.md, step 5), each with my approval as always.
- **At once,** when work can't go on without one, or when I ask for a
  review of the rules themselves: say so, and I decide.
- **One commit a pass:** what one review of the rules changes goes in as
  one commit, not one for each finding, so the history isn't flooded
  with small docs commits.
- **Not held:** a commit that changes the code, CI or the tests updates
  the rule docs that describe them in the same commit (How it's written,
  "Docs change with the code").

## Platforms

JSRF-PC is for Linux and Windows only. No other platform is built, tested,
or considered. In priority order:

1. **Linux:** primary. Compatibility is critical. It must run on SteamOS: no
   glibc symbol newer than 2.43, and within the floor the package's
   README.txt publishes (2.34).
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
