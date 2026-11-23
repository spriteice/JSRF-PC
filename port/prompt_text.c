/* The game's lines that name a control, drawn with the button prompts'
 * picture in the name's place (prompt_text.h). Plain C on guest memory, so
 * a test links it (tests/prompt_text). */
#include "prompt_text.h"

#include <stdio.h>
#include <string.h>

#define GUEST_LOW 0x10000u   /* below it, no guest address the game uses */

/* The lines, by their address in default.xbe (the US release), as the
 * game's files have them: each name's first byte, its bytes, what it names
 * and its bytes' check. Found by reading the game's strings and the code
 * that draws them; tests/prompt_text checks the table's shape, and the log
 * says once if a line in the game's memory isn't as it says. Where each is
 * drawn:
 *
 * - go back: a table of five by language at 0x2221F8, read at 0x115490 and
 *   0x115D4C, drawn through the queuing call, its right end at 576;
 * - the test run's start: a table of two by language at 0x2250D0, read at
 *   0x11A6A2, the queuing call at (300, 404);
 * - the message box's (class 0x1CC660): five tables of fifteen by language,
 *   from 0x1FBCA8, filled in by sprintf and shown by sub_0006F450; the box's
 *   draw draws its text at once at 0x6EE3F, from (128, 140);
 * - the graffiti save screen's: one line a call, drawn at once from
 *   0x12FB4C to 0x134212, from x 65;
 * - the graffiti editor's help: six tables of eighteen by language, from
 *   0x227E30 (Japanese, English, French, German, Spanish, Italian; the
 *   table of them at 0x227FE0), the line for the editor's mode drawn at
 *   once by its ticker, sub_001284F0, scrolling from x 78 at y 42. Eleven
 *   to thirteen of a language's lines name a control (72 in all); the
 *   others ("Save", "You wanna erase this?") aren't here. Each ends in
 *   full-width spaces, the gap before its next copy. */
const PromptTextLine prompt_text_lines[] = {
    { 0x001D2C8Cu, 1, 1, { { 0, 2, PROMPT_TEXT_B, 0x1F12u } } },   /* go back, Japanese */
    { 0x001D2C6Cu, 1, 1, { { 10, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* go back, English */
    { 0x001D2C48u, 1, 1, { { 10, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* go back, its (Fr) copy */
    { 0x001D2C24u, 1, 1, { { 10, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* go back, its (Ger) copy */
    { 0x001D2C00u, 1, 1, { { 10, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* go back, its (Esp) copy */
    { 0x001D276Cu, 0, 1, { { 0, 2, PROMPT_TEXT_A, 0x1981u } } },   /* the test run's start, Japanese */
    { 0x001D7BF4u, 0, 1, { { 10, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* the test run's start, English */
    { 0x001CBAB0u, 0, 1, { { 41, 2, PROMPT_TEXT_A, 0x1981u } } },   /* box: the save can't be read, Japanese */
    { 0x001CBA30u, 0, 1, { { 99, 2, PROMPT_TEXT_A, 0x1981u } } },   /* box: a memory unit taken out, Japanese */
    { 0x001CB958u, 0, 2, {   /* box: not enough memory, Japanese (A and B) */
        { 126, 2, PROMPT_TEXT_A, 0x1981u }, { 164, 2, PROMPT_TEXT_B, 0x1F12u } } },
    { 0x001CB884u, 0, 1, { { 24, 2, PROMPT_TEXT_A, 0x1981u } } },   /* box: done saving, Japanese */
    { 0x001CB718u, 0, 1, { { 58, 2, PROMPT_TEXT_A, 0x1981u } } },   /* box: graffiti can't be read, Japanese */
    { 0x001CB6C0u, 0, 1, { { 49, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* box: the save can't be read, English */
    { 0x001CB640u, 0, 1, {   /* box: a memory unit taken out, Japanese (the English table's) */
        { 97, 2, PROMPT_TEXT_A, 0x1981u } } },
    { 0x001CB570u, 0, 2, {   /* box: not enough memory, English (A and B) */
        { 97, 1, PROMPT_TEXT_A, 0x32C7u }, { 149, 1, PROMPT_TEXT_B, 0x3C8Eu } } },
    { 0x001CB4C0u, 0, 1, { { 24, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* box: done saving, English */
    { 0x001CB370u, 0, 1, { { 37, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* box: graffiti can't be read, English */
    { 0x001CB2DCu, 0, 1, { { 38, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* box: the saved data can't be read */
    { 0x001CB210u, 0, 2, {   /* box: not enough memory, the other tables' (A and B) */
        { 97, 1, PROMPT_TEXT_A, 0x32C7u }, { 151, 1, PROMPT_TEXT_B, 0x3C8Eu } } },
    { 0x001DDB80u, 0, 1, { { 37, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, Italian */
    { 0x001DDBACu, 0, 1, { { 19, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, Italian */
    { 0x001DDBDCu, 0, 1, { { 18, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Italian */
    { 0x001DDC6Cu, 0, 1, { { 19, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, Spanish */
    { 0x001DDC9Cu, 0, 1, { { 18, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Spanish */
    { 0x001DDD6Cu, 0, 1, { { 26, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, German */
    { 0x001DDD90u, 0, 1, { { 11, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, German */
    { 0x001DDE08u, 0, 1, { { 22, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, French */
    { 0x001DDE50u, 0, 1, { { 25, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, French */
    { 0x001DDE84u, 0, 1, { { 21, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, French */
    { 0x001DDF0Cu, 0, 1, { { 10, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, English */
    { 0x001DDF60u, 0, 1, { { 13, 1, PROMPT_TEXT_B, 0x3C8Eu } } },   /* graffiti save screen, English */
    { 0x001DDF94u, 0, 1, { { 10, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, English */
    { 0x001DE028u, 0, 1, { { 4, 2, PROMPT_TEXT_B, 0x1F12u } } },   /* graffiti save screen, Japanese */
    { 0x001DE080u, 0, 2, {   /* graffiti save screen, Japanese (A and B) */
        { 0, 2, PROMPT_TEXT_A, 0x1981u }, { 36, 2, PROMPT_TEXT_B, 0x1F12u } } },
    { 0x001DE240u, 0, 1, { { 0, 2, PROMPT_TEXT_A, 0x1981u } } },   /* graffiti save screen, Japanese */
    { 0x001DE374u, 0, 1, { { 10, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, English */
    { 0x001DE430u, 0, 1, { { 12, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, French */
    { 0x001DE478u, 0, 1, { { 21, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, French */
    { 0x001DE504u, 0, 1, { { 11, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, German */
    { 0x001DE544u, 0, 1, { { 11, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, German */
    { 0x001DE5C0u, 0, 1, { { 9, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Spanish */
    { 0x001DE5FCu, 0, 1, { { 18, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Spanish */
    { 0x001DE678u, 0, 1, { { 8, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Italian */
    { 0x001DE6B8u, 0, 1, { { 18, 1, PROMPT_TEXT_A, 0x32C7u } } },   /* graffiti save screen, Italian */
    { 0x001DD0D8u, 0, 4, {   /* the editor's help, Japanese 0 */
        { 0, 10, PROMPT_TEXT_DPAD, 0x2B6Eu }, { 34, 16, PROMPT_TEXT_LSTICK, 0x7661u },
        { 70, 8, PROMPT_TEXT_LTRIGGER, 0xFA0Fu }, { 106, 8, PROMPT_TEXT_RTRIGGER, 0xEA00u } } },
    { 0x001DD088u, 0, 2, {   /* the editor's help, Japanese 1 */
        { 0, 16, PROMPT_TEXT_LSTICK, 0x7661u }, { 34, 8, PROMPT_TEXT_RTRIGGER, 0xEA00u } } },
    { 0x001DD028u, 0, 3, {   /* the editor's help, Japanese 2 */
        { 0, 16, PROMPT_TEXT_LSTICK, 0x7661u }, { 34, 8, PROMPT_TEXT_LTRIGGER, 0xFA0Fu },
        { 72, 8, PROMPT_TEXT_RTRIGGER, 0xEA00u } } },
    { 0x001DCFF8u, 0, 2, {   /* the editor's help, Japanese 3 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 24, 2, PROMPT_TEXT_A, 0x1981u } } },
    { 0x001DCF58u, 0, 2, {   /* the editor's help, Japanese 8 */
        { 0, 10, PROMPT_TEXT_DPAD, 0x2B6Eu }, { 28, 2, PROMPT_TEXT_A, 0x1981u } } },
    { 0x001DCF08u, 0, 2, {   /* the editor's help, Japanese 9 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 32, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x7661u } } },
    { 0x001DCEB8u, 0, 2, {   /* the editor's help, Japanese 10 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 32, 16, PROMPT_TEXT_LSTICK, 0x7661u } } },
    { 0x001DCE68u, 0, 2, {   /* the editor's help, Japanese 11 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 32, 16, PROMPT_TEXT_LSTICK, 0x7661u } } },
    { 0x001DCE18u, 0, 2, {   /* the editor's help, Japanese 12 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 32, 16, PROMPT_TEXT_LSTICK, 0x7661u } } },
    { 0x001DCDE0u, 0, 1, { { 0, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x7661u } } },   /* the editor's help, Japanese 13 */
    { 0x001DCD90u, 0, 2, {   /* the editor's help, Japanese 14 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 32, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x7661u } } },
    { 0x001DCD48u, 0, 2, {   /* the editor's help, Japanese 15 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 30, 2, PROMPT_TEXT_A, 0x1981u } } },
    { 0x001DCD10u, 0, 2, {   /* the editor's help, Japanese 17 */
        { 0, 10, PROMPT_TEXT_DPAD_ACROSS, 0x2B6Eu }, { 30, 2, PROMPT_TEXT_A, 0x1981u } } },
    { 0x001DCC30u, 0, 4, {   /* the editor's help, English 0 */
        { 8, 5, PROMPT_TEXT_DPAD, 0x5630u }, { 87, 15, PROMPT_TEXT_LSTICK, 0x3AECu },
        { 113, 12, PROMPT_TEXT_LTRIGGER, 0xE632u }, { 185, 13, PROMPT_TEXT_RTRIGGER, 0xCCA1u } } },
    { 0x001DCB90u, 0, 3, {   /* the editor's help, English 1 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 81, 15, PROMPT_TEXT_LSTICK, 0x3AECu },
        { 131, 13, PROMPT_TEXT_RTRIGGER, 0x9481u } } },
    { 0x001DCAD0u, 0, 3, {   /* the editor's help, English 2 */
        { 25, 15, PROMPT_TEXT_LSTICK, 0x3AECu }, { 54, 12, PROMPT_TEXT_LTRIGGER, 0xE632u },
        { 135, 13, PROMPT_TEXT_RTRIGGER, 0xCCA1u } } },
    { 0x001DCA68u, 0, 2, {   /* the editor's help, English 3 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 62, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DC9B0u, 0, 3, {   /* the editor's help, English 8 */
        { 10, 5, PROMPT_TEXT_DPAD, 0x5630u }, { 37, 1, PROMPT_TEXT_A, 0x32C7u },
        { 63, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DC930u, 0, 2, {   /* the editor's help, English 9 */
        { 27, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 66, 15, PROMPT_TEXT_LSTICK_UPDOWN, 0x3AECu } } },
    { 0x001DC8A0u, 0, 2, {   /* the editor's help, English 10 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 89, 15, PROMPT_TEXT_LSTICK, 0x3AECu } } },
    { 0x001DC818u, 0, 2, {   /* the editor's help, English 11 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 71, 15, PROMPT_TEXT_LSTICK, 0x3AECu } } },
    { 0x001DC798u, 0, 2, {   /* the editor's help, English 12 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 71, 15, PROMPT_TEXT_LSTICK, 0x3AECu } } },
    { 0x001DC738u, 0, 1, { { 38, 15, PROMPT_TEXT_LSTICK_UPDOWN, 0x3AECu } } },   /* the editor's help, English 13 */
    { 0x001DC698u, 0, 2, {   /* the editor's help, English 14 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 72, 15, PROMPT_TEXT_LSTICK_UPDOWN, 0x3AECu } } },
    { 0x001DC628u, 0, 2, {   /* the editor's help, English 15 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 62, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DC598u, 0, 2, {   /* the editor's help, English 17 */
        { 10, 5, PROMPT_TEXT_DPAD_ACROSS, 0x5630u }, { 88, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DC490u, 0, 4, {   /* the editor's help, French 0 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 125, 23, PROMPT_TEXT_LSTICK, 0xB248u },
        { 164, 15, PROMPT_TEXT_LTRIGGER, 0x6A6Cu }, { 223, 15, PROMPT_TEXT_RTRIGGER, 0xDD05u } } },
    { 0x001DC410u, 0, 2, {   /* the editor's help, French 1 */
        { 26, 23, PROMPT_TEXT_LSTICK, 0xB248u }, { 92, 15, PROMPT_TEXT_RTRIGGER, 0xDD05u } } },
    { 0x001DC350u, 0, 3, {   /* the editor's help, French 2 */
        { 25, 23, PROMPT_TEXT_LSTICK, 0xB248u }, { 64, 15, PROMPT_TEXT_LTRIGGER, 0x6A6Cu },
        { 126, 15, PROMPT_TEXT_RTRIGGER, 0xDD05u } } },
    { 0x001DC2D0u, 0, 2, {   /* the editor's help, French 3 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 96, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DC1C8u, 0, 2, {   /* the editor's help, French 9 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 125, 23, PROMPT_TEXT_LSTICK_UPDOWN, 0xB248u } } },
    { 0x001DC128u, 0, 2, {   /* the editor's help, French 10 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 97, 23, PROMPT_TEXT_LSTICK, 0xB248u } } },
    { 0x001DC068u, 0, 2, {   /* the editor's help, French 11 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 109, 23, PROMPT_TEXT_LSTICK, 0xB248u } } },
    { 0x001DBFB0u, 0, 2, {   /* the editor's help, French 12 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 109, 23, PROMPT_TEXT_LSTICK, 0xB248u } } },
    { 0x001DBF50u, 0, 1, { { 52, 23, PROMPT_TEXT_LSTICK_UPDOWN, 0xB248u } } },   /* the editor's help, French 13 */
    { 0x001DBE80u, 0, 2, {   /* the editor's help, French 14 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 131, 23, PROMPT_TEXT_LSTICK_UPDOWN, 0xB248u } } },
    { 0x001DBDB8u, 0, 2, {   /* the editor's help, French 17 */
        { 35, 24, PROMPT_TEXT_DPAD_ACROSS, 0x8D23u }, { 130, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DBCB8u, 0, 4, {   /* the editor's help, German 0 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 104, 16, PROMPT_TEXT_LSTICK, 0xE3A1u },
        { 141, 14, PROMPT_TEXT_LTRIGGER, 0x03EDu }, { 205, 15, PROMPT_TEXT_RTRIGGER, 0x18E7u } } },
    { 0x001DBC40u, 0, 2, {   /* the editor's help, German 1 */
        { 27, 16, PROMPT_TEXT_LSTICK, 0xE3A1u }, { 75, 15, PROMPT_TEXT_RTRIGGER, 0x18E7u } } },
    { 0x001DBB48u, 0, 3, {   /* the editor's help, German 2 */
        { 34, 17, PROMPT_TEXT_LSTICK, 0x0D91u }, { 71, 15, PROMPT_TEXT_LTRIGGER, 0xF87Eu },
        { 170, 15, PROMPT_TEXT_RTRIGGER, 0x18E7u } } },
    { 0x001DBAD0u, 0, 2, {   /* the editor's help, German 3 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 75, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DB9D8u, 0, 2, {   /* the editor's help, German 9 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 94, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0xE3A1u } } },
    { 0x001DB930u, 0, 2, {   /* the editor's help, German 10 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 133, 16, PROMPT_TEXT_LSTICK, 0xE3A1u } } },
    { 0x001DB890u, 0, 2, {   /* the editor's help, German 11 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 91, 16, PROMPT_TEXT_LSTICK, 0xE3A1u } } },
    { 0x001DB7F0u, 0, 2, {   /* the editor's help, German 12 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 91, 16, PROMPT_TEXT_LSTICK, 0xE3A1u } } },
    { 0x001DB788u, 0, 1, { { 34, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0xE3A1u } } },   /* the editor's help, German 13 */
    { 0x001DB6D8u, 0, 2, {   /* the editor's help, German 14 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 94, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0xE3A1u } } },
    { 0x001DB618u, 0, 2, {   /* the editor's help, German 17 */
        { 33, 11, PROMPT_TEXT_DPAD_ACROSS, 0x5F51u }, { 103, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DB500u, 0, 4, {   /* the editor's help, Spanish 0 */
        { 35, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 107, 25, PROMPT_TEXT_LSTICK, 0x864Cu },
        { 141, 20, PROMPT_TEXT_LTRIGGER, 0x3011u }, { 239, 18, PROMPT_TEXT_RTRIGGER, 0x9EA1u } } },
    { 0x001DB490u, 0, 2, {   /* the editor's help, Spanish 1 */
        { 22, 25, PROMPT_TEXT_LSTICK, 0x864Cu }, { 59, 18, PROMPT_TEXT_RTRIGGER, 0x9EA1u } } },
    { 0x001DB3C8u, 0, 3, {   /* the editor's help, Spanish 2 */
        { 31, 25, PROMPT_TEXT_LSTICK, 0x864Cu }, { 69, 20, PROMPT_TEXT_LTRIGGER, 0x3011u },
        { 164, 18, PROMPT_TEXT_RTRIGGER, 0x9EA1u } } },
    { 0x001DB340u, 0, 2, {   /* the editor's help, Spanish 3 */
        { 30, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 98, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DB240u, 0, 2, {   /* the editor's help, Spanish 9 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 99, 25, PROMPT_TEXT_LSTICK_UPDOWN, 0x864Cu } } },
    { 0x001DB198u, 0, 2, {   /* the editor's help, Spanish 10 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 122, 25, PROMPT_TEXT_LSTICK, 0x864Cu } } },
    { 0x001DB0E8u, 0, 2, {   /* the editor's help, Spanish 11 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 101, 25, PROMPT_TEXT_LSTICK, 0x864Cu } } },
    { 0x001DB050u, 0, 2, {   /* the editor's help, Spanish 12 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 100, 15, PROMPT_TEXT_LSTICK, 0xF644u } } },
    { 0x001DAFE8u, 0, 1, { { 9, 25, PROMPT_TEXT_LSTICK_UPDOWN, 0x864Cu } } },   /* the editor's help, Spanish 13 */
    { 0x001DAF40u, 0, 2, {   /* the editor's help, Spanish 14 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 100, 25, PROMPT_TEXT_LSTICK, 0x864Cu } } },
    { 0x001DAEB0u, 0, 2, {   /* the editor's help, Spanish 15 */
        { 12, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 107, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DAE00u, 0, 2, {   /* the editor's help, Spanish 17 */
        { 43, 18, PROMPT_TEXT_DPAD_ACROSS, 0x1435u }, { 117, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DACF8u, 0, 4, {   /* the editor's help, Italian 0 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 111, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu },
        { 138, 18, PROMPT_TEXT_LTRIGGER, 0x37A5u }, { 213, 16, PROMPT_TEXT_RTRIGGER, 0xE12Bu } } },
    { 0x001DAC98u, 0, 2, {   /* the editor's help, Italian 1 */
        { 24, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu }, { 61, 16, PROMPT_TEXT_RTRIGGER, 0xE12Bu } } },
    { 0x001DABC0u, 0, 3, {   /* the editor's help, Italian 2 */
        { 30, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu }, { 60, 18, PROMPT_TEXT_LTRIGGER, 0x37A5u },
        { 149, 16, PROMPT_TEXT_RTRIGGER, 0xE12Bu } } },
    { 0x001DAB50u, 0, 2, {   /* the editor's help, Italian 3 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 74, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DAA68u, 0, 2, {   /* the editor's help, Italian 9 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 89, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x6B0Cu } } },
    { 0x001DA9D0u, 0, 2, {   /* the editor's help, Italian 10 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 98, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu } } },
    { 0x001DA940u, 0, 2, {   /* the editor's help, Italian 11 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 87, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu } } },
    { 0x001DA8B8u, 0, 2, {   /* the editor's help, Italian 12 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 87, 16, PROMPT_TEXT_LSTICK, 0x6B0Cu } } },
    { 0x001DA860u, 0, 1, { { 35, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x6B0Cu } } },   /* the editor's help, Italian 13 */
    { 0x001DA7B8u, 0, 2, {   /* the editor's help, Italian 14 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 89, 16, PROMPT_TEXT_LSTICK_UPDOWN, 0x6B0Cu } } },
    { 0x001DA740u, 0, 2, {   /* the editor's help, Italian 15 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 86, 1, PROMPT_TEXT_A, 0x32C7u } } },
    { 0x001DA6C0u, 0, 2, {   /* the editor's help, Italian 17 */
        { 28, 17, PROMPT_TEXT_DPAD_ACROSS, 0x5AE2u }, { 89, 1, PROMPT_TEXT_A, 0x32C7u } } },
};
const int prompt_text_nlines = (int)(sizeof prompt_text_lines / sizeof prompt_text_lines[0]);

static int in_guest(uint32_t top, uint32_t va, uint32_t bytes)
{
    return va >= GUEST_LOW && va < top && bytes <= top - va;
}

uint16_t prompt_text_check(const uint8_t *bytes, uint32_t n)
{
    uint32_t h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++) h = (h ^ bytes[i]) * 0x01000193u;
    return (uint16_t)((h >> 16) ^ h);
}

/* Whether line l's names are in the game's bytes where it says. */
static int names_there(const PromptTextLine *l, const uint8_t *mem, uint32_t top)
{
    int k;
    for (k = 0; k < l->n; k++) {
        const uint32_t at = l->va + l->name[k].at;
        if (!l->name[k].bytes || !in_guest(top, at, l->name[k].bytes)) return 0;
        if (prompt_text_check(mem + at, l->name[k].bytes) != l->name[k].check) return 0;
    }
    return 1;
}

static int digit(uint8_t c) { return c >= '0' && c <= '9'; }

/* The text at text against line l's bytes, a format's %d and %s matching
 * what sprintf put in their place, the names' places in the text noted. */
static int same(const PromptTextLine *l, const uint8_t *mem, uint32_t top, uint32_t text, PromptTextMatch *m)
{
    uint32_t i = 0, j = 0;
    int k = 0;
    for (;;) {
        uint8_t f, t;
        if (i >= PROMPT_TEXT_LONGEST || j >= PROMPT_TEXT_LONGEST || !in_guest(top, l->va + i, 3u)
         || !in_guest(top, text + j, 1u))
            return 0;
        f = mem[l->va + i];
        t = mem[text + j];
        if (k < l->n && i == l->name[k].at) {
            m->name[k].at = j;
            m->name[k].bytes = l->name[k].bytes;
            m->name[k].what = l->name[k].what;
            m->name[k].pics = 0;
            k++;
        }
        if (f == '%') {
            const uint8_t spec = mem[l->va + i + 1], next = mem[l->va + i + 2];
            if (spec == 'd') {
                if (t == '-') j++;
                if (!in_guest(top, text + j, 1u) || !digit(mem[text + j])) return 0;
                while (j < PROMPT_TEXT_LONGEST && in_guest(top, text + j, 1u) && digit(mem[text + j])) j++;
            } else if (spec == 's') {
                while (j < PROMPT_TEXT_LONGEST && in_guest(top, text + j, 1u) && mem[text + j]
                       && mem[text + j] != next)
                    j++;
            } else if (spec == '%') {
                if (t != '%') return 0;
                j++;
            } else {
                return 0;
            }
            i += 2;
            continue;
        }
        if (f != t) return 0;
        if (!f) break;
        i++;
        j++;
    }
    m->len = j;
    m->n = k;
    return k == l->n;
}

int prompt_text_match(const PromptTextLine *table, int n, const uint8_t *mem, uint32_t top, uint32_t text,
                      PromptTextMatch *m)
{
    static unsigned char told[256];
    int i;
    if (!in_guest(top, text, 1u) || !mem[text]) return 0;
    for (i = 0; i < n; i++) {
        const PromptTextLine *l = &table[i];
        /* The first byte first: nearly every text stops there. */
        if (!in_guest(top, l->va, 1u) || mem[l->va] != mem[text]) continue;
        if (!same(l, mem, top, text, m)) continue;
        if (!names_there(l, mem, top)) {
            if (i < (int)sizeof told && !told[i]) {
                told[i] = 1;
                fprintf(stderr, "[INPUT] button prompts: the game's line at %08X hasn't the names the "
                        "table has: left as it is\n", (unsigned)l->va);
            }
            continue;
        }
        m->line = i;
        m->right = l->right;
        return 1;
    }
    return 0;
}

/* ---- what stands in a name's place --------------------------------------- */

/* A layout's first input on control c, or INPUT_NONE, as the icons take it
 * (src/input/input_prompts.c). */
static int first(const InputLayout *l, int c)
{
    int b;
    if (!l) return INPUT_NONE;
    for (b = 0; b < INPUT_BINDS; b++)
        if (l->bind[c][b] != INPUT_NONE) return l->bind[c][b];
    return INPUT_NONE;
}

/* The D-pad's and the left stick's ways, as they are shown side by side:
 * up, left, down, right. */
enum { WAY_UP = 1, WAY_LEFT = 2, WAY_DOWN = 4, WAY_RIGHT = 8, WAYS = 4 };
static const struct { uint8_t control, pad; } k_dpad[WAYS] = {
    { INPUT_CTL_DPAD_UP, INPUT_PAD_DPAD_UP }, { INPUT_CTL_DPAD_LEFT, INPUT_PAD_DPAD_LEFT },
    { INPUT_CTL_DPAD_DOWN, INPUT_PAD_DPAD_DOWN }, { INPUT_CTL_DPAD_RIGHT, INPUT_PAD_DPAD_RIGHT },
};
static const uint8_t k_lstick[WAYS] = {
    INPUT_CTL_LSTICK_UP, INPUT_CTL_LSTICK_LEFT, INPUT_CTL_LSTICK_DOWN, INPUT_CTL_LSTICK_RIGHT,
};

/* What each name is: one control, or the D-pad's or the left stick's ways
 * (WAY_..., 0 for one control) with the pads' picture of them. */
static const struct { uint8_t control, ways, stick, group; } k_whats[PROMPT_TEXT_WHATS] = {
    [PROMPT_TEXT_A]        = { INPUT_CTL_A, 0, 0, 0 },
    [PROMPT_TEXT_B]        = { INPUT_CTL_B, 0, 0, 0 },
    [PROMPT_TEXT_LTRIGGER] = { INPUT_CTL_LTRIGGER, 0, 0, 0 },
    [PROMPT_TEXT_RTRIGGER] = { INPUT_CTL_RTRIGGER, 0, 0, 0 },
    [PROMPT_TEXT_DPAD]          = { 0, WAY_UP | WAY_LEFT | WAY_DOWN | WAY_RIGHT, 0, PROMPT_GROUP_DPAD },
    [PROMPT_TEXT_DPAD_ACROSS]   = { 0, WAY_LEFT | WAY_RIGHT, 0, PROMPT_GROUP_DPAD_ACROSS },
    [PROMPT_TEXT_DPAD_UPDOWN]   = { 0, WAY_UP | WAY_DOWN, 0, PROMPT_GROUP_DPAD_UPDOWN },
    [PROMPT_TEXT_LSTICK]        = { 0, WAY_UP | WAY_LEFT | WAY_DOWN | WAY_RIGHT, 1, PROMPT_GROUP_LSTICK },
    [PROMPT_TEXT_LSTICK_ACROSS] = { 0, WAY_LEFT | WAY_RIGHT, 1, PROMPT_GROUP_LSTICK_ACROSS },
    [PROMPT_TEXT_LSTICK_UPDOWN] = { 0, WAY_UP | WAY_DOWN, 1, PROMPT_GROUP_LSTICK_UPDOWN },
};

int prompt_text_pictures(int what, int art, const InputLayout *pad, const InputLayout *keys,
                         PromptTextPicture out[PROMPT_TEXT_PICTURES])
{
    const int w = what >= 0 && what < PROMPT_TEXT_WHATS ? what : PROMPT_TEXT_A;
    const int ways = k_whats[w].ways, stick = k_whats[w].stick;
    int n = 0, i, whole = 1;
    if (!ways) {
        out[0].show = k_whats[w].control;
        out[0].art = (uint8_t)art;
        return 1;
    }
    if (art == PROMPT_ART_KEYS) {
        /* The key on each way named; none on any, the Xbox pad's picture. */
        for (i = 0; i < WAYS; i++) {
            const int c = stick ? k_lstick[i] : k_dpad[i].control;
            if (!(ways & (1 << i)) || first(keys, c) == INPUT_NONE) continue;
            out[n].show = (uint8_t)c;
            out[n++].art = PROMPT_ART_KEYS;
        }
        if (n) return n;
        out[0].show = (uint8_t)(NV2A_PROMPT_MARK_GROUP + k_whats[w].group);
        out[0].art = PROMPT_ART_XBOX;
        return 1;
    }
    /* A pad's: its own picture of the whole, unless a way named of the
     * D-pad is on something else (a pad's stick is the game's as it is). */
    for (i = 0; i < WAYS && !stick; i++)
        if ((ways & (1 << i)) && first(pad, k_dpad[i].control) != k_dpad[i].pad) whole = 0;
    if (whole) {
        out[0].show = (uint8_t)(NV2A_PROMPT_MARK_GROUP + k_whats[w].group);
        out[0].art = (uint8_t)art;
        return 1;
    }
    for (i = 0; i < WAYS; i++) {
        if (!(ways & (1 << i))) continue;
        out[n].show = k_dpad[i].control;
        out[n++].art = (uint8_t)art;
    }
    return n;
}

void prompt_text_show(PromptTextMatch *m, int art, const InputLayout *pad, const InputLayout *keys)
{
    int k;
    for (k = 0; k < m->n; k++)
        m->name[k].pics = prompt_text_pictures(m->name[k].what, art, pad, keys, m->name[k].pic);
}

/* ---- drawing a line ------------------------------------------------------ */

/* Bytes from to to of the text drawn at (x, y) through scratch; the pen
 * where it ended. */
static int piece(const PromptTextCalls *c, uint8_t *mem, uint32_t text, uint32_t scratch, uint32_t from,
                 uint32_t to, float x, float y, float *px, float *py)
{
    memcpy(mem + scratch, mem + text + from, to - from);
    mem[scratch + (to - from)] = 0;
    return c->draw(c->ctx, scratch, x, y, px, py);
}

/* Where the text's next new line ($n) is, from from to before to; to if none. */
static uint32_t new_line(const uint8_t *mem, uint32_t text, uint32_t from, uint32_t to)
{
    uint32_t q;
    for (q = from; q + 1 < to; q++)
        if (mem[text + q] == '$' && mem[text + q + 1] == 'n') return q;
    return to;
}

/* The room name k's pictures take, at a side and a pad. */
static float room(const PromptTextMatch *m, int k, float side, float pad)
{
    return (float)m->name[k].pics * side + 2.0f * pad;
}

/* The game's measure of bytes from to to of the text, through scratch. */
static float measured(const PromptTextCalls *c, uint8_t *mem, uint32_t text, uint32_t scratch, uint32_t from,
                      uint32_t to, float xs)
{
    if (to <= from) return 0.0f;
    memcpy(mem + scratch, mem + text + from, to - from);
    mem[scratch + (to - from)] = 0;
    return c->measure(c->ctx, scratch, xs);
}

int prompt_text_draw(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float x,
                     float y, float xs, float ys, const PromptTextCalls *c)
{
    const float side = PROMPT_TEXT_SIDE * ys, pad = PROMPT_TEXT_PAD * xs, line = PROMPT_TEXT_CELL * ys;
    float cx, cy, px, py;
    uint32_t cur = 0;
    int k, p, marks = 0;
    /* Placed by its right end: left by half of what each name's pictures
     * add over the name, as the game measures it, so the line's middle
     * stays where the game's was (prompt_text.h). */
    if (m->right)
        for (k = 0; k < m->n; k++)
            x -= PROMPT_TEXT_RIGHT_MOVE
               * (room(m, k, side, pad)
                  - measured(c, mem, text, scratch, m->name[k].at, m->name[k].at + m->name[k].bytes, xs));
    cx = x;
    cy = y;
    for (k = 0; k < m->n; k++) {
        const uint32_t at = m->name[k].at, next = k + 1 < m->n ? m->name[k + 1].at : m->len;
        uint32_t q;
        float gx;
        if (at > cur) {
            if (!piece(c, mem, text, scratch, cur, at, cx, cy, &px, &py)) return 0;
        } else {
            px = cx;
            py = cy;
        }
        for (p = 0; p < m->name[k].pics; p++, marks++)
            c->mark(c->ctx, m->name[k].pic[p], px + pad + ((float)p + 0.5f) * side, py + 0.5f * line,
                    0.5f * side);
        gx = px + room(m, k, side, pad);
        cur = at + m->name[k].bytes;
        /* The words after it to the end of its line, from the pictures; the
         * lines after that from the line's start. */
        q = new_line(mem, text, cur, next);
        if (q < next) {
            if (q > cur && !piece(c, mem, text, scratch, cur, q, gx, py, &px, &py)) return 0;
            cur = q + 2u;
            cx = x;
            cy = py + line;
        } else {
            cx = gx;
            cy = py;
        }
    }
    if (cur < m->len && !piece(c, mem, text, scratch, cur, m->len, cx, cy, &px, &py)) return 0;
    return marks;
}

float prompt_text_width(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float xs,
                        float ys, const PromptTextCalls *c)
{
    const float side = PROMPT_TEXT_SIDE * ys, pad = PROMPT_TEXT_PAD * xs;
    float w = 0.0f;
    uint32_t cur = 0;
    int k;
    for (k = 0; k < m->n; k++) {
        w += measured(c, mem, text, scratch, cur, m->name[k].at, xs) + room(m, k, side, pad);
        cur = m->name[k].at + m->name[k].bytes;
    }
    return w + measured(c, mem, text, scratch, cur, m->len, xs);
}
