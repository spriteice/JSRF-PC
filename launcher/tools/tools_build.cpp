/*
 * JSRF-PC Tools' Build page (tools.h): what the launcher's first start does
 * (launcher/setup), a step at a time, and what it leaves behind.
 *
 *   Engine: where it is, what it says it is (its --version, against the
 *     package's VERSION), and its signature: its SHA-256 against the one
 *     this release builds (kit/expected.txt), as the launcher checks it
 *     before every start. When it isn't the release's, the player can use it
 *     anyway, as the setup asks.
 *   Build: the engine built from the player's own game (default.xbe, from
 *     its folder or a disc image), or only the code generated from it, not
 *     compiled (build_engine.py --generate-only). A build compiles the code
 *     generated before as it is when it's from the same game and this
 *     release (--reuse-generated); otherwise it generates it first. The
 *     generated code can be deleted, the engine staying.
 *   Tools: Python with capstone, and Zig, downloaded from their own sites,
 *     each checked by its SHA-256 (setup_core.c's list), and deleted.
 *
 * The work is setup_core.c's, on a thread of the page's own, one task at a
 * time, with the setup's bar; closing the window stops it
 * (tools_build_closing), as closing the setup's does, and the engine there
 * stays as it was. Everything the page shows is read on that thread too
 * (sizes, hashes), when the page is shown and after each task.
 *
 * A package with the engine built in (a test build's) has no kit: the page
 * shows the engine, and has nothing to build. The page needs only JSRF-PC's
 * folder, not its saves, so it works before the launcher has been given the
 * game.
 */
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "file_dialog.h"
#include "save_store.h"
#include "setup.h"
#include "setup_job.h"
#include "tools.h"

#ifdef _WIN32
#define SEP "\\"
#define ENGINE_REL "engine\\jsrf_recomp.exe"
#else
#define SEP "/"
#define ENGINE_REL "bin/jsrf_recomp"
#endif

#define SEC_ENGINE 1
#define SEC_BUILD  2
#define SEC_TOOLS  3

/* The page's tasks, on its thread. */
enum {
    T_NONE,
    T_LOOK,        /* what's on disk */
    T_FIRST,       /* that, and both checks: the page's first showing */
    T_VERSION,     /* the engine's --version */
    T_SIGNATURE,   /* the engine's SHA-256 (and the generated code's) against the release's */
    T_BUILD,
    T_GENERATE,
    T_TOOLS,       /* the tools downloaded */
    T_DEL_GEN,
    T_DEL_TOOLS
};

/* The generated code's hash against the release's. */
enum { GEN_NONE, GEN_MATCH, GEN_MISMATCH, GEN_UNKNOWN, GEN_UNREADABLE };

/* The page's own types, kept to this file: each page has a page_state, and
 * without this the linker keeps one page's destructor for all of them, which
 * then frees another page's text as pointers when the tool closes. */
namespace {

/* What's on disk. */
struct look {
    int      kit;                  /* kit/kit.json: the package builds its engine */
    char     release[64];          /* the package's VERSION, "" none */
    int      engine;
    char     engine_when[32];
    uint64_t engine_size;
    int      gen;                  /* the generated code's folder is there */
    int      gen_about;            /* with build_engine.py's note of what it was made from */
    char     gen_when[32];
    uint64_t gen_size;             /* the generated code and the build's work: what Delete takes */
    int      tools_ready, tools_there;
    uint64_t tools_size;
    int      log;
};

/* The checks' results. */
struct checks {
    int  version_done, version_ok;
    char version[256];             /* what the engine said, or why it didn't */
    int  sig_done, engine_code;    /* JS_ENGINE_* */
    char engine_sha[65];
    int  gen_code;                 /* GEN_* */
    char gen_sha[65];
};

struct page_state {
    int          inited;
    int          drawn_frame;      /* the frame the page last drew: a gap means it was just shown */
    int          first_done;       /* the first showing's checks have been started */
    SDL_Thread  *thread;
    SDL_atomic_t done;
    SDL_mutex   *mu;
    int          task;             /* the one running, T_NONE when idle */
    volatile int cancel;
    /* under mu: the bar, and what it says */
    js_bar       bar;
    char         bar_text[512];
    /* the thread's, read once it's done */
    jsrf_setup   s;
    js_job       job;
    int          rc;
    look         lk_new;
    checks       ck_new;
    /* the page's own */
    look         lk;
    int          looked;
    checks       ck;
    char         root[JSRF_PATH_MAX];
    char         game[JSRF_PATH_MAX];      /* picked or dropped; "" the launcher's */
    int          st_section, st_kind;
    char         st[2400];
    ImGuiTextBuffer text;
};
}  /* namespace */

static page_state g;

static void status(int section, int kind, const char *fmt, ...) IM_FMTARGS(3);
static void status(int section, int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.st, sizeof g.st, fmt, ap);
    va_end(ap);
    g.st_section = section;
    g.st_kind = kind;
}

static void show_status(int section)
{
    if (g.st_section == section && g.st[0]) tools_status(g.st_kind, "%s", g.st);
}

/* ---- small things ----------------------------------------------------------------------- */

static void size_text(uint64_t bytes, char *out, size_t n)
{
    double mb = (double)bytes / 1e6;
    if (mb < 1) snprintf(out, n, "under 1 MB");
    else if (mb < 1000) snprintf(out, n, "%.0f MB", mb);
    else snprintf(out, n, "%.1f GB", mb / 1000);
}

static uint64_t file_size(const char *path)
{
    FILE *f = js_fopen(path, "rb");
    long s;
    if (!f) return 0;
    s = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : 0;
    fclose(f);
    return s > 0 ? (uint64_t)s : 0;
}

/* A path's last two parts, as tools_path shows them, for running text. */
static const char *short_path(const char *path, char *out, size_t n)
{
    const char *cut = path, *p;
    int seps = 0;
    for (p = path + strlen(path); p > path; p--)
        if ((p[-1] == '/' || p[-1] == '\\') && ++seps == 2) { cut = p; break; }
    snprintf(out, n, "%s%s", cut != path ? "..." SEP : "", cut);
    return out;
}

/* A SHA-256 as its start and end, the whole on hover (field's tip). */
static const char *short_sha(const char *sha, char *out, size_t n)
{
    if (strlen(sha) == 64) snprintf(out, n, "%.12s...%s", sha, sha + 60);
    else snprintf(out, n, "%s", sha);
    return out;
}

/* label, then text: grey (kind -1) or as tools_status colours it, the
 * whole of tip on hover when there is one. */
static void field(const char *label, int kind, const char *text, const char *tip)
{
    ImVec4 c = kind == TOOLS_OK     ? ImVec4(0.55f, 0.85f, 0.35f, 1)
             : kind == TOOLS_WARN   ? ImVec4(0.95f, 0.75f, 0.3f, 1)
             : kind == TOOLS_ERROR  ? ImVec4(0.95f, 0.4f, 0.35f, 1)
                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (tip && tip[0]) ImGui::SetItemTooltip("%s", tip);
}

/* The tools' downloads, in MB, rounded to 10: a size not known ahead taken
 * as the setup takes it. */
static int download_mb(void)
{
    size_t n, i;
    const js_download_item *d = js_downloads(&n);
    uint64_t total = 0;
    for (i = 0; i < n; i++) total += d[i].size ? d[i].size : 25000000;
    return (int)((total / 1e6 + 5) / 10) * 10;
}

static const char *game_of(const tools_ctx *t)
{
    return g.game[0] ? g.game : t->game_files;
}

/* ---- the thread ------------------------------------------------------------------------- */

static void look_now(const char *root, look *lk)
{
    char p[JSRF_PATH_MAX], gen[JSRF_PATH_MAX];
    unsigned char *data = NULL;
    size_t size = 0;
    memset(lk, 0, sizeof *lk);
    js_path(p, sizeof p, root, "kit/kit.json");
    lk->kit = js_is_file(p);
    js_path(p, sizeof p, root, "VERSION");
    if (js_read_file(p, 4096, &data, &size) == 0 && data) {
        size_t l = 0;
        while (l < size && l < sizeof lk->release - 1 && data[l] != '\r' && data[l] != '\n') l++;
        memcpy(lk->release, data, l);
        lk->release[l] = 0;
    }
    free(data);
    js_engine_path(root, p, sizeof p);
    if ((lk->engine = js_is_file(p)) != 0) {
        jsrf_store_file_when(p, lk->engine_when);
        lk->engine_size = file_size(p);
    }
    js_path(gen, sizeof gen, root, JS_GEN_DIR);
    lk->gen = js_is_dir(gen);
    js_path(p, sizeof p, root, JS_GEN_ABOUT);
    lk->gen_about = js_is_file(p);
    if (lk->gen_about) jsrf_store_file_when(p, lk->gen_when);
    lk->gen_size = js_generated_size(root);
    lk->tools_ready = js_tools_ready(root);
    js_path(p, sizeof p, root, "tools");
    lk->tools_there = js_is_dir(p);
    lk->tools_size = lk->tools_there ? js_tools_folder_size(root) : 0;
    js_path(p, sizeof p, root, "build/build.log");
    lk->log = js_is_file(p);
}

static void check_version(const char *root, checks *ck)
{
    ck->version_done = 1;
    ck->version_ok = js_engine_version(root, ck->version, sizeof ck->version) == 0;
}

static void check_signature(const char *root, checks *ck)
{
    char gen[JSRF_PATH_MAX], p[JSRF_PATH_MAX], want[128];
    ck->sig_done = 1;
    ck->engine_code = js_engine_check(root, ck->engine_sha);
    ck->gen_sha[0] = 0;
    js_path(gen, sizeof gen, root, JS_GEN_DIR);
    if (!js_is_dir(gen)) { ck->gen_code = GEN_NONE; return; }
    if (js_gen_hash(gen, ck->gen_sha)) { ck->gen_code = GEN_UNREADABLE; return; }
    js_path(p, sizeof p, root, "kit/expected.txt");
    if (!js_read_kv(p, "gen_sha256", want, sizeof want) || !want[0]) ck->gen_code = GEN_UNKNOWN;
    else ck->gen_code = strcmp(want, ck->gen_sha) ? GEN_MISMATCH : GEN_MATCH;
}

static void on_update(void *ctx, double at, double end, double secs, double left, const char *text, const char *log)
{
    (void)ctx; (void)log;
    SDL_LockMutex(g.mu);
    js_bar_move(&g.bar, (double)SDL_GetTicks64() / 1000.0, at, end, secs, left);
    if (text) snprintf(g.bar_text, sizeof g.bar_text, "%s", text);
    SDL_UnlockMutex(g.mu);
}

static int worker(void *arg)
{
    const char *root = g.root;
    int checks_after = 0;
    (void)arg;
    memset(&g.ck_new, 0, sizeof g.ck_new);
    switch (g.task) {
    case T_BUILD:
    case T_GENERATE:
    case T_TOOLS:
        g.rc = js_job_run(&g.job);
        if (g.job.log) { fclose(g.job.log); g.job.log = NULL; }
        checks_after = g.task != T_TOOLS;
        break;
    case T_DEL_GEN:
        g.rc = js_delete_generated(root);
        checks_after = 1;
        break;
    case T_DEL_TOOLS:
        g.rc = js_delete_tools_folder(root);
        break;
    case T_FIRST:
        checks_after = 1;
        break;
    case T_VERSION:
        check_version(root, &g.ck_new);
        break;
    case T_SIGNATURE:
        check_signature(root, &g.ck_new);
        break;
    default:
        break;
    }
    if (checks_after) {
        check_version(root, &g.ck_new);
        check_signature(root, &g.ck_new);
    }
    look_now(root, &g.lk_new);
    SDL_AtomicSet(&g.done, 1);
    return 0;
}

static int running(void)
{
    return g.task != T_NONE;
}

static void start(const tools_ctx *t, int task)
{
    if (running()) return;
    snprintf(g.root, sizeof g.root, "%s", t->game);
    g.task = task;
    g.cancel = 0;
    g.rc = 0;
    SDL_LockMutex(g.mu);
    memset(&g.bar, 0, sizeof g.bar);
    snprintf(g.bar_text, sizeof g.bar_text, "%s", task == T_TOOLS ? "Downloading the tools" : "Starting");
    SDL_UnlockMutex(g.mu);
    if (task == T_BUILD || task == T_GENERATE || task == T_TOOLS) {
        memset(&g.s, 0, sizeof g.s);
        g.s.root = g.root;
        snprintf(g.s.game, sizeof g.s.game, "%s", game_of(t));
        memset(&g.job, 0, sizeof g.job);
        g.job.s = &g.s;
        g.job.task = task == T_BUILD ? JS_TASK_BUILD : task == T_GENERATE ? JS_TASK_GENERATE : JS_TASK_TOOLS;
        g.job.download = !g.lk.tools_ready;
        g.job.reuse_generated = task == T_BUILD;
        g.job.prog.update = on_update;
        g.job.prog.cancel = &g.cancel;
    }
    SDL_AtomicSet(&g.done, 0);
    g.thread = SDL_CreateThread(worker, "build page", NULL);
    if (!g.thread) {
        g.task = T_NONE;
        status(task == T_TOOLS || task == T_DEL_TOOLS ? SEC_TOOLS : task == T_VERSION || task == T_SIGNATURE
                   ? SEC_ENGINE : SEC_BUILD, TOOLS_ERROR, "It couldn't be started (%s).", SDL_GetError());
    }
}

/* The task over: what it found, and how it went. */
static void finished(void)
{
    int task = g.task;
    const js_job *j = &g.job;
    char sz[32];
    SDL_WaitThread(g.thread, NULL);
    g.thread = NULL;
    g.task = T_NONE;
    g.lk = g.lk_new;
    g.looked = 1;
    if (g.ck_new.version_done) {
        g.ck.version_done = 1;
        g.ck.version_ok = g.ck_new.version_ok;
        memcpy(g.ck.version, g.ck_new.version, sizeof g.ck.version);
    }
    if (g.ck_new.sig_done) {
        g.ck.sig_done = 1;
        g.ck.engine_code = g.ck_new.engine_code;
        g.ck.gen_code = g.ck_new.gen_code;
        memcpy(g.ck.engine_sha, g.ck_new.engine_sha, sizeof g.ck.engine_sha);
        memcpy(g.ck.gen_sha, g.ck_new.gen_sha, sizeof g.ck.gen_sha);
    }
    switch (task) {
    case T_VERSION:
        if (g.ck.version_ok) status(SEC_ENGINE, TOOLS_OK, "Checked.");
        else status(SEC_ENGINE, TOOLS_ERROR, "Its version couldn't be read: %s.", g.ck.version);
        break;
    case T_SIGNATURE:
        status(SEC_ENGINE, TOOLS_OK, "Checked.");
        break;
    case T_BUILD:
        if (g.rc != 0 && !strcmp(j->err, "cancelled"))
            status(SEC_BUILD, TOOLS_WARN, "Stopped. The engine there stays as it was.");
        else if (g.rc != 0)
            status(SEC_BUILD, TOOLS_ERROR, "The engine couldn't be built: %s", j->err);
        else if (!strcmp(j->engine_check, "match"))
            status(SEC_BUILD, TOOLS_OK, "Built. It's the engine this release builds.");
        else if (!strcmp(j->engine_check, "mismatch"))
            status(SEC_BUILD, TOOLS_WARN, "Built, but it isn't the engine this release builds. JSRF-PC will offer "
                                          "to build it again, unless you use it anyway (under Engine).");
        else
            status(SEC_BUILD, TOOLS_OK, "Built. This release doesn't list its engine's SHA-256, so it's taken as "
                                        "the one built here.");
        break;
    case T_GENERATE:
        if (g.rc != 0 && !strcmp(j->err, "cancelled"))
            status(SEC_BUILD, TOOLS_WARN, "Stopped. There's no generated code now: generate it again to build "
                                          "from it.");
        else if (g.rc != 0)
            status(SEC_BUILD, TOOLS_ERROR, "The code couldn't be generated: %s", j->err);
        else if (!strcmp(j->gen_check, "mismatch"))
            status(SEC_BUILD, TOOLS_WARN, "Generated, but it isn't the code this release's game makes, so an engine "
                                          "built from it won't be this release's either.");
        else
            status(SEC_BUILD, TOOLS_OK, "Generated. Build engine compiles it as it is.");
        break;
    case T_TOOLS:
        if (g.rc != 0 && !strcmp(j->err, "cancelled"))
            status(SEC_TOOLS, TOOLS_WARN, "Stopped. Nothing of the download was kept.");
        else if (g.rc != 0)
            status(SEC_TOOLS, TOOLS_ERROR, "The tools couldn't be downloaded: %s", j->err);
        else {
            size_text(g.lk.tools_size, sz, sizeof sz);
            status(SEC_TOOLS, TOOLS_OK, "Downloaded. The tools are ready (%s).", sz);
        }
        break;
    case T_DEL_GEN:
        if (g.rc == 0) status(SEC_BUILD, TOOLS_OK, "Deleted. The engine stays.");
        else status(SEC_BUILD, TOOLS_ERROR, "Some of it couldn't be deleted (is a file in it open?).");
        break;
    case T_DEL_TOOLS:
        if (g.rc == 0) status(SEC_TOOLS, TOOLS_OK, "Deleted.");
        else status(SEC_TOOLS, TOOLS_ERROR, "Some of the tools couldn't be deleted (is one of them running?).");
        break;
    default:
        break;
    }
}

/* ---- the sections ----------------------------------------------------------------------- */

static void draw_engine(tools_ctx *t)
{
    char p[JSRF_PATH_MAX], when[64], sz[32], line[400], sha[40];
    tools_heading("Engine");
    tools_hint("The program that runs the game, built on this PC from your own copy of it.");
    tools_help_marker("Before each start JSRF-PC checks the engine's SHA-256, its signature: when it isn't the one "
                      "this release builds, it offers to build it again. Check version asks the engine which "
                      "release it was built as. Check signature works out its SHA-256 again and compares it with "
                      "the release's.");
    js_engine_path(t->game, p, sizeof p);
    tools_path("Engine:", g.lk.engine ? p : "");
    if (g.lk.engine) {
        jsrf_store_when_text(g.lk.engine_when, when, sizeof when);
        size_text(g.lk.engine_size, sz, sizeof sz);
        snprintf(line, sizeof line, "%s, %s", when, sz);
        field("Made:", -1, line, NULL);
    }

    if (!g.lk.engine) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("No engine yet. %s", g.lk.kit ? "Build it below." : "This package should have one: "
                                                                               "unpack the whole download again.");
        ImGui::PopStyleColor();
    } else if (!g.ck.version_done) field("Version:", -1, "not checked", NULL);
    else if (!g.ck.version_ok) field("Version:", TOOLS_ERROR, "couldn't be read", g.ck.version);
    else if (g.lk.release[0] && strcmp(g.ck.version, g.lk.release)) {
        snprintf(line, sizeof line, "%s, but this release is %s: build it again to match", g.ck.version,
                 g.lk.release);
        field("Version:", TOOLS_WARN, line, NULL);
    } else {
        field("Version:", -1, g.ck.version, NULL);
    }

    if (!g.lk.engine) {
        /* said above */
    } else if (!g.ck.sig_done) {
        field("Signature:", -1, "not checked", NULL);
    } else {
        switch (g.ck.engine_code) {
        case JS_ENGINE_RELEASE:
            field("Signature:", TOOLS_OK, "matches the engine this release builds", NULL); break;
        case JS_ENGINE_BUILT:
            field("Signature:", TOOLS_OK, "the one built here (this release doesn't list its engine's)", NULL); break;
        case JS_ENGINE_ACCEPTED:
            field("Signature:", TOOLS_WARN, "not this release's engine, used anyway as you said", NULL); break;
        case JS_ENGINE_OTHER:
            field("Signature:", TOOLS_ERROR, "not this release's engine: JSRF-PC will offer to build it again",
                  NULL);
            break;
        case JS_ENGINE_BUILT_IN:
            field("Signature:", -1, "came with this package, which lists none to check it against", NULL); break;
        default:
            field("Signature:", -1, "there's no engine to check", NULL); break;
        }
        if (g.ck.engine_sha[0]) field("SHA-256:", -1, short_sha(g.ck.engine_sha, sha, sizeof sha), g.ck.engine_sha);
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));

    ImGui::BeginDisabled(running() || !g.lk.engine);
    if (tools_flow_button("Check version", true)) start(t, T_VERSION);
    if (tools_flow_button("Check signature", false)) start(t, T_SIGNATURE);
    ImGui::BeginDisabled(!(g.ck.sig_done && g.ck.engine_code == JS_ENGINE_OTHER));
    if (tools_flow_button("Use it anyway", false)) {
        g.text.clear();
        g.text.appendf("JSRF-PC then starts with this engine, and doesn't offer to build it again. It may still "
                       "work: your game may differ from the one the release was made with.\n\nBuilding again "
                       "later checks the new engine afresh.");
        ImGui::OpenPopup("Use this engine anyway?");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (tools_warning("Use this engine anyway?", g.text.c_str(), "Use it")) {
        char now[65];
        if (js_engine_check(t->game, now) == JS_ENGINE_OTHER && !strcmp(now, g.ck.engine_sha)) {
            js_accept_engine(t->game, now);
            g.ck.engine_code = js_engine_check(t->game, now);
            status(SEC_ENGINE, TOOLS_OK, "JSRF-PC now starts with it.");
        } else {
            status(SEC_ENGINE, TOOLS_WARN, "The engine has changed since it was checked: check its signature again.");
        }
    }
    show_status(SEC_ENGINE);
}

static void pick_game(tools_ctx *t, int image)
{
    char picked[JSRF_PATH_MAX] = "";
    int r;
    if (image) {
        static const char *const pats[] = { "*.iso", "*.xiso", "*.ISO", NULL };
        struct one { char *out; size_t n; } o = { picked, sizeof picked };
        r = jsrf_pick_files_in("Pick the game's disc image", game_of(t), "Xbox disc images", pats, 0,
                               [](void *ctx, const char *path) {
                                   struct one *x = (struct one *)ctx;
                                   snprintf(x->out, x->n, "%s", path);
                               }, &o);
    } else {
        r = jsrf_pick_folder_in("Where is the game? Pick the folder with default.xbe in it", game_of(t), picked,
                                sizeof picked);
    }
    if (r == JSRF_NO_DIALOG)
        status(SEC_BUILD, TOOLS_WARN, "There's no file dialog here (zenity or kdialog): drop the game's folder or "
                                      "disc image on this window instead.");
    else if (r == JSRF_PICKED && picked[0]) {
        snprintf(g.game, sizeof g.game, "%s", picked);
        g.st[0] = 0;
    }
}

static void ask_build(tools_ctx *t, int generate)
{
    char shown[JSRF_PATH_MAX + 8];
    short_path(game_of(t), shown, sizeof shown);
    g.text.clear();
    if (generate) {
        g.text.appendf("The code is generated from the game in %s, in place of the code generated before. Nothing "
                       "is compiled, and the engine stays as it is.\n\n", shown);
        if (!g.lk.tools_ready)
            g.text.appendf("The tools are downloaded first: about %d MB, from their own sites.\n\n", download_mb());
        g.text.appendf("It takes a minute or two.");
        ImGui::OpenPopup("Generate the code?");
    } else {
        g.text.appendf("The engine is built from the game in %s. Code generated before from the same game by this "
                       "release is compiled as it is; otherwise it's generated first.\n\n", shown);
        if (!g.lk.tools_ready)
            g.text.appendf("The tools are downloaded first: about %d MB, from their own sites.\n\n", download_mb());
        g.text.appendf("It takes a few minutes. The engine there now stays until the new one is complete. Close "
                       "JSRF-PC before going on.");
        ImGui::OpenPopup("Build the engine?");
    }
}

static void draw_bar(void)
{
    char text[512];
    float f;
    SDL_LockMutex(g.mu);
    f = (float)js_bar_at(&g.bar, (double)SDL_GetTicks64() / 1000.0);
    snprintf(text, sizeof text, "%s", g.bar_text);
    SDL_UnlockMutex(g.mu);
    ImGui::ProgressBar(f, ImVec2(-FLT_MIN, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    if (ImGui::Button("Stop")) {
        g.cancel = 1;
        js_kill_child();
    }
}

static void draw_build(tools_ctx *t)
{
    char when[64], sz[32], line[400], sha[40];
    int job = g.task == T_BUILD || g.task == T_GENERATE;
    tools_heading("Build");
    if (!g.lk.kit) {
        tools_hint("This package came with its engine built in, so it has nothing to build.");
        tools_help_marker("A release's package builds its engine on your PC from your own copy of the game, with "
                          "the kit beside JSRF-PC (the kit folder). This one has no kit: its engine was built "
                          "before it was packaged, as a test build's is.");
    } else {
        tools_hint("Builds the engine from your game, or only generates its code.");
        tools_help_marker("Building translates the game's default.xbe into C, the generated code, then compiles it "
                          "with the tools: a few minutes. Generate code does only the first part; Build engine then "
                          "compiles that code as it is, when it's from the same game and this release. The "
                          "generated code is made from your game, so it stays on this PC and is never in a "
                          "package. You can also drop the game's folder or disc image on this window.");
    }
    tools_path("Game:", game_of(t));
    if (g.lk.gen) {
        if (g.lk.gen_about) {
            jsrf_store_when_text(g.lk.gen_when, when, sizeof when);
            size_text(g.lk.gen_size, sz, sizeof sz);
            snprintf(line, sizeof line, "%s, %s", when, sz);
            field("Generated code:", -1, line, NULL);
        } else {
            size_text(g.lk.gen_size, sz, sizeof sz);
            snprintf(line, sizeof line, "from an earlier build, or one that stopped (%s): a build generates it again",
                     sz);
            field("Generated code:", -1, line, NULL);
        }
        if (g.ck.sig_done) {
            switch (g.ck.gen_code) {
            case GEN_MATCH:
                field("Its signature:", TOOLS_OK, "matches the code this release's game makes", g.ck.gen_sha); break;
            case GEN_MISMATCH:
                field("Its signature:", TOOLS_WARN, "isn't the code this release's game makes", g.ck.gen_sha); break;
            case GEN_UNKNOWN:
                field("Its signature:", -1, short_sha(g.ck.gen_sha, sha, sizeof sha), g.ck.gen_sha); break;
            case GEN_UNREADABLE:
                field("Its signature:", TOOLS_ERROR, "couldn't be read", NULL); break;
            default:
                break;
            }
        }
    } else if (g.lk.gen_size) {
        size_text(g.lk.gen_size, sz, sizeof sz);
        snprintf(line, sizeof line, "none, but the build's work is there (%s)", sz);
        field("Generated code:", -1, line, NULL);
    } else {
        field("Generated code:", -1, "none", NULL);
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));

    ImGui::BeginDisabled(running() || !g.lk.kit);
    if (tools_flow_button("Build engine", true)) {
        if (!game_of(t)[0]) pick_game(t, 0);
        if (game_of(t)[0]) ask_build(t, 0);
    }
    if (tools_flow_button("Generate code", false)) {
        if (!game_of(t)[0]) pick_game(t, 0);
        if (game_of(t)[0]) ask_build(t, 1);
    }
    ImGui::BeginDisabled(!g.lk.gen_size);
    if (tools_flow_button("Delete generated code", false)) {
        size_text(g.lk.gen_size, sz, sizeof sz);
        g.text.clear();
        g.text.appendf("The code generated from your game, and what the build made from it (%s), are deleted. The "
                       "engine stays.\n\nBuilding again generates it again.", sz);
        ImGui::OpenPopup("Delete the generated code?");
    }
    ImGui::EndDisabled();
    if (tools_flow_button("Pick game...", false)) pick_game(t, 0);
    if (tools_flow_button("Pick disc image...", false)) pick_game(t, 1);
    ImGui::EndDisabled();

    if (job) draw_bar();
    if (tools_warning("Build the engine?", g.text.c_str(), "Build")) start(t, T_BUILD);
    if (tools_warning("Generate the code?", g.text.c_str(), "Generate")) start(t, T_GENERATE);
    if (tools_warning("Delete the generated code?", g.text.c_str(), "Delete")) start(t, T_DEL_GEN);
    if (!job) show_status(SEC_BUILD);
    if (g.lk.log && !job) {
        char p[JSRF_PATH_MAX];
        js_path(p, sizeof p, t->game, "build/build.log");
        tools_path("Log:", p);
    }
}

static void draw_tools(tools_ctx *t)
{
    char p[JSRF_PATH_MAX], sz[32], line[200];
    int job = g.task == T_TOOLS;
    tools_heading("Tools");
    tools_hint("Python and Zig, which building uses, from their own sites.");
    tools_help_marker("Python with capstone, from GitHub and PyPI, and Zig, from PyPI, each checked by its SHA-256, "
                      "in the tools folder beside JSRF-PC. Nothing of your system's is used. Once the engine is "
                      "built you can delete them: building again downloads them again.");
    js_path(p, sizeof p, t->game, "tools");
    tools_path("Folder:", g.lk.tools_there ? p : "");
    size_text(g.lk.tools_size, sz, sizeof sz);
    if (g.lk.tools_ready) {
        snprintf(line, sizeof line, "ready, %s", sz);
        field("Tools:", TOOLS_OK, line, js_tools_id());
    } else if (g.lk.tools_there) {
        snprintf(line, sizeof line, "not whole, or another release's (%s): download them again", sz);
        field("Tools:", TOOLS_WARN, line, NULL);
    } else {
        snprintf(line, sizeof line, "not downloaded (about %d MB to download)", download_mb());
        field("Tools:", -1, line, NULL);
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.25f));

    ImGui::BeginDisabled(running());
    ImGui::BeginDisabled(!g.lk.kit);
    if (tools_flow_button("Download tools", true)) {
        g.text.clear();
        g.text.appendf("Python with capstone, and Zig: about %d MB from their own sites (GitHub and PyPI), each "
                       "checked by its SHA-256.", download_mb());
        if (g.lk.tools_there) g.text.appendf(" They replace the tools folder there now.");
        ImGui::OpenPopup("Download the tools?");
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!g.lk.tools_there);
    if (tools_flow_button("Delete tools", false)) {
        g.text.clear();
        g.text.appendf("The tools folder (%s) is deleted. The engine and the generated code stay.\n\nBuilding again "
                       "downloads them again.", sz);
        ImGui::OpenPopup("Delete the tools?");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (job) draw_bar();
    if (tools_warning("Download the tools?", g.text.c_str(), "Download")) start(t, T_TOOLS);
    if (tools_warning("Delete the tools?", g.text.c_str(), "Delete")) start(t, T_DEL_TOOLS);
    if (!job) show_status(SEC_TOOLS);
}

/* ---- the page ------------------------------------------------------------------------------ */

static void draw(tools_ctx *t)
{
    int frame = ImGui::GetFrameCount();
    if (!g.inited) {
        g.inited = 1;
        g.drawn_frame = -2;
        g.mu = SDL_CreateMutex();
    }
    if (running() && SDL_AtomicGet(&g.done)) finished();
    if (!running() && frame - g.drawn_frame > 1) {          /* just shown */
        start(t, g.first_done ? T_LOOK : T_FIRST);
        g.first_done = 1;
    }
    g.drawn_frame = frame;
    if (running()) {
        t->busy = 1;           /* frames keep coming, for the bar and to see the task end */
        t->wait_ms = 30;       /* about 30 a second is plenty for it */
    }
    if (!g.looked) {
        ImGui::TextDisabled("Looking...");
        return;
    }
    draw_engine(t);
    draw_build(t);
    draw_tools(t);
}

static void dropped(tools_ctx *t, const char *path)
{
    (void)t;
    if (running()) return;
    snprintf(g.game, sizeof g.game, "%s", path);
    g.st[0] = 0;
}

void tools_build_closing(void)
{
    if (!g.thread) return;
    g.cancel = 1;
    js_kill_child();
    SDL_WaitThread(g.thread, NULL);
    g.thread = NULL;
    g.task = T_NONE;
}

extern const tools_page tools_build_page = { "Build", draw, dropped };
