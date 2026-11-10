/*
 * menu_style.cpp -- the menu's look (menu_style.h).
 *
 * Dear ImGui's dark style makes its accents from one blue. Here each of them
 * is made from one of the original Xbox's two greens instead:
 *
 *   - the console's green, bright, for thin marks on the dark: the pad's
 *     cursor, a slider's grab, a tick in a box, selected text;
 *   - its dark green behind white text: the row the pad is on, the open
 *     menu's title, buttons, a box's title bar, the progress bar.
 *
 * White text stays at 4.5:1 or more on everything it is drawn on, and the
 * bright marks at 3:1 or more on the dark (tests/menu_style). The greens are
 * the nearest matches to the console's brand colours, Pantone 7488 C and
 * 364 C.
 *
 * Built with -fno-exceptions -fno-rtti -fno-threadsafe-statics, as Dear
 * ImGui is: nothing here may need the C++ runtime library.
 */
#include "menu_style.h"
#include "imgui.h"
#include "imgui_internal.h"   /* ImLerp on colours */

static constexpr ImVec4 rgb(int r, int g, int b)
{
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

static ImVec4 with_alpha(ImVec4 c, float a)
{
    c.w = a;
    return c;
}

/* The original Xbox's greens: the console's own (#92C83E), and its dark one
 * (#397C2D). */
static constexpr ImVec4 k_green      = rgb(0x92, 0xC8, 0x3E);
static constexpr ImVec4 k_dark_green = rgb(0x39, 0x7C, 0x2D);
static constexpr ImVec4 k_black      = rgb(0x00, 0x00, 0x00);

/* How much of the dark green a frame takes when the pointer is on it, and
 * when it is held, over the dark style's frame colour. */
#define STYLE_FRAME_HOVERED   0.40f
#define STYLE_FRAME_ACTIVE    0.65f
/* A box's title bar, from the dark style's title colour to the dark green. */
#define STYLE_TITLE_ACTIVE    0.80f
/* Behind white text: a button at rest, the open menu's title on the bar, and
 * the row the pad or pointer is on. The dark style's alphas for these (0.40,
 * 0.31 and 0.80) were for its lighter blue. */
#define STYLE_BUTTON_ALPHA    0.60f
#define STYLE_HEADER_ALPHA    0.70f
#define STYLE_HOVERED_ALPHA   0.90f
/* Held down (a button or row being pressed): the dark green darkened, so
 * the white on it gets clearer, not fainter. */
#define STYLE_PRESSED_DARKEN  0.25f

/* The colours the dark style makes from others, made again from the greens
 * with the dark style's own mixes (StyleColorsDark, imgui_draw.cpp). */
#define DARK_CHECKBOX_MIX     0.65f
#define DARK_TAB_MIX          0.80f
#define DARK_TAB_SELECTED_MIX 0.60f
#define DARK_TAB_DIMMED_MIX   0.80f
#define DARK_TAB_DIMMED_SELECTED_MIX 0.40f

/* Sizes, at Dear ImGui's own scale. Corners rounded on boxes, menus,
 * buttons, and on the green behind a row and the pad's outline round it
 * (the row the pad or the pointer is on, and the open menu's title), which
 * Dear ImGui rounds by a size of its own, MenuItemRounding;
 * the bar stays square (Dear ImGui keeps a main menu bar's corners
 * square). Rows a little further apart and a little taller than the dark
 * style's 4 and 3, so a pad's cursor is easy to follow, and a box's edge a
 * little further from what is in it than its 8. */
#define STYLE_ROUNDING        4.0f
#define STYLE_ROW_GAP         6.0f
#define STYLE_FRAME_PAD_Y     4.0f
#define STYLE_WINDOW_PAD      10.0f

void menu_style_apply(ImGuiStyle *st)
{
    ImVec4 *c = st->Colors;
    const ImVec4 pressed = ImLerp(k_dark_green, k_black, STYLE_PRESSED_DARKEN);

    *st = ImGuiStyle();   /* Dear ImGui's sizes, and its dark style */

    st->WindowRounding = st->ChildRounding = st->PopupRounding = STYLE_ROUNDING;
    st->FrameRounding = st->GrabRounding = st->MenuItemRounding = STYLE_ROUNDING;
    st->ItemSpacing.y = STYLE_ROW_GAP;
    st->FramePadding.y = STYLE_FRAME_PAD_Y;
    st->WindowPadding = ImVec2(STYLE_WINDOW_PAD, STYLE_WINDOW_PAD);

    c[ImGuiCol_FrameBgHovered]       = ImLerp(c[ImGuiCol_FrameBg], k_dark_green, STYLE_FRAME_HOVERED);
    c[ImGuiCol_FrameBgActive]        = ImLerp(c[ImGuiCol_FrameBg], k_dark_green, STYLE_FRAME_ACTIVE);
    c[ImGuiCol_TitleBgActive]        = ImLerp(c[ImGuiCol_TitleBg], k_dark_green, STYLE_TITLE_ACTIVE);
    c[ImGuiCol_CheckMark]            = k_green;
    c[ImGuiCol_SliderGrab]           = k_dark_green;
    c[ImGuiCol_SliderGrabActive]     = k_green;
    c[ImGuiCol_Button]               = with_alpha(k_dark_green, STYLE_BUTTON_ALPHA);
    c[ImGuiCol_ButtonHovered]        = k_dark_green;
    c[ImGuiCol_ButtonActive]         = pressed;
    c[ImGuiCol_Header]               = with_alpha(k_dark_green, STYLE_HEADER_ALPHA);
    c[ImGuiCol_HeaderHovered]        = with_alpha(k_dark_green, STYLE_HOVERED_ALPHA);
    c[ImGuiCol_HeaderActive]         = pressed;
    c[ImGuiCol_SeparatorHovered]     = with_alpha(k_dark_green, c[ImGuiCol_SeparatorHovered].w);
    c[ImGuiCol_SeparatorActive]      = k_dark_green;
    c[ImGuiCol_ResizeGrip]           = with_alpha(k_green, c[ImGuiCol_ResizeGrip].w);
    c[ImGuiCol_ResizeGripHovered]    = with_alpha(k_green, c[ImGuiCol_ResizeGripHovered].w);
    c[ImGuiCol_ResizeGripActive]     = with_alpha(k_green, c[ImGuiCol_ResizeGripActive].w);
    c[ImGuiCol_PlotHistogram]        = k_dark_green;   /* its "57 of 121" can sit on it */
    c[ImGuiCol_PlotHistogramHovered] = k_dark_green;
    c[ImGuiCol_TextLink]             = k_green;
    c[ImGuiCol_TextSelectedBg]       = with_alpha(k_green, c[ImGuiCol_TextSelectedBg].w);
    c[ImGuiCol_NavCursor]            = k_green;

    c[ImGuiCol_CheckboxSelectedBg]   = ImLerp(c[ImGuiCol_FrameBg], c[ImGuiCol_FrameBgHovered], DARK_CHECKBOX_MIX);
    c[ImGuiCol_TabHovered]           = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_Tab]                  = ImLerp(c[ImGuiCol_Header], c[ImGuiCol_TitleBgActive], DARK_TAB_MIX);
    c[ImGuiCol_TabSelected]          = ImLerp(c[ImGuiCol_HeaderActive], c[ImGuiCol_TitleBgActive], DARK_TAB_SELECTED_MIX);
    c[ImGuiCol_TabSelectedOverline]  = c[ImGuiCol_HeaderActive];
    c[ImGuiCol_TabDimmed]            = ImLerp(c[ImGuiCol_Tab], c[ImGuiCol_TitleBg], DARK_TAB_DIMMED_MIX);
    c[ImGuiCol_TabDimmedSelected]    = ImLerp(c[ImGuiCol_TabSelected], c[ImGuiCol_TitleBg], DARK_TAB_DIMMED_SELECTED_MIX);
}
