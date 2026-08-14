# iPlug2 + GMPI-UI

[![build](https://github.com/JeffMcClintock/iPlug_gmpi_ui/actions/workflows/build.yml/badge.svg)](https://github.com/JeffMcClintock/iPlug_gmpi_ui/actions/workflows/build.yml)

A gain plugin whose DSP is [iPlug2](https://github.com/iPlug2/iPlug2) and whose
user interface is [GMPI-UI](https://github.com/JeffMcClintock/gmpi_ui), instead
of iPlug2's own IGraphics.

<p align="center">
  <img src="docs/screenshot.png" width="360" alt="A glowing vector knob with a dB readout in the centre"/>
</p>

VST3, Audio Unit and standalone, on Windows and macOS. All dependencies are
fetched by CMake — nothing to download by hand:

```bash
cmake -S . -B build
cmake --build build --config Release
```

The first configure clones iPlug2 and the Steinberg VST3 SDK, so give it a few
minutes. To build against local checkouts instead, set any of
`-DIPLUG2_FOLDER_OVERRIDE=…`, `-DGMPI_UI_FOLDER_OVERRIDE=…`,
`-DGMPI_SDK_FOLDER_OVERRIDE=…`.

CI builds the plugin on Windows and macOS on every push, and uploads the
binaries as artifacts.

**Linux** builds the editor but not the plugin. iPlug2 has no Linux support —
its `Scripts/cmake/IPlug.cmake` stops with *"Error - Linux not yet supported"* —
so configuring on Linux builds `tests/ui_compile_check.cpp` instead, which
compiles `GainView` on its own. GMPI-UI itself runs on Linux happily; it is the
plugin framework that doesn't. CI compiles it there on every push so the editor
stays portable for when that changes.

---

# How to integrate GMPI-UI with iPlug2

Five steps. The whole integration is about 120 lines; the rest of the repo is
the knob.

Each source file carries a `STEP n of 5` banner, so you can read them in order.

### Step 1 — turn IGraphics off, and compile GMPI-UI

In [`CMakeLists.txt`](CMakeLists.txt):

```cmake
iplug_add_plugin(${PROJECT_NAME}
    SOURCES  …
    UI NONE                  # don't build or link IGraphics
    DEFINES NO_IGRAPHICS     # …and don't expect it in the headers either
    LINK gmpi_ui
    FORMATS APP VST3 AU
)
```

Both are needed. `UI NONE` drops the library; `NO_IGRAPHICS` is what makes
`IPlugDelegate_select.h` choose plain `IEditorDelegate` as your plugin's base
class instead of `IGEditorDelegate`.

GMPI-UI is header-only except for its platform backends, which you compile
yourself. That list is short:

| | always | Windows | macOS |
|---|---|---|---|
| sources | `backends/DrawingFrameCommon.cpp`, `helpers/Timer.cpp` | `backends/DirectXGfx.cpp`, `backends/DrawingFrameWin.cpp` | `backends/DrawingFrameMac.mm` |
| link | | `d2d1 dwrite d3d11 dxgi dxguid Dwmapi windowscodecs` | `Cocoa QuartzCore CoreText CoreGraphics ImageIO Accelerate` |

> **Put those in their own static library, not in the plugin target.**
> GMPI-UI's Win32 code is a `UNICODE` build and iPlug2's standalone-app code is
> ANSI. In one target, either choice breaks the other — you get a wall of
> `cannot convert from 'const char[14]' to 'LPCWSTR'` from `IPlugAPP_main.cpp`.
> Keep `UNICODE` `PRIVATE` to the GMPI-UI library and both compile happily.

### Step 2 — bridge the window handle

iPlug2's entire editor contract, once IGraphics is gone, is:

```cpp
void* OpenWindow(void* pParent);   // return an HWND / NSView*
void  CloseWindow();
```

GMPI-UI produces exactly that, via two unrelated APIs:

| | Windows | macOS |
|---|---|---|
| host window | `gmpi::hosting::DrawingFrame` | `createNativeView()` |
| renderer | Direct2D | CoreGraphics |

[`GmpiPlugFrame`](src/GmpiPlugFrame.h) hides the split behind `Open`/`Close`,
with one implementation file each. On Windows it is essentially:

```cpp
frame.attachClient(pClient);          // before open(), see below
const gmpi::drawing::SizeL size{width, height};
frame.open(pParent, &size);
return frame.getWindowHandle();
```

> **`attachClient` before `open`.** `open()` lays out whatever client is
> already attached. Attach afterwards and your view sits at zero size until
> something else happens to trigger a re-layout.

### Step 3 — implement the two overrides

In [`IPlugGmpiGain.cpp`](src/IPlugGmpiGain.cpp):

```cpp
void* IPlugGmpiGain::OpenWindow(void* pParent)
{
  if (!mView)
    mView = new GainView(*this);

  void* handle = mFrame.Open(pParent, static_cast<gmpi::api::IDrawingClient*>(mView),
                             PLUG_WIDTH, PLUG_HEIGHT);
  OnUIOpen();      // seeds the editor with current parameter values
  return handle;
}
```

GMPI-UI objects are refcounted, so `CloseWindow` calls `mView->release()`
rather than `delete`.

> **Don't forget `OnUIOpen()`.** It calls `OnParamChangeUI` once per parameter.
> Without it, a *reopened* editor shows stale values — which looks fine in
> testing, because the first open usually happens to be correct.

### Step 4 — write the editor

[`GainView`](src/GainView.h) implements two GMPI-UI interfaces and contains no
iPlug2 types at all:

```cpp
class GainView final : public gmpi::api::IDrawingClient,
                       public gmpi::api::IInputClient
```

The host drives it in a fixed order — `setHost` → `measure` → `arrange` →
`render` — and `setHost` is where you `queryInterface` for the services you
want: `IDrawingHost` (repaint, drawing factory), `IInputHost` (mouse capture),
`IDialogHost` (native text edits, menus, file dialogs).

> **Everything is in DIPs, not pixels.** The rect you get in `arrange` is
> already DPI-scaled — a 360px window is 240 DIPs wide at 150%. Derive your
> layout from that rect rather than from `PLUG_WIDTH`, or your UI will be the
> wrong size on any display that isn't the one you developed on. This is the
> single easiest thing to get wrong.

### Step 5 — wire the parameters both ways

The view speaks in normalized 0..1 and reports gestures through a three-method
interface. The plugin maps that onto iPlug2:

```
GainView  ──OnKnobValueChanged──▶  SendParameterValueFromUI
GainView  ◀──────SetValue───────   OnParamChangeUI
```

`SendParameterValueFromUI` sets the parameter, informs the host **and** calls
`OnParamChangeUI` — so a UI edit refreshes the view through the same path host
automation uses. There is no separate "update the UI after a UI edit" step, and
no feedback loop to break.

Because of that, DAW automation moves the knob with no extra code.

> **Bracket drags with `BeginInformHostOfParamChangeFromUI` /
> `End…`.** Skipping them mostly works, and then misbehaves while the host is
> writing automation in a live take — the worst place to debug it.

### Optional — native text entry

Clicking the readout calls `IDialogHost::createTextEdit`, which gives you the
real platform edit control: system caret, selection, IME and accessibility for
free. The typed string goes back through `IKnobHost`, and iPlug2's
`IParam::StringToValue` parses it, because the parameter owns its units.

> **Dialogs are asynchronous.** `showAsync` returns immediately. Both the
> editor object and its callback must outlive the call — members, not locals.
> `gmpi::sdk::TextEditCallback` is additionally `GMPI_REFCOUNT_NO_DELETE`, so a
> `shared_ptr` to a `new`'d one leaks; own it outright and pass its address.

---

## Notes on the drawing

- The halo is a real gaussian blur, not stacked translucent strokes: the arc is
  rendered into an offscreen mask, blurred and tinted by
  `helpers/CachedBlur.h`, and cached until the value changes.
- No image or font assets. It is all vector paths and system fonts.

## One more build wrinkle

iPlug2 vendors RTAudio and RTMidi but *not* the VST3 SDK — its own
`Dependencies/IPlug/download-vst3-sdk.sh` normally fetches that by hand. This
project's CMake does it instead, into the path iPlug2 hardcodes, so a clean
checkout configures in one step.

## Licence

MIT. iPlug2, GMPI-UI and the VST3 SDK carry their own licences.
