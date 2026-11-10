/* Run with: python3 tests/menu_style/run.py
 *
 * The menu's look (src/menu/menu_style.cpp), as the in-game menu and the
 * setup window get it:
 *
 *   - No colour is left in Dear ImGui's blue: every colour of some strength
 *     is checked for the dark style's hue, which the same check finds in
 *     the dark style itself.
 *   - White text is readable, at 4.5:1 or more, on everything the menu and
 *     the setup window draw it on, with the game behind them at its
 *     brightest and at its darkest: the bar, menus and boxes, a box's title
 *     bar, the row the pad is on, the open menu's title, buttons in each
 *     state, the number box, and the progress bar.
 *   - The bright marks (the pad's cursor, a tick in a box) stand out at 3:1
 *     or more on what they are drawn on.
 *   - The sizes: rounded corners, and rows further apart than the dark
 *     style's. A menu's row is drawn, with the pointer on it, and the green
 *     behind it has rounded corners too (Dear ImGui rounds rows by a size
 *     of their own): the same row drawn with square ones shows the check
 *     tells them apart.
 *   - The whole style is set, whatever the style held before.
 */
#include "menu_style.h"
#include "imgui.h"
#include "backends/imgui_impl_null.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail = 1; } } while (0)

/* WCAG 2's contrast: text at 4.5:1, marks that aren't text at 3:1. */
#define TEXT_CONTRAST 4.5
#define MARK_CONTRAST 3.0

struct rgb3 { double r, g, b; };

static rgb3 over(ImVec4 top, rgb3 under)
{
    rgb3 o = { top.x * top.w + under.r * (1 - top.w),
               top.y * top.w + under.g * (1 - top.w),
               top.z * top.w + under.b * (1 - top.w) };
    return o;
}

static double channel(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double luminance(rgb3 c) { return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b); }
static double contrast(rgb3 a, rgb3 b)
{
    double la = luminance(a), lb = luminance(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

/* Hue in degrees, saturation and value, as HSV. */
static void hsv(ImVec4 c, double *h, double *s, double *v)
{
    double mx = fmax(c.x, fmax(c.y, c.z)), mn = fmin(c.x, fmin(c.y, c.z)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) *h = 0;
    else if (mx == c.x) *h = 60 * fmod((c.y - c.z) / d + 6, 6);
    else if (mx == c.y) *h = 60 * ((c.z - c.x) / d + 2);
    else *h = 60 * ((c.x - c.y) / d + 4);
}

/* The dark style's blue, 0.26, 0.59, 0.98, is at 212 degrees; its other
 * accents are between 205 and 215. A colour this saturated and this bright
 * in that range is the blue; the dark style's grey borders, a little blue,
 * are below the saturation. */
#define BLUE_HUE_FROM 190.0
#define BLUE_HUE_TO   250.0
#define STRONG_SAT    0.30
#define STRONG_VALUE  0.20

static int blue_count(const ImGuiStyle &st, int say)
{
    int n = 0;
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        double h, s, v;
        hsv(st.Colors[i], &h, &s, &v);
        if (s >= STRONG_SAT && v >= STRONG_VALUE && h >= BLUE_HUE_FROM && h <= BLUE_HUE_TO) {
            if (say) fprintf(stderr, "  blue: %s (hue %.0f)\n", ImGui::GetStyleColorName(i), h);
            n++;
        }
    }
    return n;
}

/* Text on a surface, with the game behind it at its brightest and darkest. */
static void text_on(const ImGuiStyle &st, const char *what, const ImVec4 *layers, int n, double need)
{
    static const rgb3 games[2] = { { 1, 1, 1 }, { 0, 0, 0 } };
    for (const rgb3 &game : games) {
        rgb3 bg = game;
        for (int i = 0; i < n; i++) bg = over(layers[i], bg);
        rgb3 text = over(st.Colors[ImGuiCol_Text], bg);
        double c = contrast(text, bg);
        fprintf(stderr, "%-34s game %s: %.2f:1\n", what, game.r > 0 ? "white" : "black", c);
        CHECK(c >= need);
    }
}

/* A menu's row with the pointer on it, drawn by Dear ImGui at the menu's
 * style (rounding < 0) or with rows rounded by this much: how much of the
 * green covers the pixel in the top left corner of its box, from 0 to 255.
 * A square corner is all green (255), drawn with the font atlas's white
 * pixel; Dear ImGui draws a rounded one from a corner it bakes into the
 * atlas, where that pixel is left out. */
static int row_corner(float rounding)
{
    ImGuiContext *ctx = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;                /* no imgui.ini where the test runs */
    ImGui_ImplNull_Init();
    menu_style_apply(&ImGui::GetStyle());
    if (rounding >= 0.0f) ImGui::GetStyle().MenuItemRounding = rounding;
    ImDrawList *dl = NULL;
    for (int f = 0; f < 3; f++) {         /* laid out; the pointer moves onto it; drawn under it */
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f));
        ImGui::Begin("##rows", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::MenuItem("Fullscreen", NULL, true);
        ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        dl = ImGui::GetWindowDrawList();
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        io.AddMousePosEvent((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
    }

    /* The green's corner point, and the next point in from it on both
     * sides: the far corner when square, the end of the baked corner when
     * rounded. The corner pixel's middle is half a pixel in from the first. */
    const ImU32 green = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
    const ImDrawVert *corner = NULL, *in = NULL;
    int points = 0;
    for (const ImDrawVert &v : dl->VtxBuffer)
        if (v.col == green) {
            points++;
            if (!corner || v.pos.x + v.pos.y < corner->pos.x + corner->pos.y) corner = &v;
        }
    int alpha = -1;
    if (corner) {
        for (const ImDrawVert &v : dl->VtxBuffer)
            if (v.col == green && v.pos.x > corner->pos.x && v.pos.y > corner->pos.y &&
                (!in || v.pos.x + v.pos.y < in->pos.x + in->pos.y)) in = &v;
    }
    if (corner && in) {
        const ImTextureData *tex = io.Fonts->TexData;
        float u = corner->uv.x + (in->uv.x - corner->uv.x) * 0.5f / (in->pos.x - corner->pos.x);
        float v = corner->uv.y + (in->uv.y - corner->uv.y) * 0.5f / (in->pos.y - corner->pos.y);
        int x = (int)(u * tex->Width), y = (int)(v * tex->Height);
        const unsigned char *px = tex->Pixels + ((size_t)y * tex->Width + x) * tex->BytesPerPixel;
        alpha = tex->Format == ImTextureFormat_RGBA32 ? px[3] : px[0];
        fprintf(stderr, "a row the pointer is on, %s: its green in %d points, its corner pixel %d of 255\n",
                rounding < 0.0f ? "the menu's" : "square", points, alpha);
    }
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext(ctx);
    return alpha;
}

int main(void)
{
    ImGuiStyle st, dark;
    const ImVec4 *col = st.Colors;
    ImGui::StyleColorsDark(&dark);

    /* Whatever it held before: the same style. */
    {
        ImGuiStyle a, b;
        memset((void *)&a, 0x5A, sizeof a);
        menu_style_apply(&a);
        menu_style_apply(&b);
        CHECK(!memcmp(a.Colors, b.Colors, sizeof a.Colors));
        CHECK(a.FrameRounding == b.FrameRounding && a.ItemSpacing.y == b.ItemSpacing.y);
        CHECK(a.Alpha == 1.0f && a.FontScaleMain == 1.0f);
    }
    menu_style_apply(&st);

    fprintf(stderr, "the dark style's blue colours:\n");
    CHECK(blue_count(dark, 1) > 0);       /* the check finds the blue it looks for */
    fprintf(stderr, "the menu's:\n");
    CHECK(blue_count(st, 1) == 0);

    /* White text. Menus and boxes are PopupBg over the game; the bar and
     * the setup's window are MenuBarBg and WindowBg; the busy box is
     * WindowBg with a progress bar in it. */
    {
        const ImVec4 popup = col[ImGuiCol_PopupBg], bar = col[ImGuiCol_MenuBarBg], win = col[ImGuiCol_WindowBg];
        struct { const char *what; ImVec4 layers[2]; int n; } on[] = {
            { "the bar",                       { bar }, 1 },
            { "a menu or box",                 { popup }, 1 },
            { "the setup's window",            { win }, 1 },
            { "a box's title bar",             { col[ImGuiCol_TitleBgActive] }, 1 },
            { "the open menu's title",         { bar, col[ImGuiCol_Header] }, 2 },
            { "a title the pointer is on",     { bar, col[ImGuiCol_HeaderHovered] }, 2 },
            { "the row the pad is on",         { popup, col[ImGuiCol_HeaderHovered] }, 2 },
            { "a row being pressed",           { popup, col[ImGuiCol_HeaderActive] }, 2 },
            { "a chosen row",                  { popup, col[ImGuiCol_Header] }, 2 },
            { "a button",                      { popup, col[ImGuiCol_Button] }, 2 },
            { "a button the pad is on",        { popup, col[ImGuiCol_ButtonHovered] }, 2 },
            { "a button being pressed",        { popup, col[ImGuiCol_ButtonActive] }, 2 },
            { "a setup button",                { win, col[ImGuiCol_Button] }, 2 },
            { "a setup button the pad is on",  { win, col[ImGuiCol_ButtonHovered] }, 2 },
            { "the number box",                { popup, col[ImGuiCol_FrameBg] }, 2 },
            { "the progress bar, filled",      { win, col[ImGuiCol_PlotHistogram] }, 2 },
            { "the progress bar, empty",       { win, col[ImGuiCol_FrameBg] }, 2 },
        };
        for (auto &o : on) text_on(st, o.what, o.layers, o.n, TEXT_CONTRAST);
    }

    /* The bright marks, on what they're drawn on, with the game black
     * behind (the popup's own colour at its darkest is the hardest case for
     * a dark mark; these are bright). */
    {
        const rgb3 black = { 0, 0, 0 };
        rgb3 popup = over(col[ImGuiCol_PopupBg], black);
        rgb3 frame = over(col[ImGuiCol_FrameBg], popup);
        double cursor = contrast(over(col[ImGuiCol_NavCursor], popup), popup);
        double tick = contrast(over(col[ImGuiCol_CheckMark], frame), frame);
        fprintf(stderr, "the pad's cursor on a menu: %.2f:1, a tick in a box: %.2f:1\n", cursor, tick);
        CHECK(cursor >= MARK_CONTRAST);
        CHECK(tick >= MARK_CONTRAST);
    }

    /* The sizes. */
    CHECK(st.WindowRounding > 0 && st.PopupRounding > 0 && st.FrameRounding > 0 && st.GrabRounding > 0);
    CHECK(st.ItemSpacing.y > dark.ItemSpacing.y);
    CHECK(st.FramePadding.y > dark.FramePadding.y);
    CHECK(st.MenuItemRounding == st.FrameRounding);
    CHECK(row_corner(0.0f) == 255);    /* the check finds a square corner */
    CHECK(row_corner(-1.0f) == 0);     /* the menu's are rounded */

    if (g_fail) return 1;
    fprintf(stderr, "menu_style: ok\n");
    return 0;
}
