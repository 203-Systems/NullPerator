# PicoTracker UI2 Rendering and Interaction Architecture

Status: proposed implementation framework. This document consolidates the UI
decisions made in chat through 2026-08-24. Visual approval still precedes page
implementation.

Base revision: `67c182e6 fix(web-ui): restore 203 branding`

Branch: `codex/ui2-architecture`

## 1. Goal

Replace the current character-grid presentation with a modern, restrained,
pixel-native UI that remains exactly 240 by 240 pixels, preserves PicoTracker's
eight-track and sixteen-row workflow, and runs efficiently on ESP32-S3.

The browser workbench is the reference implementation environment. It must run
the same C++ UI scene and renderer used on ESP32-S3; Svelte only hosts the
canvas, controls, diagnostics, and design-review tooling. We must not create a
second JavaScript implementation of tracker behavior.

The new architecture must provide:

- one shared layout and rendering implementation for WASM and ESP32-S3;
- exact 240 by 240 output at 1x, with integer 2x and larger previews;
- fixed-memory rendering with no heap allocation in a frame;
- nonlinear but short transitions;
- continuous motion when navigation is repeated quickly;
- animated cursor movement without delaying model updates;
- centralized Top Bar and Bottom Bar state resolution;
- semantic theme tokens rather than visual color names;
- dirty-region transfer so idle frames do almost no display work;
- a migration path that does not require rewriting input, audio, project, and
  playback logic at the same time.

## 2. Confirmed visual rules

### 2.1 Screen and typography

- The logical and physical UI surface is always 240 by 240 pixels.
- Bars fill the complete horizontal width. They do not have a surrounding black
  margin.
- The content region may retain a small black inset so the screen still feels
  like a dedicated instrument rather than a borderless phone UI.
- The smallest glyph has at least a 5 by 8 ink body. The normal cell is 8 by 10
  pixels, allowing one pixel of breathing room around the bitmap glyph.
- Top Bar titles use a larger bitmap treatment. Body data remains the compact
  regular bitmap font.
- Fonts stay monospaced wherever columns carry tracker data.
- No visible grid lines are used. Spacing, alignment, contrast, and the cursor
  establish structure.
- Column spacing is consistent between Song, Phrase, and Table layouts. Song
  must not become unusually loose and Phrase/Table must not become cramped.
- Sixteen rows remain visible in all tracker pages that currently show sixteen
  rows.

Recommended base geometry:

```text
0................................................239
+------------------------------------------------+
| Top Bar: y=0, h=34                             |
+------------------------------------------------+
| Content viewport: y=34..207, h=174             |
| content inset x=5..234                         |
| header + 16 rows at 10 px step                 |
+------------------------------------------------+
| Bottom Bar: y=208, h=32, or hidden/expanded    |
+------------------------------------------------+
```

Page-specific geometry may use the whole content viewport, but it must not
change these shared chrome bounds.

### 2.2 General style

- The base is nearly black, with slightly differentiated bar and field
  surfaces.
- Bright color is functional, not decorative.
- Row focus uses a quiet neutral background, never green.
- The active cell uses a compact rounded highlight bubble.
- Rounded highlights have crisp straight edges. Only corner coverage is
  antialiased; the complete rectangle must never be blurred.
- Radius is intentionally small, approximately 2 pixels at 1x.
- Selection text uses a dedicated high-contrast ink token.
- There is no blue line marking a page transition.
- VU meters may use a controlled semantic gradient, but general UI surfaces do
  not use decorative gradients.

### 2.3 Core page content

- Song keeps eight tracks and sixteen visible rows.
- Song reserves the right edge for a stereo master VU meter. Left and right VU
  channels are visibly separate, wide enough to read, and have a small gap.
- Song playback position is shown by one small edge tick per track/value cell,
  not by a synchronized row highlight. When a playback tick overlaps the edit
  cursor, the tick changes to the active playback color.
- Phrase keeps NOTE, INS, FX1/value, and FX2/value columns.
- Table keeps three FX/value pairs. Phrase Table is identified as `P##` and
  Instrument Table as `I##` in the Top Bar.
- Chain, Phrase, Table, Groove, and Instrument show their current number in the
  right side of the Top Bar.
- Song shows the song name in that location. Phrase shows its phrase number.
- Instrument shows the instrument number, not the instrument type.
- Project Browse is titled `BROWSE` and does not show a number.
- Mixer does not show a number on the right.
- Mixer has T1 through T8 plus a distinct Master column. Every track is stereo;
  left and right meter bars have a visible gap. The Master label is neutral,
  like the track labels. Channel volume values remain visible below the meters.
- Groove and Mixer have no default Bottom Bar.

### 2.4 Instrument, Project, Theme, Font, and Device

- Instrument places Name above Type.
- Selecting Instrument Name shows `LOAD / SAVE / RENAME` in the Bottom Bar.
- Instrument Type uses a horizontally scrolling selector. Instrument type
  selection may be configured to wrap. Adjacent values sit near the center
  value, not against the edge arrows.
- SID and OPAL show `EXPERIMENTAL` below the Top Bar number on the right.
- OPAL retains its two-operator structure and the original General Settings and
  Operator Settings information.
- Project places Name first. Selecting it shows
  `NEW / LOAD / SAVE / RENAME` in the Bottom Bar.
- Project Cleanup shows `REMOVE UNUSED` in the Bottom Bar rather than repeating
  the action as a field value.
- Project Render uses a Bottom Bar selector with `MIXDOWN / STEMS`.
- Theme places Name first. Selecting it shows
  `NEW / LOAD / SAVE / RENAME` in the Bottom Bar.
- Font is a separate page. It contains one font selector and `BROWSE`; it does
  not contain spacing or line-step controls.
- Device has no `SYSTEM` subtitle.
- Device displays battery percentage to the left of the battery icon.
- Boolean and enum device fields use the Bottom Bar selector. The selector has
  explicit wrap/no-wrap behavior per field.

### 2.5 Battery and Top Bar states

- Normal battery is white.
- Charging battery is green.
- Only low battery is red.
- Charging does not add a lightning icon.
- While playing, play time replaces the normal battery slot as defined by the
  page.
- Holding Nav replaces the right slot with the compact `S / C P I / M`
  navigation cross. The selected letter uses a small rounded background, not
  colored text alone.
- Top Bar variants are reviewed separately in the preview: playing, high
  battery, low battery, charging, and Nav.

### 2.6 Bottom Bar behavior

The Bottom Bar is contextual. A page requests content; it never draws the bar
directly.

- Song default: eight evenly spaced track labels and their notes.
- Phrase on Note or Instrument: one line,
  `INSTRUMENT 00 <instrument name>`. `INSTRUMENT 00` uses the active token; the
  name uses primary text.
- Phrase/Table on FX command or parameter: command help. For example the first
  line is `KILL --BB`, where `KILL` is active and `--BB` is primary text. The
  description appears on the second line.
- Empty Phrase cells: no Bottom Bar. The bar slides down out of view.
- Name fields: action bar, as specified above.
- Enum/boolean fields: selector bar.
- Groove and Mixer: hidden by default.
- Browser pages: page-specific Load/Delete/Edit actions.
- Pressing and holding Nav may expand the Bottom Bar to show navigation and
  mixer information. Expansion is a height/reveal animation, not a page
  relayout.
- In Phrase, Table, Instrument, and other numbered pages, holding Enter moves
  the active bubble to the number in the Top Bar. Enter+Up/Down changes the
  page number. Enter+Left/Right changes the current track.
- During that Enter-held state, the Bottom Bar temporarily becomes the same
  eight-track note bar used by Song. Its cursor bubble surrounds the selected
  `T#`. Releasing Enter restores the bar requested by the current cell.

## 3. Motion specification

Motion is restrained and directional. It borrows the decisiveness of game UI
transitions without copying decorative effects that would obscure tracker data.

### 3.1 Page navigation

The screen is composed into independent layers:

```text
Top Bar background       fixed
Top Bar text/status      fade only
Content                  translates as one complete layer
Bottom Bar background    fixed unless height changes
Bottom Bar content       fade only
Modal/critical overlay   independent, highest priority
Cursor overlay           independent within its owning layer
```

When a view changes:

1. The outgoing content and incoming content are both retained scenes.
2. The complete content scene translates on one axis. Rows, columns, headers,
   meters, and empty space all use the same signed offset; there is no stagger.
3. The sign comes from the navigation direction. All middle content moves
   consistently with that direction.
4. Top and Bottom Bar backgrounds do not translate.
5. Only changing bar text/status crossfades. Bar text does not slide.
6. A bar that changes height performs its own reveal/height animation.
7. At completion the outgoing scene is discarded and only the new scene
   remains.

Recommended timing at the existing 30 Hz UI clock:

| Motion | Duration | Curve | Expected frames |
| --- | ---: | --- | ---: |
| Content page transition | 180 ms | ease-out cubic | 5-6 |
| Top Bar text crossfade | 120 ms | smoothstep | 3-4 |
| Bottom Bar content crossfade | 120 ms | smoothstep | 3-4 |
| Bottom Bar reveal/hide | 150 ms | ease-out cubic | 4-5 |
| Cursor translation/resize | 100-130 ms | ease-out cubic | 3-4 |
| Number/track focus handoff | 90 ms | ease-out cubic | 3 |

Durations are design tokens and must be adjusted from hardware measurement,
not duplicated as literals in pages.

### 3.2 Continuity under repeated input

Navigation must not restart from a blank frame or snap to the previous page
when the user presses quickly.

- Motion is time based, not frame-count based.
- Repeated navigation in the same direction appends the next retained scene to
  the scene strip and preserves current velocity.
- Opposite navigation reverses the active transition from its current
  position.
- At most three page scenes are retained: previous, current, and next. A scene
  that is fully outside the clip is released.
- Bar fades retarget from their current opacity instead of resetting to zero.
- If a frame deadline is missed, the next frame samples the current monotonic
  time. Frames are dropped; model/input events are never delayed or queued for
  visual completion.

### 3.3 Cursor motion

Model focus changes immediately. The cursor visual interpolates from its last
rendered rectangle to the new target rectangle.

```cpp
struct CursorTarget {
  RectI16 rect;
  PaletteToken fill;
  PaletteToken ink;
  std::uint8_t radius;
  CursorLayer layer;
  TextSpan selected_text;
};
```

- Animate x, y, width, and height in fixed point.
- Dirty the union of the previous and current cursor bounds, expanded by one
  pixel for corner antialiasing.
- Restore the base scene in that union, draw the bubble, then redraw selected
  text using `cursor.ink`. This prevents trails and prevents the bubble from
  covering text.
- The low-contrast row background is a separate element. It may crossfade over
  two frames; it does not morph into the cell bubble.
- Page transitions own the whole content layer, so the cursor rides with that
  page. A cursor does not animate independently between two different pages.
- Top-number focus and Bottom-Bar track focus are separate cursor targets. The
  Enter-held mode activates both semantic states, but only the currently
  changing target receives the stronger pulse if a pulse is enabled.
- No continuous idle pulse is required. Static screens should become fully
  idle.

### 3.4 Nonlinear curves without floating point

Use Q0.16 normalized time and position. The default curve is fixed-point
ease-out cubic:

```cpp
using UnitQ16 = std::uint16_t;

UnitQ16 EaseOutCubic(UnitQ16 t) {
  const std::uint32_t u = 65535U - t;
  const std::uint32_t u2 = (u * u) >> 16;
  const std::uint32_t u3 = (u2 * u) >> 16;
  return static_cast<UnitQ16>(65535U - u3);
}
```

The production implementation may use a 256-entry `uint16_t` lookup table if
profiling shows it is cheaper. Pages never implement easing themselves.

## 4. Centralized bar control

The chrome resolver is the only component allowed to decide what the bars show.
Pages return declarative requests.

```cpp
enum class TopRightKind : std::uint8_t {
  None,
  Meta,
  Battery,
  PlayTime,
  NavCross,
};

struct TopBarRequest {
  TextId title;
  TextId meta;
  TopRightKind right_kind;
  bool meta_focused;
};

enum class BottomBarKind : std::uint8_t {
  Hidden,
  TrackNotes,
  ContextHelp,
  Actions,
  Selector,
  NavMixer,
  BrowserActions,
};

struct BottomBarRequest {
  BottomBarKind kind;
  std::uint8_t preferred_height; // 0, 32, or expanded height
  std::int8_t selected_index;
  bool wraps;
  Span<const BottomBarItem> items;
  TextId title;
  TextId detail;
};

struct ChromeRequest {
  TopBarRequest top;
  BottomBarRequest bottom;
};
```

Resolution priority is deterministic:

1. Critical/full-screen modal.
2. Nav held: Nav cross in Top Bar and expanded Nav/Mixer Bottom Bar.
3. Enter held on a numbered page: focused Top Bar number and Track Notes
   Bottom Bar with selected `T#`.
4. View/cursor contextual request: help, actions, or selector.
5. View default.

Within the normal Top Bar right slot:

1. playing and allowed by page -> play time;
2. otherwise -> battery state;
3. pages that explicitly own the slot -> page meta or none.

The low-battery shutdown warning remains a separate critical overlay, so Nav
temporarily replacing the battery icon cannot hide the safety path.

## 5. Semantic palette

User-facing palette names describe function, never a hue such as `cyan` or
`mint`.

Recommended persisted or derived tokens:

```text
surface.canvas
surface.field
surface.bar
surface.bar.deep
text.primary
text.muted
text.dim
cursor.primary
cursor.soft
cursor.row
cursor.ink
playback.active
playback.soft
battery.normal
battery.charging
battery.low
vu.track
vu.safe
vu.safe.low
vu.warning
vu.peak
```

The existing theme/project format must remain compatible. A compatibility map
converts legacy `ColorDefinition`/FourCC entries to semantic tokens. Extra UI2
colors are derived at load time; they are not added to the persisted format
unless a separate migration is approved.

Derived ramps are generated once when a theme changes:

- bar text fade ramp: bar background to target text color;
- cursor corner coverage colors;
- VU ramp: `vu.safe.low` -> `vu.safe` -> `vu.warning` -> `vu.peak`;
- muted and disabled variants.

The VU gradient is a short precomputed lookup indexed by meter height. No
general gradient shader or per-frame color interpolation is required.

## 6. Shared C++ architecture

### 6.1 API-first delivery decision

UI2 is introduced as a new shared C++ API before any production page is
ported. WASM is its first backend and first product caller. ESP32-S3 is the
second backend, added only after the API has rendered and animated the approved
pages on the computer.

The API has two deliberately separate sides:

```text
Page-facing API                    Platform-facing API
-------------------------------    --------------------------------
UiSceneBuilder                     IUiPresenter
UiChromeRequest                    UiIndexedSurface (read-only)
CursorTarget                       UiPalette (read-only)
TransitionHint                     DirtyStrip list
UiEngine::SubmitScene()             PresentResult
```

Page code never sees WebGL, SDL, DOM, RGBA, ESP-IDF, SPI, DMA, PSRAM, or LCD
panel handles. A presenter never sees `Project`, `Player`, `ViewData`, page
cursors, or input masks.

The public API surface is:

```cpp
namespace ui2 {

struct EngineConfig {
  std::uint16_t width = 240;
  std::uint16_t height = 240;
  std::uint16_t tick_interval_ms = 33;
};

struct FrameInput {
  std::uint32_t now_ms;
  InputPresentationState input;
  PlaybackPresentationState playback;
  BatteryPresentationState battery;
};

class IUiPresenter {
public:
  virtual ~IUiPresenter() = default;
  virtual PresentResult Present(const UiIndexedSurface &surface,
                                const UiPalette &palette,
                                Span<const DirtyStrip> strips) = 0;
};

class UiEngine {
public:
  UiEngine(const EngineConfig &config, IUiPresenter &presenter,
           UiStorage &storage);

  UiSceneBuilder BeginScene(ViewType view, std::uint32_t generation);
  void SubmitScene(UiSceneBuilder &&scene, const ChromeRequest &chrome,
                   const CursorTarget &cursor, TransitionHint hint);
  void SetPalette(const UiPaletteSource &source);
  void SetFrameInput(const FrameInput &input);
  bool Tick();
};

} // namespace ui2
```

`UiSceneBuilder` is a small value type whose primitive methods are inline and
append to fixed-capacity arrays. Drawing primitives do not use virtual calls.
Only one presenter call occurs per submitted frame. `UiStorage` is supplied by
the target and owns every framebuffer, command list, text pool, and DMA scratch
buffer, so allocation policy is explicit and measurable.

The first computer-side integration is:

```text
existing C++ View/controller
        -> builds UI2 scene
        -> UiEngine composites 240x240 indexed frame
        -> WasmUiPresenter expands dirty strips to RGBA/WebGL
        -> existing #picotracker-canvas
```

The later device integration changes only the final line:

```text
UiEngine composites the same indexed frame
        -> Esp32UiPresenter expands dirty strips to RGB565 DMA buffers
        -> esp_lcd_panel_draw_bitmap()
```

The WASM presenter may keep an RGBA snapshot buffer for browser tests. That is
backend-owned diagnostic memory and is never part of the shared API or the
ESP32 memory budget.

API v1 is considered stable enough to port when all of these are true:

- Song, Phrase, Table, Instrument, and Mixer use it in WASM;
- page transitions, cursor motion, and all chrome overrides use it;
- golden and deterministic timeline tests pass;
- no page uses a WASM-only primitive or reads presenter memory;
- fixed capacities are measured rather than estimated;
- the host test suite can compile the engine without SDL, Emscripten, or
  ESP-IDF headers.

### 6.2 Repository layout

```text
sources/UI2/
  Core/
    UiGeometry.h
    UiTime.h
    UiTokens.h
    UiFixed.h
  Model/
    UiFrameContext.h
    UiPageModel.h
    UiChromeModel.h
    UiInputPresentationState.h
  Scene/
    UiCommand.h
    UiCommandList.h
    UiScene.h
    UiSceneBuilder.h
  Animation/
    UiTimeline.h
    UiMotionTrack.h
    UiTransitionCoordinator.h
    UiCursorAnimator.h
  Components/
    UiTopBar.h
    UiBottomBar.h
    UiCursor.h
    UiText.h
    UiVuMeter.h
    UiBattery.h
    UiSelector.h
  Pages/
    SongPageRenderer.h/.cpp
    ChainPageRenderer.h/.cpp
    PhrasePageRenderer.h/.cpp
    TablePageRenderer.h/.cpp
    ...
  Render/
    UiIndexedSurface.h
    UiRasterizer.h
    UiDirtyTiles.h
    UiCompositor.h
  Theme/
    UiPalette.h
    UiThemeAdapter.h

sources/Adapters/wasm/gui/
  WasmUiPresenter.h/.cpp

sources/Adapters/node/display/
  IndexedDisplayPresenter.h/.cpp

web/e2e/
  ui2-frames.spec.js
  ui2-motion.spec.js
```

Names may be adjusted to project conventions, but the dependency direction is
mandatory:

```text
Application model + existing View controller state
                 |
                 v
       immutable UiPageModel
                 |
                 v
       page renderer + chrome resolver
                 |
                 v
       retained UiScene command lists
                 |
                 v
      transition/cursor compositor
                 |
                 v
      240x240 indexed framebuffer
           /                 \
          v                   v
   WASM presenter       ESP32-S3 presenter
```

The page model cannot call the presenter. The presenter cannot inspect tracker
models. This boundary makes the Web reference output authoritative for the
device.

### 6.3 Frame model

Each active view produces a small immutable presentation model. It contains
only values required to draw that frame.

```cpp
struct UiFrameContext {
  std::uint32_t now_ms;
  ViewType view;
  InputPresentationState input;
  PlaybackPresentationState playback;
  BatteryPresentationState battery;
  UiPaletteHandle palette;
};

struct PhrasePageModel {
  std::uint8_t phrase;
  std::uint8_t track;
  std::uint8_t row;
  std::uint8_t column;
  std::array<PhraseRowModel, 16> rows;
  HelpModel help;
};
```

Building this model is the synchronization boundary between playback/model
state and UI rendering. The renderer does not repeatedly query `Player` while
drawing.

### 6.4 Retained scene commands

Pages emit compact commands into fixed-capacity arrays. Commands stay in paint
order, so the renderer does not allocate or sort.

```cpp
enum class UiCommandKind : std::uint8_t {
  FillRect,
  FillRoundedRect,
  GlyphRun,
  VuMeter,
  Battery,
  PushClip,
  PopClip,
};

struct UiCommand {
  UiCommandKind kind;
  PaletteToken token;
  RectI16 bounds;
  std::uint16_t payload_offset;
};

template <std::size_t MaxCommands, std::size_t TextBytes>
class UiCommandList {
  std::array<UiCommand, MaxCommands> commands_;
  std::array<char, TextBytes> text_;
  std::uint16_t command_count_;
  std::uint16_t text_size_;
};
```

Recommended limits are established by measurement of the most complex page,
then guarded by assertions and tests. Overflow must produce a visible
diagnostic in debug builds, never heap fallback.

### 6.5 Layer and transition ownership

`UiTransitionCoordinator` retains up to three page content scenes and two text
states per bar. A scene is immutable after submission.

```cpp
struct UiScene {
  UiCommandList<384, 3072> content;
  ChromeRequest chrome;
  CursorTarget cursor;
  std::uint32_t generation;
};

class UiTransitionCoordinator {
public:
  void Submit(UiScene &&scene, TransitionHint hint, std::uint32_t now_ms);
  bool Tick(std::uint32_t now_ms);
  void Compose(UiCompositor &compositor) const;
  bool IsAnimating() const;
};
```

Scene capacity values above are initial estimates, not acceptance numbers.

### 6.6 Indexed framebuffer

Use one 8-bit indexed 240 by 240 framebuffer rather than a full RGBA or double
RGB565 framebuffer.

Benefits:

- all semantic colors and derived fade/AA ramp colors fit comfortably;
- cursor corner antialiasing is deterministic;
- a complete frame is 57,600 bytes;
- RGB565 expansion happens only for dirty display strips;
- no second full framebuffer is needed;
- WASM can expand the same indices to RGBA for presentation and snapshots.

The compositor marks 8 by 8 dirty tiles. Before rendering a moving overlay, it
rerasterizes the affected base scene region and then paints the overlay. Dirty
tiles are merged into horizontal strips for the presenter.

```cpp
class UiIndexedSurface {
public:
  static constexpr int Width = 240;
  static constexpr int Height = 240;
  std::uint8_t *Pixels();
  void Clear(std::uint8_t color_index);
  void Fill(RectI16 rect, std::uint8_t color_index);
};

class UiPresenter {
public:
  virtual void Present(const UiIndexedSurface &, const UiPalette &,
                       Span<const DirtyStrip>) = 0;
};
```

### 6.7 Rounded highlight rasterization

Do not use a blur or a general vector antialiaser. Use a tiny radius-specific
coverage mask:

```text
radius 2 corner coverage, conceptual

0%  50% 100%
50% 100% 100%
100% 100% 100%
```

Coverage colors are precomputed from `cursor.primary` and the known underlying
surface. Horizontal and vertical edges remain 100% coverage and therefore
crisp.

## 7. Scheduler and ESP32-S3 performance

### 7.1 Scheduling

Keep the existing 33 ms UI clock as the default animation cadence. The audio
callback/task remains higher priority and never waits for UI locks.

The UI task renders only when at least one condition is true:

- a page/model generation changed;
- an animation is active;
- cursor target changed;
- playback indicators or VU values changed;
- play time changed;
- battery polling produced a visible change;
- a modal changed.

The transition coordinator uses monotonic milliseconds. If the UI task misses a
tick, it renders the correct current position on the next tick instead of
playing every missed animation frame.

### 7.2 Static memory budget

Initial target for the ESP32-S3 UI path:

| Item | Approximate bytes |
| --- | ---: |
| 240x240 8-bit indexed framebuffer | 57,600 |
| Two 240x8 RGB565 DMA staging strips | 7,680 |
| Dirty tile mask and strip list | <1,000 |
| Three retained scene command/text buffers | 18,000-24,000 |
| Palette, ramps, animation state | <4,000 |
| Total target | about 82-94 KB |

Only DMA staging buffers require DMA-capable internal memory. The indexed frame
and retained scenes may use normal internal RAM; PSRAM is an optional fallback,
not a design dependency. Final placement is chosen from map-file and runtime
profiling.

There is no allocation, string construction, scene resize, or color-ramp
generation in `Tick`, `Compose`, or `Present`.

### 7.3 Transfer budget

At rest, only changed tiles are transferred. Cursor motion transfers the union
of two small cursor/row regions. VU updates transfer only meter strips.

A content transition intentionally dirties the content viewport. At 240 by 174
pixels in RGB565, one animation frame transfers about 83.5 KB. At 30 Hz that is
about 2.5 MB/s while the short transition is active. This is compatible with a
well-configured SPI LCD path, but must be confirmed using the actual panel clock
and ESP-IDF driver overhead.

If hardware measurement misses the 33 ms budget, reduce transition refresh to
20 Hz while keeping time-based duration. Do not reduce input polling or audio
priority.

### 7.4 Performance acceptance targets

Initial targets, to be validated on the real ESP32-S3 board:

- no audio underruns attributable to UI activity;
- no per-frame heap allocations after UI initialization;
- idle UI tick under 0.5 ms when nothing is dirty;
- compositor CPU time under 5 ms for a full transition frame;
- total compose plus LCD submission under 25 ms at the chosen SPI clock;
- cursor-only update under 3 ms total;
- UI memory at or below 96 KB excluding static font bitmaps;
- animation state remains correct after dropped ticks and rapid direction
  reversal.

These are budgets, not claims about current measured performance.

## 8. WASM reference workflow

The existing WebAssembly workbench at this base revision is the right desktop
host because it already:

- runs the complete C++ application on an application pthread;
- provides a 240 by 240 canvas;
- maps all physical controls;
- exposes all views and modals for diagnostics;
- supports exact frame snapshots and E2E tests;
- keeps browser tooling separate from tracker behavior.

The UI2 implementation replaces the current character-frame rendering inside
the C++/WASM path. The browser does not recreate bars, cursors, or page layouts
as DOM elements.

Add a design-review mode to the workbench only for orchestration:

- choose any registered page and state;
- show multiple exact 240x240 canvases on a large review canvas;
- select 1x/2x/integer zoom;
- choose battery, playback, Nav, Enter-held, cursor, and Bottom Bar states;
- capture fixed timestamps from a transition;
- inspect semantic palette tokens;
- compare golden frames.

The review controls drive C++ diagnostic models through an explicit test ABI.
They do not mutate production model logic through hidden JavaScript shortcuts.

## 9. Input-to-presentation contract

Input processing remains immediate and authoritative. UI animation is feedback,
not an input state machine.

```cpp
struct InputPresentationState {
  bool nav_held;
  bool enter_held;
  bool edit_held;
  bool alt_held;
  std::uint8_t active_track;
  std::uint32_t generation;
};
```

For numbered pages:

```text
Enter press        -> Top Bar number bubble becomes active;
                      Bottom Bar changes to Track Notes;
Enter + Up/Down    -> change page number;
Enter + Left/Right -> change current track and move the T# bubble;
Enter release      -> restore cursor-context Bottom Bar and cell cursor.
```

The view-switch event gains an optional transition hint:

```cpp
enum class TransitionAxis : std::uint8_t { None, Horizontal, Vertical };
enum class TransitionSign : std::int8_t { Negative = -1, Positive = 1 };

struct ViewSwitchRequest {
  ViewType target;
  TransitionAxis axis;
  TransitionSign sign;
  bool animate;
};
```

Legacy calls may default to `None` during migration. Production Nav routes must
eventually provide an explicit hint so the compositor never guesses from enum
ordering.

## 10. Migration plan

### Phase 0: lock the contract

- Approve key visuals and exact 240x240 layouts.
- Approve this architecture and motion timings.
- Record a state matrix for every page, Top Bar, Bottom Bar, cursor, modal, and
  playback variant.
- Freeze semantic palette names.

### Phase 1: freeze and implement API v1 on the host

- Add the platform-independent public types, fixed storage contract, command
  list, indexed surface, dirty tiles, and `UiEngine`.
- Compile them into the ordinary host test binary without any platform SDK.
- Test capacity, clipping, fixed-point time, deterministic output, and the
  single-present-call contract.
- Do not add an ESP32 implementation yet; instead enforce ESP constraints in
  the types and tests.

### Phase 2: make WASM the first real API user

- Add glyph-run, fill, rounded fill, clip, battery, and VU primitives.
- Add `WasmUiPresenter` and present indexed frames through the existing WebGL
  canvas.
- Keep the existing web workbench, 240x240 canvas, input, audio, and diagnostics
  unchanged.
- Add unit tests for bounds, clipping, corner coverage, token mapping, and
  deterministic frame hashes.

### Phase 3: chrome, cursor, and motion

- Implement Top Bar and Bottom Bar components.
- Implement `ChromeResolver` and its priority tests.
- Implement fixed-point motion tracks, retained scenes, page transition, bar
  crossfade, Bottom Bar reveal, and cursor motion.
- Add interruption/reversal tests.

### Phase 4: representative pages

Implement Song, Phrase, Phrase Table, Instrument, and Mixer first. Together
they exercise:

- dense 8-track and 16-row data;
- contextual help and hidden bars;
- Enter-held number/track mode;
- selectors and name actions;
- stereo VU meters and gradients;
- page transitions and cursor resizing.

Do not migrate every page until these five match approved frames and motion.

### Phase 5: approve API v1 and port the presenter to ESP32-S3

- Review actual command counts, text-pool use, indexed-frame memory, dirty
  transfer volume, and WASM timeline traces.
- Freeze API v1; page primitives may no longer be added only for desktop
  convenience.
- Implement `Esp32UiPresenter` against the same read-only indexed surface,
  palette, and dirty strips.
- Expand only dirty strips to RGB565 DMA buffers.
- Profile internal RAM, PSRAM, SPI transfer time, task time, and audio underruns.
- Tune strip height and transition refresh cadence from measurements without
  changing page code.

### Phase 6: remaining pages and modals

- Chain, Project, Device, Instrument Table, Groove, Theme, Font;
- browsers, import pages, sample editor/slices, record;
- message, text input, render progress, and full-screen modals;
- all documented field and Bottom Bar variants.

### Phase 7: default-on and cleanup

- Run UI2 by default on WASM and ESP32-S3 after acceptance.
- Keep the legacy renderer available until project/theme/RemoteUI compatibility
  is demonstrated.
- Remove duplicate immediate-drawing paths only in a separate cleanup change.

## 11. Testing and acceptance

### 11.1 Golden frame matrix

Every output is exactly 240 by 240. At minimum capture:

- every page's default state;
- every cursor column type;
- empty/non-empty contextual Bottom Bars;
- action and selector bars;
- Enter-held number and track switch state;
- playback cursor overlap state;
- Top Bar battery/play/Nav states;
- all instrument types;
- Project cleanup/render/name states;
- dialogs and full-screen states.

Frame hashes are taken from the indexed buffer before platform conversion so
WASM and ESP32 rendering can be compared deterministically.

### 11.2 Motion tests

Snapshot transition progress at deterministic times such as 0, 33, 66, 99,
132, 165, and completion. Assert:

- content bounds translate as a single layer;
- bar backgrounds never translate;
- bar text opacity changes monotonically;
- cursor bounds follow the selected curve;
- rapid same-direction navigation remains continuous;
- reversal begins from the current position;
- the final scene is pixel-identical to its static golden frame;
- no dirty pixels remain after completion.

### 11.3 State and input tests

- Chrome priority is tested as a pure function.
- Enter-held Up/Down and Left/Right produce the correct model and chrome state.
- Releasing Enter restores the correct contextual Bottom Bar.
- Selectors honor their per-field wrap flag.
- Song and Phrase playback cursors remain independent per track.
- UI animation never changes tracker data by itself.

### 11.4 Hardware tests

- Record compose time, dirty pixels, transfer bytes, transfer time, and missed
  UI deadlines.
- Run audio while repeatedly navigating and opening Nav/Enter-held bars.
- Run maximum VU activity and cursor repeat simultaneously.
- Verify no audio underrun, watchdog reset, heap growth, or visible tearing.
- Capture the physical panel at 1x to review pixel corners and low-contrast row
  fills; browser zoom cannot substitute for this check.

## 12. Decisions that keep the design efficient

- One shared C++ renderer; no parallel browser UI implementation.
- One indexed framebuffer; no always-on RGB565 double buffer.
- Retained command lists rather than retained full-page bitmaps.
- Fixed-point timelines and precomputed color ramps.
- Short 30 Hz motion designed around the existing scheduler.
- Dirty tiles at rest; full content transfer only during a short transition.
- Cursor rerasterizes a small union region instead of the complete page.
- Central bar resolution prevents multiple views from redrawing conflicting
  chrome.
- No frame-time allocation or general vector/blur engine.
- Time-based animation drops visual frames safely under load instead of
  blocking audio or input.

## 13. Approval gates

Implementation should not start across all pages at once. Approval is split:

1. Static Song/Phrase/Table/Instrument/Mixer frames.
2. Page slide with Top/Bottom text fade.
3. Cursor motion and Enter-held number/track state.
4. Nav cross and expanding Bottom Bar.
5. Semantic palette and physical-screen contrast.
6. ESP32-S3 timing and memory measurements.

Only after gates 1-4 should the remaining pages be migrated.
