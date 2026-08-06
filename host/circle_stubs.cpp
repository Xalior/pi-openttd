//
// circle_stubs.cpp — the SDL2 surface layer OpenTTD needs and circle-libsdl2
// does not implement.
//
// TWO DIFFERENT SDL2 PROGRAMMING MODELS MEET HERE. circle-libsdl2 renders
// from textures: a program creates a renderer, locks a streaming texture,
// writes pixels into it and presents it. OpenTTD's SDL2 backend uses the
// older window-surface model instead: it asks the window for a surface,
// draws into that surface's memory for a whole frame, and calls
// SDL_UpdateWindowSurfaceRects to make the result visible.
//
// This file is the bridge. The window surface is real memory allocated
// here, so OpenTTD draws into exactly what it thinks it is drawing into,
// and the update call carries the changed rows into a streaming texture and
// presents it through the library's renderer. Nothing about the library
// changes, and nothing about OpenTTD changes.
//
// The palette is the other half. OpenTTD's fast blitters draw into an
// 8-bit paletted shadow surface, blit that through the palette onto the
// 32-bit window surface once a frame, and update the palette itself
// whenever the game animates colours. The library's surfaces are 32-bit
// only, so the paletted case is added here.
//
// Two of these functions REPLACE a library function instead of adding one:
// SDL_CreateRGBSurface and SDL_FreeSurface exist in the shim and refuse
// anything but 32 bits. They are reached through the linker's --wrap (see
// the WRAPPED_SDL list in the Makefile), so the library's own versions stay
// in place and still do the 32-bit work — this file only adds the paletted
// case on top and hands everything else straight back. Redefining them
// outright would be a duplicate symbol at best and a silent shadow at worst.
//
// Everything else here is an addition. Each one either does the job
// properly or fails honestly — returns an error, returns null — so that
// nothing pretends to work. Where a function is a deliberate no-op it says
// why: on a bare-metal board with one fullscreen display and no window
// manager there is nothing for it to do.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the stub here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL2/SDL.h>

extern "C" {

// The library's own versions of the two wrapped functions.
SDL_Surface *__real_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask);
void __real_SDL_FreeSurface(SDL_Surface *surface);

// ---------------------------------------------------------------------------
// Surfaces this file owns
// ---------------------------------------------------------------------------
//
// A surface made here carries allocations the library knows nothing about —
// a palette, a heap pixel format, the pixels — so freeing it is this file's
// job too. Rather than guess from the surface's contents, every one made
// here is recorded, and the free path looks it up: found means ours and
// freed our way, not found means the library's and handed back to it.
//
// The list is short by construction. OpenTTD holds one window surface, one
// paletted shadow surface when a paletted blitter is in use, and a window
// icon it frees immediately.

struct OwnedSurface
{
    SDL_Surface     *surface;
    SDL_PixelFormat *format;      // always heap, always ours
    SDL_Palette     *palette;     // the one this file allocated with it
    bool             owns_pixels;
    OwnedSurface    *next;
};

static OwnedSurface *s_owned = nullptr;

static OwnedSurface *FindOwned(SDL_Surface *surface)
{
    for (OwnedSurface *o = s_owned; o != nullptr; o = o->next)
        if (o->surface == surface)
            return o;
    return nullptr;
}

// Fill in a heap pixel format for one of the two layouts this port needs.
// depth 8 gets a palette; depth 32 is ARGB8888, which is byte-for-byte what
// the shim's streaming textures take.
static SDL_PixelFormat *MakeFormat(int depth)
{
    SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (fmt == nullptr)
        return nullptr;

    fmt->BitsPerPixel  = (Uint8)depth;
    fmt->BytesPerPixel = (Uint8)(depth / 8);
    fmt->refcount      = 1;

    if (depth == 8)
    {
        fmt->format = SDL_PIXELFORMAT_INDEX8;
    }
    else
    {
        fmt->format = SDL_PIXELFORMAT_ARGB8888;
        fmt->Rmask  = 0x00FF0000;
        fmt->Gmask  = 0x0000FF00;
        fmt->Bmask  = 0x000000FF;
        fmt->Amask  = 0xFF000000;
        fmt->Rshift = 16;
        fmt->Gshift = 8;
        fmt->Bshift = 0;
        fmt->Ashift = 24;
    }
    return fmt;
}

static SDL_Palette *NewPalette(int ncolors)
{
    if (ncolors <= 0)
        ncolors = 256;

    SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
    if (pal == nullptr)
        return nullptr;

    pal->colors = (SDL_Color *)calloc((size_t)ncolors, sizeof(SDL_Color));
    if (pal->colors == nullptr)
    {
        free(pal);
        return nullptr;
    }
    pal->ncolors  = ncolors;
    pal->refcount = 1;
    // Opaque black until the game sets a real palette. A palette whose alpha
    // was zero throughout would convert to a fully transparent picture and
    // read as "the game drew nothing".
    for (int i = 0; i < ncolors; i++)
        pal->colors[i].a = 0xFF;
    return pal;
}

static void FreePalette(SDL_Palette *pal)
{
    if (pal == nullptr)
        return;
    free(pal->colors);
    free(pal);
}

// The one place a surface is built. pixels == nullptr allocates them.
static SDL_Surface *NewOwnedSurface(int width, int height, int depth,
                                    void *pixels, int pitch)
{
    if (width <= 0 || height <= 0 || (depth != 8 && depth != 32))
    {
        SDL_SetError("unsupported surface: %dx%d at %d bits", width, height, depth);
        return nullptr;
    }

    const bool prealloc = (pixels != nullptr);

    SDL_Surface *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    OwnedSurface *rec    = (OwnedSurface *)calloc(1, sizeof(OwnedSurface));
    SDL_PixelFormat *fmt = MakeFormat(depth);
    SDL_Palette *pal     = (depth == 8) ? NewPalette(256) : nullptr;

    if (surface == nullptr || rec == nullptr || fmt == nullptr
        || (depth == 8 && pal == nullptr))
    {
        FreePalette(pal);
        free(fmt);
        free(rec);
        free(surface);
        SDL_SetError("out of memory allocating surface");
        return nullptr;
    }

    fmt->palette = pal;

    if (pitch == 0)
        pitch = width * fmt->BytesPerPixel;

    if (!prealloc)
    {
        pixels = calloc(1, (size_t)pitch * height);
        if (pixels == nullptr)
        {
            FreePalette(pal);
            free(fmt);
            free(rec);
            free(surface);
            SDL_SetError("out of memory allocating surface pixels");
            return nullptr;
        }
    }

    surface->flags     = prealloc ? SDL_PREALLOC : 0;
    surface->format    = fmt;
    surface->w         = width;
    surface->h         = height;
    surface->pitch     = pitch;
    surface->pixels    = pixels;
    surface->clip_rect = { 0, 0, width, height };
    surface->refcount  = 1;

    rec->surface     = surface;
    rec->format      = fmt;
    rec->palette     = pal;
    rec->owns_pixels = !prealloc;
    rec->next        = s_owned;
    s_owned          = rec;

    return surface;
}

// The library makes 32-bit surfaces; this adds the paletted ones OpenTTD's
// 8-bit blitters draw into, and leaves everything else to the library.
SDL_Surface *__wrap_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask)
{
    if (depth == 8)
        return NewOwnedSurface(width, height, 8, nullptr, 0);

    return __real_SDL_CreateRGBSurface(flags, width, height, depth,
                                       Rmask, Gmask, Bmask, Amask);
}

void __wrap_SDL_FreeSurface(SDL_Surface *surface)
{
    if (surface == nullptr)
        return;

    OwnedSurface **link = &s_owned;
    for (OwnedSurface *o = s_owned; o != nullptr; link = &o->next, o = o->next)
    {
        if (o->surface != surface)
            continue;

        if (--surface->refcount > 0)
            return;

        *link = o->next;
        // Only the palette this file allocated with the surface is freed
        // here. A palette attached later with SDL_SetSurfacePalette belongs
        // to whoever allocated it, which for OpenTTD is the video backend,
        // and it outlives every surface it is attached to.
        FreePalette(o->palette);
        if (o->owns_pixels)
            free(surface->pixels);
        free(o->format);
        free(o);
        free(surface);
        return;
    }

    __real_SDL_FreeSurface(surface);
}

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

SDL_Palette *SDL_AllocPalette(int ncolors)
{
    SDL_Palette *pal = NewPalette(ncolors);
    if (pal == nullptr)
        SDL_SetError("out of memory allocating palette");
    return pal;
}

void SDL_FreePalette(SDL_Palette *palette)
{
    FreePalette(palette);
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors)
{
    if (palette == nullptr || colors == nullptr)
    {
        SDL_SetError("SDL_SetPaletteColors: no palette");
        return -1;
    }
    if (firstcolor < 0 || ncolors < 0 || firstcolor + ncolors > palette->ncolors)
    {
        SDL_SetError("SDL_SetPaletteColors: range outside the palette");
        return -1;
    }

    for (int i = 0; i < ncolors; i++)
    {
        SDL_Color c = colors[i];
        // OpenTTD fills r, g and b and leaves alpha at zero. A zero alpha
        // here would convert the whole picture to transparent.
        c.a = 0xFF;
        palette->colors[firstcolor + i] = c;
    }
    palette->version++;
    return 0;
}

// Attaching a palette to a surface. OpenTTD calls this on its paletted
// shadow surface, where it is the whole point, and also on the 32-bit
// window surface, where on a desktop it would ask the display server to
// allocate those colours. There is no display server here and the window
// surface is direct colour, so that second call is accepted and does
// nothing — which is the truth, not a pretence.
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (surface == nullptr || surface->format == nullptr)
    {
        SDL_SetError("SDL_SetSurfacePalette: no surface");
        return -1;
    }
    if (surface->format->BytesPerPixel != 1)
        return 0;

    OwnedSurface *o = FindOwned(surface);
    if (o != nullptr && o->palette != nullptr && o->palette != palette)
    {
        // The surface came with a palette of its own and is being given
        // another. Drop ours now rather than leak it; the caller's is the
        // one that will be used from here on, and the caller owns it.
        FreePalette(o->palette);
        o->palette = nullptr;
    }
    surface->format->palette = palette;
    return 0;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    if (format == nullptr)
        return 0;

    if (format->palette != nullptr)
    {
        // The nearest colour in the palette, by squared distance. Only the
        // window icon reaches this, and only to find a transparent key.
        int best = 0;
        long bestd = -1;
        for (int i = 0; i < format->palette->ncolors; i++)
        {
            const SDL_Color &c = format->palette->colors[i];
            long dr = (long)c.r - r, dg = (long)c.g - g, db = (long)c.b - b;
            long d = dr*dr + dg*dg + db*db;
            if (bestd < 0 || d < bestd) { bestd = d; best = i; }
        }
        return (Uint32)best;
    }

    return ((Uint32)0xFF << format->Ashift)
         | ((Uint32)r << format->Rshift)
         | ((Uint32)g << format->Gshift)
         | ((Uint32)b << format->Bshift);
}

// Colour keying is a blit-time transparency rule. The only surface OpenTTD
// sets one on is the window icon, which is never blitted here because there
// is no window manager to draw it. Recorded on the surface so a reader of
// the surface sees what was asked for, and otherwise unused.
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (surface == nullptr)
    {
        SDL_SetError("SDL_SetColorKey: no surface");
        return -1;
    }
    if (flag)
        surface->flags |= SDL_RLEACCEL;     // marker only; nothing reads it
    else
        surface->flags &= ~(Uint32)SDL_RLEACCEL;
    (void)key;
    return 0;
}

// Nothing here is RLE encoded or hardware backed, so a lock is a formality.
int SDL_LockSurface(SDL_Surface *) { return 0; }
void SDL_UnlockSurface(SDL_Surface *) {}

// ---------------------------------------------------------------------------
// Blitting
// ---------------------------------------------------------------------------

static SDL_Rect WholeSurface(SDL_Surface *s)
{
    return SDL_Rect{ 0, 0, s->w, s->h };
}

// The frame's one conversion: OpenTTD's 8-bit shadow surface through its
// palette into the 32-bit window surface. Same-format copies are a row
// memcpy.
//
// Both rectangles are clipped against the memory behind them before a byte
// moves, because this is the UPPER blit and SDL's contract for it is that
// the blit does the clipping.
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (src == nullptr || dst == nullptr
        || src->pixels == nullptr || dst->pixels == nullptr)
    {
        SDL_SetError("SDL_BlitSurface: no source or no destination");
        return -1;
    }

    SDL_Rect sr = (srcrect != nullptr) ? *srcrect : WholeSurface(src);
    SDL_Rect dr = (dstrect != nullptr) ? *dstrect : SDL_Rect{ 0, 0, sr.w, sr.h };

    // A negative origin moves the start point and shortens the run.
    if (sr.x < 0) { sr.w += sr.x; dr.x -= sr.x; sr.x = 0; }
    if (sr.y < 0) { sr.h += sr.y; dr.y -= sr.y; sr.y = 0; }
    if (dr.x < 0) { sr.w += dr.x; sr.x -= dr.x; dr.x = 0; }
    if (dr.y < 0) { sr.h += dr.y; sr.y -= dr.y; dr.y = 0; }

    int w = sr.w;
    int h = sr.h;
    if (w > src->w - sr.x) w = src->w - sr.x;
    if (h > src->h - sr.y) h = src->h - sr.y;
    if (w > dst->w - dr.x) w = dst->w - dr.x;
    if (h > dst->h - dr.y) h = dst->h - dr.y;
    if (w <= 0 || h <= 0)
    {
        if (dstrect != nullptr) { dstrect->w = 0; dstrect->h = 0; }
        return 0;
    }

    const int sbpp = src->format->BytesPerPixel;
    const int dbpp = dst->format->BytesPerPixel;

    if (sbpp == 1 && dbpp == 4)
    {
        const SDL_Palette *pal = src->format->palette;
        if (pal == nullptr)
        {
            SDL_SetError("SDL_BlitSurface: paletted source has no palette");
            return -1;
        }

        // One flat lookup table per blit, built from the palette as it
        // stands. OpenTTD animates its palette — water, lights, the company
        // colours — so it is rebuilt every frame rather than cached.
        Uint32 lut[256];
        const int n = pal->ncolors < 256 ? pal->ncolors : 256;
        for (int i = 0; i < n; i++)
        {
            const SDL_Color &c = pal->colors[i];
            lut[i] = 0xFF000000u | ((Uint32)c.r << 16) | ((Uint32)c.g << 8)
                     | (Uint32)c.b;
        }
        for (int i = n; i < 256; i++)
            lut[i] = 0xFF000000u;

        for (int y = 0; y < h; y++)
        {
            const Uint8 *s = (const Uint8 *)src->pixels
                             + (size_t)(sr.y + y) * src->pitch + sr.x;
            Uint32 *d = (Uint32 *)((Uint8 *)dst->pixels
                        + (size_t)(dr.y + y) * dst->pitch) + dr.x;
            for (int x = 0; x < w; x++)
                d[x] = lut[s[x]];
        }
    }
    else if (sbpp == dbpp)
    {
        for (int y = 0; y < h; y++)
        {
            const Uint8 *s = (const Uint8 *)src->pixels
                             + (size_t)(sr.y + y) * src->pitch
                             + (size_t)sr.x * sbpp;
            Uint8 *d = (Uint8 *)dst->pixels + (size_t)(dr.y + y) * dst->pitch
                       + (size_t)dr.x * dbpp;
            memcpy(d, s, (size_t)w * sbpp);
        }
    }
    else
    {
        SDL_SetError("SDL_BlitSurface: %d-bit to %d-bit is not implemented",
                     sbpp * 8, dbpp * 8);
        return -1;
    }

    if (dstrect != nullptr)
    {
        dstrect->x = dr.x; dstrect->y = dr.y;
        dstrect->w = w;    dstrect->h = h;
    }
    return 0;
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    if (dst == nullptr || dst->pixels == nullptr)
    {
        SDL_SetError("SDL_FillRect: no destination");
        return -1;
    }

    SDL_Rect r = (rect != nullptr) ? *rect : WholeSurface(dst);
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > dst->w) r.w = dst->w - r.x;
    if (r.y + r.h > dst->h) r.h = dst->h - r.y;
    if (r.w <= 0 || r.h <= 0)
        return 0;

    const int bpp = dst->format->BytesPerPixel;
    for (int y = 0; y < r.h; y++)
    {
        Uint8 *row = (Uint8 *)dst->pixels + (size_t)(r.y + y) * dst->pitch
                     + (size_t)r.x * bpp;
        if (bpp == 1)
        {
            memset(row, (int)(color & 0xFF), (size_t)r.w);
        }
        else
        {
            Uint32 *p = (Uint32 *)row;
            for (int x = 0; x < r.w; x++)
                p[x] = color;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The window surface, and how it reaches the screen
// ---------------------------------------------------------------------------
//
// One window, one surface, one renderer, one streaming texture — all made on
// the first ask and kept for the run. The surface is ordinary memory the
// game draws into for a whole frame; the update call below is the only
// moment any of it moves.

static SDL_Window   *s_window   = nullptr;
static SDL_Surface  *s_surface  = nullptr;
static SDL_Renderer *s_renderer = nullptr;
static SDL_Texture  *s_texture  = nullptr;

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    if (window == nullptr)
    {
        SDL_SetError("SDL_GetWindowSurface: no window");
        return nullptr;
    }
    if (s_surface != nullptr && s_window == window)
        return s_surface;

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0)
    {
        SDL_SetError("SDL_GetWindowSurface: the window has no size");
        return nullptr;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == nullptr)
        return nullptr;

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, w, h);
    if (texture == nullptr)
    {
        SDL_DestroyRenderer(renderer);
        return nullptr;
    }

    SDL_Surface *surface = NewOwnedSurface(w, h, 32, nullptr, 0);
    if (surface == nullptr)
    {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        return nullptr;
    }

    s_window   = window;
    s_surface  = surface;
    s_renderer = renderer;
    s_texture  = texture;
    return s_surface;
}

// Make part of the window surface visible. The rectangles say what changed;
// the texture is locked whole and only those rows are copied into it, then
// the whole texture is presented, because the library presents a frame at a
// time and there is nothing cheaper to ask it for.
int SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *rects,
                                 int numrects)
{
    if (window == nullptr || s_surface == nullptr || s_texture == nullptr)
    {
        SDL_SetError("SDL_UpdateWindowSurfaceRects: no window surface");
        return -1;
    }

    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(s_texture, nullptr, &pixels, &pitch) != 0)
        return -1;

    for (int i = 0; i < numrects; i++)
    {
        SDL_Rect r = rects[i];
        if (r.x < 0) { r.w += r.x; r.x = 0; }
        if (r.y < 0) { r.h += r.y; r.y = 0; }
        if (r.x + r.w > s_surface->w) r.w = s_surface->w - r.x;
        if (r.y + r.h > s_surface->h) r.h = s_surface->h - r.y;
        if (r.w <= 0 || r.h <= 0)
            continue;

        for (int y = 0; y < r.h; y++)
        {
            const Uint8 *s = (const Uint8 *)s_surface->pixels
                             + (size_t)(r.y + y) * s_surface->pitch
                             + (size_t)r.x * 4;
            Uint8 *d = (Uint8 *)pixels + (size_t)(r.y + y) * pitch
                       + (size_t)r.x * 4;
            memcpy(d, s, (size_t)r.w * 4);
        }
    }

    SDL_UnlockTexture(s_texture);
    SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
    SDL_RenderPresent(s_renderer);
    return 0;
}

int SDL_UpdateWindowSurface(SDL_Window *window)
{
    if (s_surface == nullptr)
    {
        SDL_SetError("SDL_UpdateWindowSurface: no window surface");
        return -1;
    }
    SDL_Rect whole = WholeSurface(s_surface);
    return SDL_UpdateWindowSurfaceRects(window, &whole, 1);
}

// ---------------------------------------------------------------------------
// Window and renderer calls with nothing to do on this board
// ---------------------------------------------------------------------------
//
// The display is one fullscreen panel that the host kernel declared before
// the game started. Its size, its position and its scaling are settled
// before any of these can be called, so each answers success and changes
// nothing.

int SDL_SetWindowFullscreen(SDL_Window *, Uint32) { return 0; }
void SDL_SetWindowSize(SDL_Window *, int, int) {}
void SDL_SetWindowMinimumSize(SDL_Window *, int, int) {}

// The icon would be drawn by a window manager, and there is none.
void SDL_SetWindowIcon(SDL_Window *, SDL_Surface *) {}

// There is no screen saver and nothing to switch off.
void SDL_DisableScreenSaver(void) {}
void SDL_EnableScreenSaver(void) {}

// Reading a bitmap file into a surface. The only caller is the window-icon
// path above, whose result is never drawn, so this reports honestly that
// there is no bitmap reader here rather than carrying one for a picture
// nothing displays. OpenTTD checks for null and carries on without an icon.
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    if (src != nullptr && freesrc && src->close != nullptr)
        src->close(src);
    SDL_SetError("bitmap files are not read on this platform");
    return nullptr;
}

// ---------------------------------------------------------------------------
// Events, keys and the clipboard
// ---------------------------------------------------------------------------

// OpenTTD peeks one event ahead to pair a key press with the text it
// produced. The shim has no peek, so this answers "nothing queued", which is
// the same answer a keyboard producing no text input would give.
int SDL_PeepEvents(SDL_Event *, int, SDL_eventaction, Uint32, Uint32)
{
    return 0;
}

// The name-to-key direction, for the one caller: OpenTTD hands the text a
// keystroke produced back to SDL to recover which key it was. A single
// printable character names itself, which covers every character text input
// can deliver here; anything longer is a named key this port has no table
// for and is reported as unknown.
SDL_Keycode SDL_GetKeyFromName(const char *name)
{
    if (name == nullptr || name[0] == '\0' || name[1] != '\0')
        return SDLK_UNKNOWN;
    return (SDL_Keycode)(unsigned char)name[0];
}

// The key-to-scancode direction, for OpenTTD's hotkey matching. The
// printable ASCII keys are the ones hotkeys are written with, and their
// scancodes follow SDL's own layout-independent numbering; everything else
// has no scancode here.
SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'a'));
    if (key >= 'A' && key <= 'Z')
        return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'A'));
    if (key >= '1' && key <= '9')
        return (SDL_Scancode)(SDL_SCANCODE_1 + (key - '1'));
    if (key == '0')
        return SDL_SCANCODE_0;

    switch (key)
    {
    case SDLK_RETURN:    return SDL_SCANCODE_RETURN;
    case SDLK_ESCAPE:    return SDL_SCANCODE_ESCAPE;
    case SDLK_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
    case SDLK_TAB:       return SDL_SCANCODE_TAB;
    case SDLK_SPACE:     return SDL_SCANCODE_SPACE;
    default:             return SDL_SCANCODE_UNKNOWN;
    }
}

// Names for the keys a caller prints. SDL builds these from a table this
// port does not carry; the printable keys name themselves and everything
// else is reported honestly as unnamed.
const char *SDL_GetKeyName(SDL_Keycode key)
{
    static char name[2];
    if (key >= ' ' && key < 0x7F)
    {
        name[0] = (char)key;
        name[1] = '\0';
        return name;
    }
    return "";
}

// There is no clipboard on this board: no window manager, no second
// program to have copied anything. Saying so is the whole implementation.
SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }

char *SDL_GetClipboardText(void)
{
    // SDL's contract is that the caller frees what it gets, even when the
    // clipboard is empty, so this must be an allocation and not a literal.
    char *empty = (char *)SDL_malloc(1);
    if (empty != nullptr)
        empty[0] = '\0';
    return empty;
}

int SDL_SetClipboardText(const char *)
{
    SDL_SetError("there is no clipboard on this platform");
    return -1;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

// The old whole-subsystem audio close. OpenTTD's SDL sound backend opens a
// device with SDL_OpenAudioDevice, which the library implements, and closes
// with this — so it is routed to the device the library actually opened.
// SDL numbers the first device it opens 1, and this port never opens a
// second.
void SDL_CloseAudio(void)
{
    SDL_CloseAudioDevice(1);
}

// ---------------------------------------------------------------------------
// Where the game's files live
// ---------------------------------------------------------------------------
//
// On a desktop these are the directory the program was installed in and a
// per-user directory for its settings and saves. Here they are one and the
// same: the card directory this game was started from, which is also its
// working directory and the directory argv[0] names. The caller frees what
// it gets, so each hands back a fresh copy every time.

static char *DuplicateGameDir(void)
{
    static const char path[] = RAPI_GAME_DIR "/";
    char *copy = (char *)SDL_malloc(sizeof(path));
    if (copy != nullptr)
        memcpy(copy, path, sizeof(path));
    return copy;
}

char *SDL_GetBasePath(void) { return DuplicateGameDir(); }
char *SDL_GetPrefPath(const char *, const char *) { return DuplicateGameDir(); }

// The board has no window manager to put a dialog on top of, so the message
// goes where every other diagnostic goes: the serial console.
int SDL_ShowSimpleMessageBox(Uint32, const char *title, const char *message,
                             SDL_Window *)
{
    printf("%s: %s\n", title != nullptr ? title : "message",
           message != nullptr ? message : "");
    return 0;
}

} // extern "C"
