/**
 * A window on macOS, without a third-party toolkit.
 *
 * The renderer builds its frame in an offscreen framebuffer on the thread
 * that drives the pushbuffer. Getting that frame onto the screen looks like
 * it should be one blit into the window's back buffer, and it is not: since
 * 10.14 every view is layer-backed, and a layer-backed OpenGL view does not
 * present anything drawn into its context from another thread. Everything
 * reports success -- the blit lands, the back buffer measurably contains the
 * picture, the framebuffer is complete, glGetError is clean -- and the window
 * stays black. Four separate measurements said "fine" before the fifth said
 * "the view IS layer-backed".
 *
 * So the frame crosses to the main thread as pixels instead, and the view
 * draws it there, in drawRect:, in its own context, which is exactly where
 * AppKit expects drawing to happen. That costs one readback and one texture
 * upload a frame -- about a megabyte at this resolution, nothing on this
 * hardware -- and in exchange there is no shared context, no cross-thread GL,
 * and no dependence on undocumented layer behaviour.
 *
 * The view's context is deliberately a legacy one. It is not shared with the
 * renderer's core-profile context and never has to be, so the simplest thing
 * that can draw a full-screen textured quad is the right thing.
 *
 * As with the gamepad shim, this file includes none of the project's headers:
 * the Windows compatibility layer typedefs BOOL as int, Objective-C's is a
 * signed char, and Carbon owns GetCurrentThread.
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static NSWindow *g_window;
static int       g_w, g_h;
static int       g_should_close;
static int       g_open;

/* The frame in flight, handed over under a lock. Double-buffered so the
 * renderer is never blocked by the main thread being mid-upload. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char  *g_px;
static int             g_px_w, g_px_h;
static int             g_px_new;
static GLuint          g_tex;

@interface NvGLView : NSOpenGLView
@end

static NvGLView *g_view;

/*
 * The Metal renderer's view: a layer the renderer draws the finished frame
 * into itself, from its own thread, which is what CAMetalLayer is for -- no
 * readback, no hand-over of pixels, no drawing on the main thread. The view
 * only keeps the layer's drawable the size of its backing store.
 */
@interface NvMetalView : NSView
@end

static NvMetalView  *g_mview;
static CAMetalLayer *g_mlayer;

/* The frame is sRGB: the console's output is video with BT.709 primaries,
 * which are sRGB's, and every colour the title chose was chosen on that.
 * An OpenGL layer is not colour-matched unless it is told what its content
 * is ("if nil, no colormatching occurs"), so on a wide-gamut panel -- every
 * recent Mac's own screen is Display P3 -- the numbers went to the display
 * as P3 and every colour came out more saturated than the title made it,
 * this title's greens and yellows most visibly. Tagging the layer (and the
 * window) sRGB has the window server convert, as it does for everything
 * else on screen. Done once the view has its layer; RECOMP_WINDOW_SRGB=0
 * leaves it untagged, as before. */
static void nv_window_colour_space(void)
{
    static int on = -1, done;
    CALayer *layer;
    NSColorSpace *scr;
    if (done || !g_window || (!g_view && !g_mview)) return;
    if (on < 0) { const char *e = getenv("RECOMP_WINDOW_SRGB"); on = e ? atoi(e) : 1; }
    layer = g_mview ? (CALayer *)g_mlayer : [g_view layer];
    if (!layer) return;                    /* not yet: try at the next draw */
    done = 1;
    scr = [[g_window screen] colorSpace];
    if (on) {
        [g_window setColorSpace:[NSColorSpace sRGBColorSpace]];
        if ([layer isKindOfClass:[CAOpenGLLayer class]]) {
            CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            ((CAOpenGLLayer *)layer).colorspace = cs;
            CGColorSpaceRelease(cs);
        } else if ([layer isKindOfClass:[CAMetalLayer class]]) {
            CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            ((CAMetalLayer *)layer).colorspace = cs;
            CGColorSpaceRelease(cs);
        }
    }
    fprintf(stderr, "[WIN] screen colour space: %s; window %s; layer %s%s\n",
            scr ? [[scr localizedName] UTF8String] : "?",
            [g_window colorSpace] ? [[[g_window colorSpace] localizedName] UTF8String] : "(screen's)",
            [NSStringFromClass([layer class]) UTF8String],
            !on ? " (untagged: RECOMP_WINDOW_SRGB=0)"
                : ([layer isKindOfClass:[CAOpenGLLayer class]]
                   || [layer isKindOfClass:[CAMetalLayer class]]) ? " tagged sRGB"
                : " (not a GL layer: window only)");
    fflush(stderr);
}

@implementation NvGLView

- (BOOL)isOpaque          { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirty
{
    NSOpenGLContext *ctx = [self openGLContext];
    NSRect back = [self convertRectToBacking:[self bounds]];
    int vw = (int)back.size.width, vh = (int)back.size.height;
    unsigned char *px = NULL;
    int pw = 0, ph = 0;

    (void)dirty;
    nv_window_colour_space();
    [ctx makeCurrentContext];

    pthread_mutex_lock(&g_lock);
    if (g_px_new && g_px) { px = g_px; pw = g_px_w; ph = g_px_h; g_px_new = 0; }
    pthread_mutex_unlock(&g_lock);

    if (px) {
        if (!g_tex) {
            glGenTextures(1, &g_tex);
            glBindTexture(GL_TEXTURE_2D, g_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0,
                     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
    }

    glViewport(0, 0, vw, vh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g_tex) {
        /* The frame keeps its shape. The window can be resized and can go
         * full screen, and a 4:3 picture stretched over a 16:10 screen is not
         * the picture: black bars instead, the frame centred between them. */
        static int lw = 640, lh = 480;
        int fw, fh, dw = vw, dh = vh;
        extern int nv2a_wide_scene(void);
        extern float nv2a_wide_aspect(void);
        if (pw > 0 && ph > 0) { lw = pw; lh = ph; }
        fw = lw; fh = lh;
        /* Widescreen: the frame holds a 16:9 view squeezed into 4:3
         * (nv2a_widescreen in nv2a_gl.c) -- shown at 16:9 it is the right
         * shape again. A scene the game kept 4:3 (the title) keeps its
         * shape too, between black bars. */
        if (nv2a_wide_scene()) { fw = (int)(nv2a_wide_aspect() * 900.0f + 0.5f); fh = 900; }
        if ((long)vw * fh > (long)vh * fw) dw = (int)((long)vh * fw / fh);
        else                               dh = (int)((long)vw * fh / fw);
        glViewport((vw - dw) / 2, (vh - dh) / 2, dw, dh);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        /* glReadPixels hands back row 0 first and row 0 is the bottom of the
         * GL image -- and the renderer draws the picture the GL way up (its
         * vertex stage sends screen y = 0 to NDC +1), so the bottom of the
         * quad takes v = 0 and the picture comes out upright.
         *
         * This used to be the other way round, which showed any surface the
         * title drew directly upside down. It looked right only because the
         * renderer also turned every render-to-texture copy over, and the
         * surface on screen had always been through exactly one copy. The
         * boost dash makes two copies; nv2a_gl.c now samples surfaces the
         * right way up, and this shows the frame the right way up. */
        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    [ctx flushBuffer];

    /* That the view drew at all is the one thing the previous design could
     * not claim. Said three times, then never again. */
    { static int said;
      if (said < 3) {
          said++;
          fprintf(stderr, "[WIN] view drew #%d: %dx%d from a %dx%d frame\n",
                  said, vw, vh, pw, ph);
          fflush(stderr);
      } }
}
@end

@implementation NvMetalView

- (BOOL)isOpaque          { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer  { return YES; }

- (CALayer *)makeBackingLayer
{
    CAMetalLayer *l = [CAMetalLayer layer];
    const char *v = getenv("RECOMP_VSYNC");
    l.device = MTLCreateSystemDefaultDevice();
    l.pixelFormat = MTLPixelFormatBGRA8Unorm;
    l.framebufferOnly = YES;
    l.opaque = YES;
    /* In step with the display, like the OpenGL view's swap interval of
     * one; RECOMP_VSYNC=0 lets frames go as soon as they are ready. */
    l.displaySyncEnabled = (v && v[0] == '0') ? NO : YES;
    g_mlayer = l;
    return l;
}

/* The drawable follows the view, in pixels: a Retina window has twice as
 * many as points, and the renderer draws the frame letterboxed into it. */
- (void)nvSyncDrawable
{
    NSSize sz = [self convertSizeToBacking:[self bounds].size];
    if (!g_mlayer) return;
    g_mlayer.contentsScale = [[self window] backingScaleFactor] > 0
                           ? [[self window] backingScaleFactor] : 1.0;
    if (sz.width >= 1 && sz.height >= 1) {
        CGSize was = g_mlayer.drawableSize;
        if (was.width != sz.width || was.height != sz.height)
            fprintf(stderr, "[WIN] drawable %.0fx%.0f\n", sz.width, sz.height);
        g_mlayer.drawableSize = sz;
    }
}
- (void)setFrameSize:(NSSize)s { [super setFrameSize:s]; [self nvSyncDrawable]; }
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; [self nvSyncDrawable]; }
- (void)viewDidMoveToWindow { [super viewDidMoveToWindow]; [self nvSyncDrawable]; nv_window_colour_space(); }
@end

/* Whether the window should be made for Metal: RECOMP_BACKEND if it says,
 * else the stored choice -- and only where there is a Metal device. */
static int nv_window_wants_metal(void)
{
    const char *v = getenv("RECOMP_BACKEND");
    extern int nv_window_pref_metal(void);
    int want = (v && *v) ? !strcmp(v, "metal") : nv_window_pref_metal();
    if (!want) return 0;
    @autoreleasepool {
        id<MTLDevice> d = MTLCreateSystemDefaultDevice();
        int ok = d != nil;
#if !__has_feature(objc_arc)
        [d release];
#endif
        return ok;
    }
}

/* For the renderer (nv2a_metal.m): the layer to present into, or NULL when
 * the window is not a Metal one. */
void *nv_window_metal_layer(void);
void *nv_window_metal_layer(void)
{
    return (g_open && g_mlayer) ? (__bridge void *)g_mlayer : NULL;
}

static NvGLView *make_gl_view(NSRect frame);

/* The renderer asked for Metal, the window was made for it, and the device
 * would not come up after all: put an OpenGL view in its place, so that the
 * frames OpenGL hands over as pixels have somewhere to go. */
void nv_window_use_gl(void);
void nv_window_use_gl(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NvGLView *v;
        if (!g_window || g_view) return;
        v = make_gl_view([[g_window contentView] frame]);
        if (!v) return;
        g_view = v;
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];
        g_mview = nil;
        g_mlayer = nil;
        fprintf(stderr, "[WIN] Metal unavailable: the window draws with OpenGL\n");
    });
}

@interface NvWindowDelegate : NSObject <NSWindowDelegate>
@end
/* Whether any of the window can be seen: the Metal renderer does not ask
 * a hidden or minimised window's layer for drawables, which it would only
 * hand out once the window came back -- holding the game up meanwhile. */
static volatile int g_visible = 1;
int nv_window_visible(void);
int nv_window_visible(void) { return g_visible; }

/* The window's display, in refreshes a second (0: not known). The Metal
 * presenter paces a sixty-frame game on a faster screen by it -- a MacBook
 * Pro's runs at up to 120. Kept here, from the main thread, because the
 * presenter's thread should not ask AppKit. */
static volatile int g_refresh_hz;
int nv_window_refresh_hz(void);
int nv_window_refresh_hz(void) { return g_refresh_hz; }
static void nv_window_note_refresh(void)
{
    int hz = 0;
    NSScreen *scr = g_window ? [g_window screen] : nil;
    if (@available(macOS 12.0, *)) hz = scr ? (int)[scr maximumFramesPerSecond] : 0;
    if (hz != g_refresh_hz) {
        g_refresh_hz = hz;
        fprintf(stderr, "[WIN] display: %s, up to %d refreshes a second\n",
                scr ? [[scr localizedName] UTF8String] : "?", hz);
        fflush(stderr);
    }
}

@implementation NvWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender
{ (void)sender; g_should_close = 1; return NO; }
- (void)windowDidChangeOcclusionState:(NSNotification *)n
{
    (void)n;
    g_visible = ([g_window occlusionState] & NSWindowOcclusionStateVisible) ? 1 : 0;
}
- (void)windowDidChangeScreen:(NSNotification *)n
{
    (void)n;
    nv_window_note_refresh();
}
@end

/* ---- the keyboard as a gamepad -------------------------------------------
 *
 * A pad is the right way to play this and not everyone has one to hand, so
 * the window doubles as a controller.
 *
 * Keys are matched by POSITION (the hardware key code), not by the letter
 * the layout prints on them: the four keys under the left hand are W A S D
 * on a QWERTY Mac and Z Q S D on an AZERTY one, and they are the same four
 * key codes. The previous version matched letters and drove only the d-pad,
 * which JSRF's menus read but its skater does not -- movement is the analog
 * stick -- so on the keyboard you could reach New Game and then not move.
 * Now those keys push the left stick (and still the d-pad, for menus).
 *
 * Layout (AZERTY labels, QWERTY in brackets):
 *   Z Q S D  [W A S D]   left stick + d-pad      arrows   right stick
 *   Space  or  J         A  (jump)                K        B
 *   H                    X                        L        Y
 *   E                    R trigger (talk, spray)  A  [Q]   L trigger
 *   R                    Black                    F        White
 *   Enter                START                    Esc      BACK
 *   C  /  V              left / right stick click
 * Keys held with Command go to the menu bar instead (Cmd+Q quits).
 */
static volatile unsigned short g_kb_buttons;
static volatile unsigned char  g_kb_analog[8];

enum { KB_UP = 1u<<0, KB_DOWN = 1u<<1, KB_LEFT = 1u<<2, KB_RIGHT = 1u<<3,
       KB_START = 1u<<4, KB_BACK = 1u<<5,
       KB_R_UP = 1u<<6, KB_R_DOWN = 1u<<7, KB_R_LEFT = 1u<<8, KB_R_RIGHT = 1u<<9,
       KB_LTHUMB = 1u<<10, KB_RTHUMB = 1u<<11 };

/* Hardware key codes (HIToolbox kVK_*), named by their US-layout letter. */
enum { K_A = 0x00, K_S = 0x01, K_D = 0x02, K_F = 0x03, K_H = 0x04,
       K_C = 0x08, K_V = 0x09, K_Q = 0x0C, K_W = 0x0D, K_E = 0x0E, K_R = 0x0F,
       K_L = 0x25, K_J = 0x26, K_K = 0x28,
       K_RETURN = 0x24, K_SPACE = 0x31, K_ESCAPE = 0x35, K_PAD_ENTER = 0x4C,
       K_LEFT = 0x7B, K_RIGHT = 0x7C, K_DOWN = 0x7D, K_UP = 0x7E };

/* analog[] order, as xinput_device.c reads it: A B X Y Black White LT RT */
static void kb_apply(NSEvent *e, int down)
{
    unsigned short bit = 0;
    int analog = -1;

    switch ([e keyCode]) {
    case K_W:      bit = KB_UP;      break;
    case K_S:      bit = KB_DOWN;    break;
    case K_A:      bit = KB_LEFT;    break;
    case K_D:      bit = KB_RIGHT;   break;
    case K_UP:     bit = KB_R_UP;    break;
    case K_DOWN:   bit = KB_R_DOWN;  break;
    case K_LEFT:   bit = KB_R_LEFT;  break;
    case K_RIGHT:  bit = KB_R_RIGHT; break;
    case K_RETURN: case K_PAD_ENTER: bit = KB_START; break;
    case K_ESCAPE: bit = KB_BACK;    break;
    case K_C:      bit = KB_LTHUMB;  break;
    case K_V:      bit = KB_RTHUMB;  break;
    case K_SPACE: case K_J: analog = 0; break;   /* A */
    case K_K:      analog = 1; break;            /* B */
    case K_H:      analog = 2; break;            /* X */
    case K_L:      analog = 3; break;            /* Y */
    case K_R:      analog = 4; break;            /* Black */
    case K_F:      analog = 5; break;            /* White */
    case K_Q:      analog = 6; break;            /* L trigger */
    case K_E:      analog = 7; break;            /* R trigger */
    default: return;
    }
    if (bit) {
        if (down) g_kb_buttons |= bit;
        else      g_kb_buttons &= (unsigned short)~bit;
    }
    if (analog >= 0)
        g_kb_analog[analog] = down ? 255 : 0;
}

/* Losing focus with a key held never delivers its key-up: let go of all. */
static void kb_release_all(void)
{
    int i;
    g_kb_buttons = 0;
    for (i = 0; i < 8; i++) g_kb_analog[i] = 0;
}

int nv_window_keys(unsigned short *buttons, unsigned char *analog)
{
    int i, any;
    unsigned short b = g_kb_buttons;
    if (buttons) *buttons = b;
    any = b != 0;
    for (i = 0; i < 8; i++) {
        unsigned char v = g_kb_analog[i];
        if (analog) analog[i] = v;
        if (v) any = 1;
    }
    return any;
}


/* ---- the menu bar --------------------------------------------------------
 *
 * An app with no menu bar has no Quit, no About, and no way to change
 * anything without an environment variable -- which is fine for a diagnostic
 * run driven from a shell and not fine for something someone double-clicks.
 *
 * Resolution is the one setting worth putting in front of a player. The game
 * draws a 640x480 surface and always will; what changes is the resolution the
 * renderer works at internally -- how sharp the picture is. (The window used
 * to follow it, in points, which made 4x a window twice the screen's height;
 * it opens at one size now and can be resized or made full screen.) At 1x
 * every pixel of a 1280x960 window is a 2x2 block: the default is 2x.
 *
 * The choice is stored rather than applied live, because the render targets,
 * the depth buffer and the texture cache are all sized at startup, and tearing
 * them down mid-frame to avoid a relaunch would be a great deal of machinery
 * for a setting people change once. Choosing a scale restarts the game.
 */

static int scale_clamp(int s) { return s < 1 ? 1 : s > 4 ? 4 : s; }

int nv_window_pref_scale(void);
int nv_window_pref_scale(void)
{
    /* An explicit RECOMP_GL_SCALE in the environment wins: someone who set it
     * on a command line means it, and should not be overruled by a menu they
     * cannot see. */
    const char *e = getenv("RECOMP_GL_SCALE");
    if (e && *e) return scale_clamp(atoi(e));
    @autoreleasepool {
        NSInteger v = [[NSUserDefaults standardUserDefaults] integerForKey:@"glScale"];
        return scale_clamp(v ? (int)v : 2);
    }
}

/* Video > Widescreen (16:9), stored as the "widescreen" default; applied at
 * once (nv2a_set_widescreen). RECOMP_WIDESCREEN in the environment wins
 * (nv2a_widescreen in nv2a_gl.c reads it first). */
int nv_window_pref_widescreen(void);
int nv_window_pref_widescreen(void)
{
    @autoreleasepool {
        return [[NSUserDefaults standardUserDefaults] boolForKey:@"widescreen"] ? 1 : 0;
    }
}

/* Video > Renderer: Metal or OpenGL, stored as the "renderer" default
 * ("metal" / "opengl") and read once, when the renderer starts -- it cannot
 * change under a running frame. RECOMP_BACKEND in the environment wins
 * (gfx_init in nv2a_gl.c reads it first). */
/* Video > Renderer, as stored: Metal unless OpenGL was chosen. Metal is the
 * Mac's renderer now; OpenGL stays for a Mac where Metal will not start
 * (nv_window_use_gl) and as the other half of any comparison. */
int nv_window_pref_metal(void);
int nv_window_pref_metal(void)
{
    @autoreleasepool {
        NSString *r = [[NSUserDefaults standardUserDefaults] stringForKey:@"renderer"];
        return (r && [r isEqualToString:@"opengl"]) ? 0 : 1;
    }
}

/* The window in the picture's shape: 16:9 or 4:3, keeping its height (and
 * fitting the screen). Not while full screen -- the frame is letterboxed
 * there anyway. */
static void nv_window_shape_for_mode(void)
{
    extern int nv2a_widescreen(void);
    double aw = nv2a_widescreen() ? 16.0 : 4.0, ah = nv2a_widescreen() ? 9.0 : 3.0;
    NSRect content;
    if (!g_window) return;
    [g_window setContentAspectRatio:NSMakeSize(aw, ah)];
    if ([g_window styleMask] & NSWindowStyleMaskFullScreen) return;
    content = [g_window contentRectForFrameRect:[g_window frame]];
    {
        double h = content.size.height, w = h * aw / ah;
        NSScreen *scr = [g_window screen] ? [g_window screen] : [NSScreen mainScreen];
        if (scr) {
            NSRect vis = [scr visibleFrame];
            double maxw = vis.size.width - 40.0;
            if (w > maxw) { w = maxw; h = w * ah / aw; }
        }
        content.origin.x += (content.size.width - w) / 2.0;
        content.size.width = w;
        content.size.height = h;
        [g_window setFrame:[g_window frameRectForContentRect:content] display:YES animate:NO];
    }
}

@interface NvMenuTarget : NSObject
- (void)pickScale:(id)sender;
- (void)pickRenderer:(id)sender;
- (void)toggleWidescreen:(id)sender;
- (void)showKeys:(id)sender;
- (void)showPad:(id)sender;
@end

@implementation NvMenuTarget
/* Video > Renderer. Like the resolution, set when the game starts: the
 * window's view and every surface, texture and program belong to one API. */
- (void)pickRenderer:(id)sender
{
    NSInteger want = [(NSMenuItem *)sender tag];        /* 1 Metal, 0 OpenGL */
    int running = g_mview ? 1 : 0;
    NSAlert *a;
    NSModalResponse r;
    NSMenuItem *it;

    if (want == running && want == nv_window_pref_metal()) return;
    kb_release_all();
    a = [[NSAlert alloc] init];
    a.messageText = want ? @"Render with Metal?" : @"Render with OpenGL?";
    a.informativeText = want
        ? @"Metal is the Mac's own graphics API: the frame goes straight to the "
           "window, without the copy OpenGL needs. The renderer is chosen when the "
           "game starts; restarting now loses anything you have not saved."
        : @"OpenGL is the renderer the game used on the Mac before Metal, kept as a "
           "fallback. The renderer is chosen when the game starts; restarting now "
           "loses anything you have not saved.";
    [a addButtonWithTitle:@"Restart Now"];
    [a addButtonWithTitle:@"At Next Launch"];
    [a addButtonWithTitle:@"Cancel"];
    r = [a runModal];
    if (r != NSAlertFirstButtonReturn && r != NSAlertSecondButtonReturn) return;

    [[NSUserDefaults standardUserDefaults] setObject:(want ? @"metal" : @"opengl")
                                              forKey:@"renderer"];
    [[NSUserDefaults standardUserDefaults] synchronize];
    for (it in [[(NSMenuItem *)sender menu] itemArray])
        [it setState:([it tag] == want) ? NSControlStateValueOn : NSControlStateValueOff];
    fprintf(stderr, "[WIN] renderer: %s from the next launch\n", want ? "Metal" : "OpenGL");
    if (r != NSAlertFirstButtonReturn) return;
    {
        extern void recomp_restart_self(void) __attribute__((weak));
        /* The relaunch inherits the environment, where RECOMP_BACKEND would
         * outrank the choice just made. */
        unsetenv("RECOMP_BACKEND");
        if (recomp_restart_self) recomp_restart_self();   /* does not return on success */
        {
            NSAlert *b = [[NSAlert alloc] init];
            b.messageText = @"Restart to change the renderer";
            b.informativeText = @"Quit and launch again and the game will use the "
                                 "renderer you picked.";
            [b runModal];
        }
    }
}
- (void)toggleWidescreen:(id)sender
{
    extern int nv2a_widescreen(void);
    extern void nv2a_set_widescreen(int on);
    int on = !nv2a_widescreen();
    nv2a_set_widescreen(on);
    [[NSUserDefaults standardUserDefaults] setBool:(on ? YES : NO) forKey:@"widescreen"];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [(NSMenuItem *)sender setState:on ? NSControlStateValueOn : NSControlStateValueOff];
    nv_window_shape_for_mode();
    fprintf(stderr, "[WIN] widescreen %s\n", on ? "on (16:9)" : "off (4:3)");
}
- (void)showPad:(id)sender
{
    NSAlert *a = [[NSAlert alloc] init];
    (void)sender;
    kb_release_all();
    a.messageText = @"Game controllers";
    a.informativeText =
        @"Any controller macOS recognises works: PlayStation DualSense and "
         "DualShock 4, Xbox, Switch Pro, MFi. Pair it in System Settings > "
         "Bluetooth (or plug it in by USB), before or during the game.\n\n"
         "PlayStation / Xbox label -> original Xbox button\n\n"
         "Cross / A\t\tA - jump\n"
         "Circle / B\t\tB\n"
         "Square / X\t\tX\n"
         "Triangle / Y\t\tY\n"
         "L1 / LB\t\t\tWhite\n"
         "R1 / RB\t\t\tBlack\n"
         "L2 / LT\t\t\tL trigger\n"
         "R2 / RT\t\t\tR trigger - talk, spray\n"
         "Options / Menu\t\tStart\n"
         "Create / View\t\tBack\n"
         "Sticks, stick clicks\tthe same\n\n"
         "The keyboard keeps working alongside it. No vibration yet.";
    [a runModal];
}
- (void)showKeys:(id)sender
{
    NSAlert *a = [[NSAlert alloc] init];
    (void)sender;
    kb_release_all();
    a.messageText = @"Keyboard controls";
    a.informativeText =
        @"Keys are by position: AZERTY labels first, QWERTY in brackets.\n\n"
         "Move (left stick)\tZ Q S D  [W A S D]\n"
         "Camera (right stick)\tarrow keys\n"
         "A - jump\t\tSpace or J\n"
         "B\t\t\tK\n"
         "X\t\t\tH\n"
         "Y\t\t\tL\n"
         "R trigger - talk, spray\tE\n"
         "L trigger\t\tA  [Q]\n"
         "Black / White\t\tR / F\n"
         "Start / Back\t\tEnter / Esc\n"
         "Stick clicks\t\tC / V\n\n"
         "A game controller works too, at the same time.";
    [a runModal];
}
- (void)pickScale:(id)sender
{
    NSInteger want = [(NSMenuItem *)sender tag];
    NSInteger stored = [[NSUserDefaults standardUserDefaults] integerForKey:@"glScale"];
    NSAlert *a;
    NSModalResponse r;
    NSMenuItem *it;

    /* Changing it restarts the game, and a restart loses whatever was not
     * saved -- so it is asked, never done on the spot. (The items used to
     * carry Cmd+1..4 as shortcuts, which made one stray keystroke a restart;
     * they have none now.) */
    if (want == nv_window_pref_scale() && (!stored || stored == want)) return;
    kb_release_all();
    a = [[NSAlert alloc] init];
    a.messageText = [NSString stringWithFormat:@"Render at %ldx (%ld x %ld)?",
                     (long)want, (long)(640 * want), (long)(480 * want)];
    a.informativeText = @"The resolution is set when the game starts. "
                         "Restarting now loses anything you have not saved.";
    [a addButtonWithTitle:@"Restart Now"];
    [a addButtonWithTitle:@"At Next Launch"];
    [a addButtonWithTitle:@"Cancel"];
    r = [a runModal];
    if (r != NSAlertFirstButtonReturn && r != NSAlertSecondButtonReturn) return;

    [[NSUserDefaults standardUserDefaults] setInteger:want forKey:@"glScale"];
    [[NSUserDefaults standardUserDefaults] synchronize];
    for (it in [[(NSMenuItem *)sender menu] itemArray])
        [it setState:([it tag] == want) ? NSControlStateValueOn : NSControlStateValueOff];
    if (r != NSAlertFirstButtonReturn) return;

    {
        extern void recomp_restart_self(void) __attribute__((weak));
        if (recomp_restart_self) {
            recomp_restart_self();           /* does not return on success */
        }
        {
            NSAlert *b = [[NSAlert alloc] init];
            b.messageText = @"Restart to change the resolution";
            b.informativeText = @"The renderer is set up once at startup. "
                                 "Quit and launch again and it will be at the "
                                 "size you picked.";
            [b runModal];
        }
    }
}
@end

static NvMenuTarget *g_menu_target;

static void build_menu(void)
{
    NSString *app = [[NSProcessInfo processInfo] processName];
    NSMenu *bar = [[NSMenu alloc] init];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    NSMenu *appMenu = [[NSMenu alloc] init];
    NSMenuItem *videoItem = [[NSMenuItem alloc] init];
    NSMenu *video = [[NSMenu alloc] initWithTitle:@"Video"];
    NSMenu *res = [[NSMenu alloc] initWithTitle:@"Resolution"];
    NSMenuItem *resItem = [[NSMenuItem alloc] initWithTitle:@"Resolution"
                                                    action:nil keyEquivalent:@""];
    int cur = nv_window_pref_scale();
    int i;

    g_menu_target = [[NvMenuTarget alloc] init];

    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:app]
                       action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:app]
                       action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [bar addItem:appItem];

    for (i = 1; i <= 4; i++) {
        NSMenuItem *it = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"%dx  (%d x %d)",
                                                     i, 640 * i, 480 * i]
                   action:@selector(pickScale:)
            keyEquivalent:@""];
        [it setTag:i];
        [it setTarget:g_menu_target];
        [it setState:(i == cur) ? NSControlStateValueOn : NSControlStateValueOff];
        [res addItem:it];
    }
    [resItem setSubmenu:res];
    [video addItem:resItem];
    {
        /* Renderer: which API draws the game, checked as it is running. */
        NSMenu *ren = [[NSMenu alloc] initWithTitle:@"Renderer"];
        NSMenuItem *renItem = [[NSMenuItem alloc] initWithTitle:@"Renderer"
                                                         action:nil keyEquivalent:@""];
        NSString *names[2] = { @"OpenGL", @"Metal" };
        int r, running = g_mview ? 1 : 0;
        for (r = 1; r >= 0; r--) {
            NSMenuItem *it = [[NSMenuItem alloc] initWithTitle:names[r]
                                                        action:@selector(pickRenderer:)
                                                 keyEquivalent:@""];
            [it setTag:r];
            [it setTarget:g_menu_target];
            [it setState:(r == running) ? NSControlStateValueOn : NSControlStateValueOff];
            [ren addItem:it];
        }
        [renItem setSubmenu:ren];
        [video addItem:renItem];
    }
    {
        /* No target: it goes up the responder chain to the window, which
         * implements toggleFullScreen: and retitles the item itself. */
        NSMenuItem *fs = [[NSMenuItem alloc] initWithTitle:@"Enter Full Screen"
                                                   action:@selector(toggleFullScreen:)
                                            keyEquivalent:@"f"];
        [fs setKeyEquivalentModifierMask:(NSEventModifierFlagCommand |
                                          NSEventModifierFlagControl)];
        [video addItem:[NSMenuItem separatorItem]];
        [video addItem:fs];
    }
    {
        /* Cmd+Shift+W: Cmd+W alone closes windows everywhere else. */
        extern int nv2a_widescreen(void);
        NSMenuItem *ws = [[NSMenuItem alloc] initWithTitle:@"Widescreen (16:9)"
                                                   action:@selector(toggleWidescreen:)
                                            keyEquivalent:@"W"];
        [ws setKeyEquivalentModifierMask:(NSEventModifierFlagCommand |
                                          NSEventModifierFlagShift)];
        [ws setTarget:g_menu_target];
        [ws setState:nv2a_widescreen() ? NSControlStateValueOn : NSControlStateValueOff];
        [video addItem:ws];
    }
    [videoItem setSubmenu:video];
    [bar addItem:videoItem];

    {
        NSMenuItem *ctlItem = [[NSMenuItem alloc] init];
        NSMenu *ctl = [[NSMenu alloc] initWithTitle:@"Controls"];
        NSMenuItem *keys = [[NSMenuItem alloc] initWithTitle:@"Keyboard..."
                                                      action:@selector(showKeys:)
                                               keyEquivalent:@"k"];
        [keys setTarget:g_menu_target];
        [ctl addItem:keys];
        {
            NSMenuItem *pad = [[NSMenuItem alloc] initWithTitle:@"Game Controller..."
                                                        action:@selector(showPad:)
                                                 keyEquivalent:@""];
            [pad setTarget:g_menu_target];
            [ctl addItem:pad];
        }
        [ctlItem setSubmenu:ctl];
        [bar addItem:ctlItem];
    }

    [NSApp setMainMenu:bar];
}

/* ---- the window ---------------------------------------------------------- */

/* Must be called on the main thread: AppKit refuses to create a window
 * anywhere else, and says so by throwing. Returns 1 on success. */
/* The OpenGL view, with the context its drawRect: draws in. */
static NvGLView *make_gl_view(NSRect frame)
{
    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAColorSize,  24,
        NSOpenGLPFAAlphaSize,   8,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    NvGLView *v;
    GLint one = 1;
    if (!pf) { fprintf(stderr, "[WIN] no pixel format\n"); return nil; }
    v = [[NvGLView alloc] initWithFrame:frame pixelFormat:pf];
    if (!v) { fprintf(stderr, "[WIN] no GL view\n"); return nil; }
    [v setWantsBestResolutionOpenGLSurface:YES];
    [[v openGLContext] setValues:&one forParameter:NSOpenGLContextParameterSwapInterval];
    return v;
}

int nv_window_prepare(int width, int height, const char *title)
{
    @autoreleasepool {
        NSRect frame;
        int metal = nv_window_wants_metal();
        NSWindowStyleMask mask = NSWindowStyleMaskTitled |
                                 NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable |
                                 NSWindowStyleMaskResizable;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        /*
         * No App Nap. macOS slows an app whose window is hidden or covered:
         * its timers are bunched and fired late, a quarter of a second at a
         * time. The game paces itself and runs its music, streaming and
         * loading on timers and sleeps, so a covered window turned into a
         * stall of 250 ms every few seconds -- seen in test runs whose window
         * sat behind others -- and a player who switches away for a moment
         * comes back to a game that has been stuttering behind their back.
         * A game is latency-critical work the user started, which is what
         * this declares, for as long as the process runs.
         */
        {
            static id activity;
            if (!activity) {
                activity = [[NSProcessInfo processInfo]
                    beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                                      reason:@"Running the game"];
#if !__has_feature(objc_arc)
                [activity retain];
#endif
            }
        }

        /*
         * Never larger than the screen.
         *
         * The size used to be the internal resolution (640x480 times the
         * Video menu's scale), which AppKit counts in points -- two pixels
         * each way on a Retina screen. At 4x that was a 2560x1920-point
         * window, over twice the height of a MacBook's screen, with no way to
         * shrink it. main.c now asks for 1280x960 whatever the scale (or
         * RECOMP_WINDOW_W/H), and the frame is drawn scaled to whatever size
         * the view is, so the window only has to fit: the largest 4:3 size
         * that does. The internal resolution stays what the menu says (at 4x
         * on a Retina screen, about a pixel-for-pixel picture in a window
         * that fits). RECOMP_WINDOW_FIT=0: the size as asked.
         */
        /* Widescreen: the same height, 16:9 wide. */
        { extern int nv2a_widescreen(void);
          if (nv2a_widescreen() && (long)height * 4 == (long)width * 3)
              width = height * 16 / 9; }
        { const char *e = getenv("RECOMP_WINDOW_FIT");
          NSScreen *scr = [NSScreen mainScreen];
          if (scr && !(e && e[0] == '0')) {
              NSRect vis = [scr visibleFrame];
              NSRect chrome = [NSWindow frameRectForContentRect:NSMakeRect(0, 0, 100, 100)
                                                      styleMask:mask];
              double maxw = vis.size.width  - (chrome.size.width  - 100.0) - 40.0;
              double maxh = vis.size.height - (chrome.size.height - 100.0) - 24.0;
              double k = 1.0;
              if (maxw >= 320.0 && maxh >= 240.0) {
                  if (width > maxw)      k = maxw / width;
                  if (height * k > maxh) k = maxh / height;
              }
              if (k < 1.0) {
                  int w2 = (int)(width * k), h2 = (int)(height * k);
                  if ((long)height * 4 == (long)width * 3) h2 = w2 * 3 / 4;
                  if ((long)height * 16 == (long)width * 9) h2 = w2 * 9 / 16;
                  fprintf(stderr, "[WIN] %dx%d does not fit the screen (%.0fx%.0f "
                          "points free, %.0fx backing): %dx%d\n", width, height,
                          vis.size.width, vis.size.height,
                          (double)[scr backingScaleFactor], w2, h2);
                  width = w2; height = h2;
              }
          } }
        frame = NSMakeRect(0, 0, width, height);

        g_window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:mask
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [g_window setTitle:[NSString stringWithUTF8String:title ? title : "JSRF"]];
        [g_window setDelegate:[[NvWindowDelegate alloc] init]];
        /* Resizable, in the picture's shape; and it can go full screen (the
         * green button, or Video > Enter Full Screen, Ctrl+Cmd+F). */
        { extern int nv2a_widescreen(void);
          [g_window setContentAspectRatio:nv2a_widescreen() ? NSMakeSize(16.0, 9.0)
                                                            : NSMakeSize(4.0, 3.0)]; }
        [g_window setContentMinSize:NSMakeSize(320.0, 240.0)];
        [g_window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
        [g_window center];

        if (metal) {
            g_mview = [[NvMetalView alloc] initWithFrame:frame];
            [g_mview setWantsLayer:YES];
            [g_window setContentView:g_mview];
            [g_window makeFirstResponder:g_mview];
            [g_mview nvSyncDrawable];
        } else {
            g_view = make_gl_view(frame);
            if (!g_view) return 0;
            [g_window setContentView:g_view];
            [g_window makeFirstResponder:g_view];
        }
        build_menu();
        if (getenv("RECOMP_WINDOW_NOACTIVATE")) {
            /* Test runs: on screen, but the keyboard stays with whoever is
             * using the Mac -- a scripted run needs no keys, and someone
             * typing elsewhere should not be steering the game. */
            [g_window orderFrontRegardless];
        } else {
            [g_window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }
        nv_window_note_refresh();

        g_w = width; g_h = height; g_open = 1;
        fprintf(stderr, "[WIN] %dx%d window open (%s), window number %ld\n", width, height,
                metal ? "Metal: the renderer presents into its layer"
                      : "OpenGL: drawn on the main thread",
                (long)[g_window windowNumber]);
        return 1;
    }
}

int nv_window_is_open(void) { return g_open; }

/* Called from the render thread with a finished frame, bottom row first, as
 * glReadPixels hands it over. Copied rather than referenced: the caller's
 * buffer is reused for the next frame the moment this returns. */
void nv_window_submit_frame(const void *pixels, int width, int height)
{
    size_t need;
    if (!g_open || !pixels || width <= 0 || height <= 0) return;
    need = (size_t)width * (size_t)height * 4u;
    pthread_mutex_lock(&g_lock);
    if (g_px_w != width || g_px_h != height || !g_px) {
        free(g_px);
        g_px = (unsigned char *)malloc(need);
        g_px_w = width; g_px_h = height;
    }
    if (g_px) { memcpy(g_px, pixels, need); g_px_new = 1; }
    pthread_mutex_unlock(&g_lock);
}

/*
 * RECOMP_KEY_SCRIPT="t keycode[@secs];..." (test only): real key events,
 * posted into this application's own event queue at those times (seconds
 * from the window opening; key codes as kVK_*, e.g. 36 Return, 49 Space,
 * 13 W), held 0.25 s or as long as asked. They come out of the same
 * nextEventMatchingMask below that a physical key press does, so a scripted
 * run exercises the whole keyboard path -- window, mapping, merge with the
 * pad -- with nobody at the keyboard. While a script is running the keys are
 * not let go when the window loses focus, so that someone using the Mac
 * during a test does not cut it short.
 */
static struct { double t, dur; unsigned short code; int state; } g_keys[48];
static int    g_nkeys = -1;
static double g_keys_t0;

static double key_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void key_script_post(unsigned short code, int down)
{
    NSEvent *ev = [NSEvent keyEventWithType:down ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                   location:NSZeroPoint
                              modifierFlags:0
                                  timestamp:[[NSProcessInfo processInfo] systemUptime]
                               windowNumber:[g_window windowNumber]
                                    context:nil
                                 characters:@""
                charactersIgnoringModifiers:@""
                                  isARepeat:NO
                                    keyCode:code];
    if (ev) [NSApp postEvent:ev atStart:NO];
}

static void key_script_step(void)
{
    double now;
    int i;
    if (g_nkeys < 0) {
        const char *s = getenv("RECOMP_KEY_SCRIPT");
        g_nkeys = 0;
        g_keys_t0 = key_now();
        while (s && *s && g_nkeys < 48) {
            char *e;
            double t = strtod(s, &e);
            unsigned long code;
            if (e == s) break;
            s = e;
            while (*s == ' ') s++;
            code = strtoul(s, &e, 10);
            if (e == s) break;
            s = e;
            g_keys[g_nkeys].t = t;
            g_keys[g_nkeys].code = (unsigned short)code;
            g_keys[g_nkeys].dur = 0.25;
            if (*s == '@') g_keys[g_nkeys].dur = strtod(s + 1, &e), s = e;
            g_keys[g_nkeys].state = 0;
            g_nkeys++;
            while (*s == ' ' || *s == ';') s++;
        }
        if (g_nkeys)
            fprintf(stderr, "[WIN] key script: %d presses\n", g_nkeys);
    }
    if (!g_nkeys) return;
    now = key_now() - g_keys_t0;
    for (i = 0; i < g_nkeys; i++) {
        if (g_keys[i].state == 0 && now >= g_keys[i].t) {
            key_script_post(g_keys[i].code, 1);
            g_keys[i].state = 1;
            fprintf(stderr, "[WIN] key %u down at %.1fs\n", g_keys[i].code, now);
        } else if (g_keys[i].state == 1 && now >= g_keys[i].t + g_keys[i].dur) {
            key_script_post(g_keys[i].code, 0);
            g_keys[i].state = 2;
        }
    }
}

/*
 * RECOMP_WIN_SCRIPT="t action;..." (test only): window operations at those
 * times (seconds from the window opening), done on the main thread as a
 * click or a menu choice would do them -- fs (full screen on/off), min,
 * restore, wide (widescreen on/off, not stored), size WxH (content size in
 * points). For checking that the renderer follows the window through all of
 * it with nobody at the Mac.
 */
static struct { double t; char act[24]; int done; } g_wops[16];
static int    g_nwops = -1;
static double g_wops_t0;

static void win_script_step(void)
{
    double now;
    int i;
    if (g_nwops < 0) {
        const char *s = getenv("RECOMP_WIN_SCRIPT");
        g_nwops = 0;
        g_wops_t0 = key_now();
        while (s && *s && g_nwops < 16) {
            char *e;
            double t = strtod(s, &e);
            int n = 0;
            if (e == s) break;
            while (*e == ' ') e++;
            while (e[n] && e[n] != ';' && n < 23) { g_wops[g_nwops].act[n] = e[n]; n++; }
            g_wops[g_nwops].act[n] = 0;
            g_wops[g_nwops].t = t;
            g_wops[g_nwops].done = 0;
            g_nwops++;
            s = strchr(e, ';');
            if (!s) break;
            s++;
        }
    }
    if (!g_nwops || !g_window) return;
    now = key_now() - g_wops_t0;
    for (i = 0; i < g_nwops; i++) {
        const char *a = g_wops[i].act;
        int w, h;
        if (g_wops[i].done || now < g_wops[i].t) continue;
        g_wops[i].done = 1;
        fprintf(stderr, "[WINOP] t=%.1f %s\n", now, a);
        if (!strcmp(a, "fs")) [g_window toggleFullScreen:nil];
        else if (!strcmp(a, "min")) [g_window miniaturize:nil];
        else if (!strcmp(a, "restore")) [g_window deminiaturize:nil];
        else if (!strcmp(a, "wide")) {
            extern int nv2a_widescreen(void);
            extern void nv2a_set_widescreen(int on);
            nv2a_set_widescreen(!nv2a_widescreen());
            nv_window_shape_for_mode();
        } else if (sscanf(a, "size %dx%d", &w, &h) == 2)
            [g_window setContentSize:NSMakeSize(w, h)];
        fflush(stderr);
    }
}

/* Drain the event queue and draw. Main thread only. */
void nv_window_pump(void)
{
    @autoreleasepool {
        NSEvent *e;
        int have;

        if (!g_window) return;
        key_script_step();
        win_script_step();
        while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES]) != nil) {
            NSEventType t = [e type];
            int is_key = (t == NSEventTypeKeyDown || t == NSEventTypeKeyUp);
            /* Command-key combinations belong to the menu bar (Cmd+Q, Cmd+H,
             * Cmd+1..4): forward them and do not treat them as pad input. */
            if (is_key && ([e modifierFlags] & NSEventModifierFlagCommand)) {
                [NSApp sendEvent:e];
                continue;
            }
            if (t == NSEventTypeKeyDown)    kb_apply(e, 1);
            else if (t == NSEventTypeKeyUp) kb_apply(e, 0);
            /* Other key events are consumed rather than forwarded: with no
             * text field, AppKit answers an unhandled key press with a beep,
             * and holding a direction to skate would beep continuously. */
            if (!is_key)
                [NSApp sendEvent:e];
        }

        pthread_mutex_lock(&g_lock);
        have = g_px_new;
        if (g_window && g_nkeys <= 0 && ![g_window isKeyWindow] && (g_kb_buttons || g_kb_analog[0] ||
            g_kb_analog[1] || g_kb_analog[2] || g_kb_analog[3] || g_kb_analog[4] ||
            g_kb_analog[5] || g_kb_analog[6] || g_kb_analog[7]))
            kb_release_all();
        pthread_mutex_unlock(&g_lock);
        if (have && g_view) [g_view display];
    }
}

/* Kept so the renderer's present path has something to call; the drawing
 * happens on the main thread now, so there is nothing to do here. */
void nv_window_present(void) { }

int nv_window_should_close(void) { return g_should_close; }

void nv_window_size(int *w, int *h) { if (w) *w = g_w; if (h) *h = g_h; }

void nv_window_drawable_size(int *w, int *h)
{
    @autoreleasepool {
        NSView *v = g_mview ? (NSView *)g_mview : (NSView *)g_view;
        NSRect r = v ? [v convertRectToBacking:[v bounds]] : NSMakeRect(0, 0, g_w, g_h);
        if (w) *w = (int)r.size.width;
        if (h) *h = (int)r.size.height;
    }
}
