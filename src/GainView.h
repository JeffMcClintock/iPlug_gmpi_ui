#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Drawing.h"
#include "RefCountMacros.h"
#include "helpers/CachedBlur.h"
#include "helpers/NativeUi.h"

/*
================================ STEP 4 of 5 =================================
The editor itself. Everything here is plain GMPI-UI - there is not one iPlug2
type in this file, and that is deliberate: the same view would drop into a VST3
wrapper or a SynthEdit module unchanged.

A GMPI-UI editor is an object implementing two interfaces:

  IDrawingClient   measure / arrange / render  - what it looks like
  IInputClient     onPointerDown / Move / Up   - what it does

Both are COM-style: refcounted, discovered through queryInterface. The two
macros at the bottom of the class (GMPI_QUERYINTERFACE, GMPI_REFCOUNT) supply
the boilerplate; you list the interfaces you implement and that is that.

The host calls you in this order:

  setHost(host)        here is the surface you draw on. Call queryInterface on
                       it for the host services you need. setHost(nullptr) at
                       shutdown means "let go, the window is going away".
  measure(avail, out)  how big would you like to be?
  arrange(rect)        this is the rect you actually got. Save it.
  render(context)      draw. Called for every dirty region, possibly often.

All coordinates are DIPs (device-independent pixels), not physical pixels. The
frame applies the DPI scale for you, so never assume the rect you are given
matches the pixel size of the window - see Layout below.

To ask for a repaint, call IDrawingHost::invalidateRect. Never draw outside
render().
==============================================================================
*/

// How this view reports changes back to whoever owns it. Values are normalized
// 0..1, because a UI control has no business knowing about decibels.
//
// Begin/End bracket a gesture (a drag), which is what lets a host record
// automation as one continuous move rather than a burst of unrelated edits.
struct IKnobHost
{
  virtual void OnKnobGestureBegin() = 0;
  virtual void OnKnobValueChanged(double normalized) = 0;
  virtual void OnKnobGestureEnd() = 0;

  // Typed into the readout, e.g. "-6.5 dB". The host owns the parsing, because
  // it owns the units.
  virtual void OnKnobTextEntered(const std::string& text) = 0;
};

namespace knob
{
using namespace gmpi::drawing;

constexpr float pi = 3.14159265358979f;

// Angles are degrees clockwise from 12 o'clock, so the knob's dead zone sits
// symmetrically at the bottom.
constexpr float angleMin = -135.0f;
constexpr float angleMax = 135.0f;

inline const Color bgTop      = colorFromHex(0x2B3440u);
inline const Color bgBottom   = colorFromHex(0x090C10u);
inline const Color halo       = colorFromHex(0x4A6E92u);
inline const Color track      = colorFromHex(0x05070Au);
inline const Color arcLow     = colorFromHex(0x36E0C8u);
inline const Color arcHigh    = colorFromHex(0x8A72FFu);
inline const Color knobTop    = colorFromHex(0x3C4757u);
inline const Color knobBottom = colorFromHex(0x12161Cu);
inline const Color textBright = colorFromHex(0xEDF4FBu);
inline const Color textDim    = colorFromHex(0x76889Au);
inline const Color pointer    = colorFromHex(0xEAF5FFu);

// Every dimension is derived from the rect the host gave us, and `unit` scales
// stroke widths and type sizes along with it.
//
// This is worth copying. The tempting shortcut is to hardcode the layout to
// PLUG_WIDTH/PLUG_HEIGHT from config.h - which looks fine on the machine you
// wrote it on, and is wrong the moment the editor is scaled. GMPI-UI hands you
// a rect in DIPs; on a 150% display the same window is 240 DIPs wide, not 360.
// Derive from the rect and the problem disappears.
struct Layout
{
  Point centre{};
  float arcRadius{};
  float bodyRadius{};
  float unit{}; // stroke widths and type sizes scale off this
  Rect readoutRect{}; // drawn here, and clicked here to type a value

  explicit Layout(Rect b)
  {
    const float w = b.right - b.left;
    const float h = b.bottom - b.top;

    arcRadius = std::min(w * 0.29f, h * 0.31f);
    bodyRadius = arcRadius * 0.635f;
    unit = arcRadius / 104.0f;
    centre = {(b.left + b.right) * 0.5f, b.top + h * 0.53f};

    const float halfWidth = bodyRadius * 0.98f;
    readoutRect = {centre.x - halfWidth, centre.y - 22.0f * unit,
                   centre.x + halfWidth, centre.y + 22.0f * unit};
  }
};

inline bool contains(Rect r, Point p)
{
  return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

inline Point onCircle(Point centre, float radius, float degrees)
{
  const float a = degrees * (pi / 180.0f);
  return {centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)};
}

// A stroked (not filled) circular arc, built as a path geometry.
//
// GMPI-UI geometry is built through a "sink": open the geometry, begin a
// figure at a start point, add segments, end it, close the sink. FigureBegin::
// Hollow and FigureEnd::Open say "this is a line, not the outline of a shape",
// which is what stops the two ends being joined by a chord.
//
// addArc takes the END point plus the ellipse radii and asks which of the four
// possible arcs you meant: which direction, and the short way round or the
// long way. Same model as SVG's elliptical arc command.
inline PathGeometry arc(Factory factory, Point centre, float radius, float from, float to)
{
  auto geometry = factory.createPathGeometry();
  auto sink = geometry.open();
  sink.beginFigure(onCircle(centre, radius, from), FigureBegin::Hollow);
  sink.addArc({onCircle(centre, radius, to),
               {radius, radius},
               0.0f,
               SweepDirection::Clockwise,
               (to - from) > 180.0f ? ArcSize::Large : ArcSize::Small});
  sink.endFigure(FigureEnd::Open);
  sink.close();
  return geometry;
}
} // namespace knob

class GainView final : public gmpi::api::IDrawingClient, public gmpi::api::IInputClient
{
public:
  GainView(IKnobHost& host) : mHost(host) {}

  void SetValue(double normalized, const char* display)
  {
    mValue = std::clamp(normalized, 0.0, 1.0);
    mDisplay = display;
    mGlow.invalidate();
    Invalidate();
  }

  // IDrawingClient::setHost and IInputClient::setHost have identical
  // signatures, so this single override satisfies both - a C++ multiple-
  // inheritance quirk that GMPI-UI leans on deliberately.
  //
  // `pHost` is one object wearing several hats. Ask it for the services you
  // want:
  //   IDrawingHost  invalidateRect, the drawing factory, the DPI scale
  //   IInputHost    mouse capture
  //   IDialogHost   native text edits, popup menus, file dialogs
  //
  // Note we do NOT keep `pHost` itself. queryInterface addRefs what it returns,
  // which is what these shared_ptrs hold. A raw copy of pHost would be an
  // un-owned reference to the frame.
  gmpi::ReturnCode setHost(gmpi::api::IUnknown* pHost) override
  {
    // setHost(nullptr) is the teardown signal, and the only chance to drop our
    // references before the frame goes away. Anything holding a window - like
    // an in-flight text edit - has to go first.
    mTextEdit = {};
    mEditing = false;

    mDrawingHost = {};
    mInputHost = {};
    mDialogHost = {};

    if (pHost)
    {
      pHost->queryInterface(&gmpi::api::IDrawingHost::guid, mDrawingHost.put_void());
      pHost->queryInterface(&gmpi::api::IInputHost::guid, mInputHost.put_void());
      pHost->queryInterface(&gmpi::api::IDialogHost::guid, mDialogHost.put_void());
    }
    return gmpi::ReturnCode::Ok;
  }

  // "Given at most this much room, how much do you want?"
  //
  // Echoing availableSize back means "I am resizable, I'll fill whatever you
  // give me". A fixed-size editor returns the SAME CONSTANT every time and
  // ignores availableSize - that difference is how hosts detect resizability,
  // so "clamp to availableSize" is not the same thing and will bite you.
  gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
  {
    *returnDesiredSize = *availableSize;
    return gmpi::ReturnCode::Ok;
  }

  // The size we actually got. Everything laid out later derives from this.
  gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
  {
    mBounds = *finalRect;
    mGlow.invalidate(); // cached blur is size-dependent
    return gmpi::ReturnCode::Ok;
  }

  // How much of the surface we might paint. The frame uses it to decide what
  // needs redrawing; claim no more than you actually cover.
  gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
  {
    *returnRect = mBounds;
    return gmpi::ReturnCode::Ok;
  }

  // Draw. Everything visible in the screenshot happens below.
  //
  // Two things to know before reading it:
  //
  // 1. `Graphics` is a thin C++ wrapper you construct around the raw context
  //    the host passes in. `Factory` makes the objects that outlive a single
  //    call in principle (geometry, stroke styles, text formats), while the
  //    context makes brushes.
  // 2. Creating brushes and geometry per frame looks wasteful and is not -
  //    these are cheap handles, and the platform backends cache underneath. Do
  //    the obvious thing first; cache only what you have measured. The one
  //    exception here is the blur, which is genuinely expensive and so is
  //    cached explicitly.
  gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
  {
    using namespace gmpi::drawing;
    using namespace knob;

    Graphics g(drawingContext);
    auto factory = g.getFactory();

    const Layout layout(mBounds);
    const Point centre = layout.centre;
    const float unit = layout.unit;
    const float valueAngle = angleMin + (angleMax - angleMin) * static_cast<float>(mValue);

    StrokeStyleProperties roundCap{};
    roundCap.lineCap = CapStyle::Round;
    auto roundStroke = factory.createStrokeStyle(roundCap);

    // ---- background ----
    // A vertical gradient, plus a radial wash behind the knob so the panel is
    // not flat. The radial brush's outer stop is fully transparent, which is
    // how you fade something out rather than fade it to a colour.
    {
      auto bg = g.createLinearGradientBrush({0, mBounds.top}, {0, mBounds.bottom}, bgTop, bgBottom);
      g.fillRectangle(mBounds, bg);

      const float haloRadius = layout.arcRadius * 1.7f;
      const Gradientstop haloStops[] = {
        {0.0f, Color{halo.r, halo.g, halo.b, 0.34f}},
        {1.0f, Color{halo.r, halo.g, halo.b, 0.0f}}
      };
      auto haloBrush = g.createRadialGradientBrush(haloStops, centre, haloRadius);
      g.fillCircle(centre, haloRadius, haloBrush);
    }

    // ---- tick marks ----
    {
      auto tickBrush = g.createSolidColorBrush(Color{textDim.r, textDim.g, textDim.b, 0.45f});
      for (int i = 0; i <= 10; ++i)
      {
        const float a = angleMin + (angleMax - angleMin) * (i / 10.0f);
        const bool major = (i % 5) == 0;
        g.drawLine(onCircle(centre, layout.arcRadius + 11.0f * unit, a),
                   onCircle(centre, layout.arcRadius + (major ? 20.0f : 16.0f) * unit, a),
                   tickBrush,
                   (major ? 2.0f : 1.0f) * unit);
      }
    }

    // ---- arc track ----
    {
      auto trackGeometry = arc(factory, centre, layout.arcRadius, angleMin, angleMax);
      auto trackBrush = g.createSolidColorBrush(Color{track.r, track.g, track.b, 0.85f});
      g.drawGeometry(trackGeometry, trackBrush, 13.0f * unit, roundStroke);
    }

    // ---- glowing value arc ----
    if (valueAngle > angleMin + 0.4f)
    {
      const Point tip = onCircle(centre, layout.arcRadius, valueAngle);

      // The halo. GMPI-UI's helpers/CachedBlur.h does the real work: it renders
      // your drawing into an offscreen single-channel mask, gaussian-blurs the
      // mask, tints it, and blits the result. It caches that bitmap until you
      // call invalidate(), which is why SetValue and arrange() both do.
      //
      // The lambda draws in the SAME coordinates as the main render - the mask
      // bitmap is the size of the rect you pass, and is blitted back to the
      // origin. (Which means the rect must start at 0,0; ours does.)
      //
      // Colour is irrelevant inside the mask - only coverage is kept - so paint
      // it white and let `tint` decide the colour afterwards.
      mGlow.blurRadius = std::max(4, static_cast<int>(15.0f * unit));
      mGlow.tint = interpolateColor(arcLow, arcHigh, 0.35f + 0.5f * static_cast<float>(mValue));

      const auto paintMask = [&](Graphics& mask) {
        auto maskFactory = mask.getFactory();
        auto maskStroke = maskFactory.createStrokeStyle(roundCap);
        auto white = mask.createSolidColorBrush(Colors::White);

        auto geometry = arc(maskFactory, centre, layout.arcRadius, angleMin, valueAngle);
        mask.drawGeometry(geometry, white, 13.0f * unit, maskStroke);
        mask.fillCircle(tip, 11.0f * unit, white);
      };

      // Blurring spreads the arc's energy out, so one pass reads as washed out.
      // The second call is nearly free - it blits the cached bitmap again.
      mGlow.draw(g, mBounds, paintMask);
      mGlow.draw(g, mBounds, paintMask);

      // The crisp arc, drawn on top of its own halo. This one carries the
      // gradient: a linear brush laid diagonally across the knob, so the sweep
      // runs teal at the bottom-left through to violet at the top-right.
      auto valueGeometry = arc(factory, centre, layout.arcRadius, angleMin, valueAngle);
      const Gradientstop stops[] = {{0.0f, arcLow}, {1.0f, arcHigh}};
      auto stopCollection = g.createGradientstopCollection(stops);
      const LinearGradientBrushProperties axis{
        {centre.x - layout.arcRadius, centre.y + layout.arcRadius},
        {centre.x + layout.arcRadius, centre.y - layout.arcRadius}};

      auto arcBrush = g.createLinearGradientBrush(axis, BrushProperties{}, stopCollection);
      g.drawGeometry(valueGeometry, arcBrush, 10.0f * unit, roundStroke);

      auto coreBrush = g.createSolidColorBrush(Color{1.0f, 1.0f, 1.0f, 0.5f});
      g.drawGeometry(valueGeometry, coreBrush, 2.6f * unit, roundStroke);

      auto bead = g.createSolidColorBrush(Color{1.0f, 1.0f, 1.0f, 0.92f});
      g.fillCircle(tip, 4.0f * unit, bead);
    }

    // ---- knob body ----
    {
      const float bodyRadius = layout.bodyRadius;

      auto shadow = g.createSolidColorBrush(Color{0.0f, 0.0f, 0.0f, 0.45f});
      g.fillCircle({centre.x, centre.y + 3.0f * unit}, bodyRadius + 3.0f * unit, shadow);

      const Gradientstop bodyStops[] = {{0.0f, knobTop}, {1.0f, knobBottom}};
      auto bodyBrush = g.createRadialGradientBrush(
        bodyStops, {centre.x - bodyRadius * 0.35f, centre.y - bodyRadius * 0.45f}, bodyRadius * 1.7f);
      g.fillCircle(centre, bodyRadius, bodyBrush);

      auto rim = g.createSolidColorBrush(Color{1.0f, 1.0f, 1.0f, 0.10f});
      g.drawCircle(centre, bodyRadius - 0.6f * unit, rim, 1.2f * unit);

      // Pointer notch. It lives in the gap between the body and the arc rather
      // than on the body, where at 3 and 9 o'clock it would sit right on top of
      // the readout and read as a stray character.
      auto pointerBrush = g.createSolidColorBrush(pointer);
      g.drawLine(onCircle(centre, layout.arcRadius * 0.72f, valueAngle),
                 onCircle(centre, layout.arcRadius * 0.86f, valueAngle),
                 pointerBrush, 3.5f * unit, roundStroke);
    }

    // ---- text ----
    {
      const std::string_view families[] = {"Segoe UI", "Helvetica Neue", "Arial"};

      auto brightBrush = g.createSolidColorBrush(textBright);
      auto dimBrush = g.createSolidColorBrush(textDim);

      auto centred = [&](float size, FontWeight weight) {
        auto format = factory.createTextFormat(size, families, weight);
        format.setTextAlignment(TextAlignment::Center);
        format.setParagraphAlignment(ParagraphAlignment::Center);
        return format;
      };

      const Rect readoutRect = layout.readoutRect;

      auto label = centred(10.5f * unit, FontWeight::SemiBold);
      g.drawTextU("G A I N", label,
                  {readoutRect.left, centre.y - 42.0f * unit, readoutRect.right, readoutRect.top},
                  dimBrush);

      // Hovering the readout tells you it is a text field.
      if (mHoverReadout && !mEditing)
      {
        const RoundedRect field{readoutRect, 5.0f * unit, 5.0f * unit};
        auto highlight = g.createSolidColorBrush(Color{1.0f, 1.0f, 1.0f, 0.09f});
        g.fillRoundedRectangle(field, highlight);
        auto outline = g.createSolidColorBrush(Color{1.0f, 1.0f, 1.0f, 0.16f});
        g.drawRoundedRectangle(field, outline, 1.0f * unit);
      }

      // While the native edit box is up it covers this spot; drawing underneath
      // it just shows through as a smudge on a partly-transparent field.
      if (!mEditing)
      {
        auto readout = centred(31.0f * unit, FontWeight::SemiBold);
        g.drawTextU(mDisplay, readout, readoutRect, brightBrush);
      }

      auto title = centred(11.0f * unit, FontWeight::Medium);
      g.drawTextU("iPlug2  \xC2\xB7  GMPI-UI", title,
                  {mBounds.left, mBounds.top + 14.0f * unit, mBounds.right, mBounds.top + 34.0f * unit},
                  dimBrush);

      auto hint = centred(9.5f * unit, FontWeight::Regular);
      auto hintBrush = g.createSolidColorBrush(Color{textDim.r, textDim.g, textDim.b, 0.7f});
      g.drawTextU("drag  \xC2\xB7  shift = fine  \xC2\xB7  double-click resets  \xC2\xB7  click value to type", hint,
                  {mBounds.left, mBounds.bottom - 32.0f * unit, mBounds.right, mBounds.bottom - 12.0f * unit},
                  hintBrush);
    }

    return gmpi::ReturnCode::Ok;
  }

  // ---------------------------------------------------------------------
  // IInputClient. Points arrive in the same DIP space you drew in, so you can
  // hit-test straight against your layout with no conversion.
  //
  // Return Handled to consume an event, Unhandled to let it pass. Ok from
  // hitTest means "yes, that point is mine" - this view claims the whole rect.
  // ---------------------------------------------------------------------
  gmpi::ReturnCode hitTest(gmpi::drawing::Point, int32_t) override { return gmpi::ReturnCode::Ok; }

  gmpi::ReturnCode setHover(bool isMouseOverMe) override
  {
    if (!isMouseOverMe)
      SetHoverReadout(false);

    return gmpi::ReturnCode::Ok;
  }

  gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
  {
    if (0 == (flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton)))
      return gmpi::ReturnCode::Unhandled;

    if (!mEditing && knob::contains(knob::Layout(mBounds).readoutRect, point))
    {
      BeginTextEdit();
      return gmpi::ReturnCode::Handled;
    }

    if (flags & static_cast<int32_t>(gmpi::api::PointerFlags::Double))
    {
      mHost.OnKnobGestureBegin();
      mHost.OnKnobValueChanged(kDefaultValue);
      mHost.OnKnobGestureEnd();
      return gmpi::ReturnCode::Handled;
    }

    // Anchor the drag to where it started rather than accumulating deltas:
    // accumulating drifts, and clamping at either end then loses your place.
    mDragging = true;
    mDragStartY = point.y;
    mDragStartValue = mValue;

    // Capture routes the mouse to us even once it leaves the window, so a drag
    // does not stop dead at the edge of the editor. Every setCapture needs its
    // releaseCapture - ours is in onPointerUp.
    if (mInputHost)
      mInputHost->setCapture();

    mHost.OnKnobGestureBegin();
    return gmpi::ReturnCode::Handled;
  }

  gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
  {
    if (!mDragging)
    {
      SetHoverReadout(!mEditing && knob::contains(knob::Layout(mBounds).readoutRect, point));
      return gmpi::ReturnCode::Unhandled;
    }

    // Full-scale drag is roughly three knob diameters, so the feel stays the
    // same whatever size the editor is.
    const float range = knob::Layout(mBounds).arcRadius * 2.4f;
    const bool fine = 0 != (flags & static_cast<int32_t>(gmpi::api::PointerFlags::KeyShift));
    const double travel = (mDragStartY - point.y) / (range * (fine ? 5.0 : 1.0));

    mHost.OnKnobValueChanged(std::clamp(mDragStartValue + travel, 0.0, 1.0));
    return gmpi::ReturnCode::Handled;
  }

  gmpi::ReturnCode onPointerUp(gmpi::drawing::Point, int32_t) override
  {
    if (!mDragging)
      return gmpi::ReturnCode::Unhandled;

    mDragging = false;

    if (mInputHost)
      mInputHost->releaseCapture();

    mHost.OnKnobGestureEnd();
    return gmpi::ReturnCode::Handled;
  }

  gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point, int32_t flags, int32_t delta) override
  {
    const bool fine = 0 != (flags & static_cast<int32_t>(gmpi::api::PointerFlags::KeyShift));
    const double step = (delta / 120.0) * (fine ? 0.005 : 0.025);

    mHost.OnKnobGestureBegin();
    mHost.OnKnobValueChanged(std::clamp(mValue + step, 0.0, 1.0));
    mHost.OnKnobGestureEnd();
    return gmpi::ReturnCode::Handled;
  }

  gmpi::ReturnCode onKeyPress(wchar_t) override { return gmpi::ReturnCode::Unhandled; }
  gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point, gmpi::api::IUnknown*) override { return gmpi::ReturnCode::Unhandled; }
  gmpi::ReturnCode getToolTip(gmpi::drawing::Point, gmpi::api::IString*) override { return gmpi::ReturnCode::Unhandled; }

  // How the frame discovers what we implement. List every interface here or
  // the host will silently not use it - a view that renders but ignores the
  // mouse is usually a missing line in this function.
  gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
  {
    *returnInterface = {};
    GMPI_QUERYINTERFACE(gmpi::api::IDrawingClient);
    GMPI_QUERYINTERFACE(gmpi::api::IInputClient);
    return gmpi::ReturnCode::NoSupport;
  }

  // Supplies addRef/release: starts at 1, deletes itself at 0. So the owner
  // calls release() rather than delete - see IPlugGmpiGain::CloseWindow.
  GMPI_REFCOUNT

private:
  // 0 dB on a -60..+12 dB scale.
  static constexpr double kDefaultValue = 60.0 / 72.0;

  // Ask for a repaint. nullptr means the whole surface; pass a rect to redraw
  // only part of it, which is worth doing for anything animating frequently.
  void Invalidate()
  {
    if (mDrawingHost)
      mDrawingHost->invalidateRect(nullptr);
  }

  void SetHoverReadout(bool hover)
  {
    if (hover == mHoverReadout)
      return;

    mHoverReadout = hover;
    Invalidate();
  }

  // Pop a native text field over the readout.
  //
  // GMPI-UI does not draw its own text editor - IDialogHost hands you the real
  // platform control, so you inherit the system caret, selection, IME, right-
  // click menu and accessibility for free.
  //
  // Everything on IDialogHost is asynchronous: showAsync returns immediately
  // and the callback fires later. Both the editor object and its callback must
  // therefore outlive this function - members, never locals.
  void BeginTextEdit()
  {
    if (!mDialogHost)
      return;

    const knob::Layout layout(mBounds);

    // Dialog-host factories hand back a bare IUnknown; queryInterface for the
    // interface you wanted. `as<T>()` is shorthand for exactly that.
    gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
    if (gmpi::ReturnCode::Ok != mDialogHost->createTextEdit(&layout.readoutRect, unknown.put()))
      return;

    mTextEdit = unknown.as<gmpi::api::ITextEdit>();
    if (!mTextEdit)
      return;

    mTextEdit->setText(mDisplay.c_str());
    mTextEdit->setTextSize(26.0f * layout.unit);
    mTextEdit->setAlignment(static_cast<int32_t>(gmpi::drawing::TextAlignment::Center) |
                            static_cast<int32_t>(gmpi::api::TextMultilineFlag::SingleLine));

    // The callback outlives showAsync, so it has to be a member. It is
    // GMPI_REFCOUNT_NO_DELETE, so pass its address rather than new'ing one.
    mTextEditCallback.onSuccess = [this](const std::string& text) {
      EndTextEdit();
      if (!text.empty())
        mHost.OnKnobTextEntered(text);
    };
    mTextEditCallback.onCancel = [this]() { EndTextEdit(); };

    mEditing = true;
    mHoverReadout = false;
    Invalidate();

    mTextEdit->showAsync(&mTextEditCallback);
  }

  void EndTextEdit()
  {
    mEditing = false;
    mTextEdit = {};
    Invalidate();
  }

  IKnobHost& mHost;
  gmpi::shared_ptr<gmpi::api::IDrawingHost> mDrawingHost;
  gmpi::shared_ptr<gmpi::api::IInputHost> mInputHost;
  gmpi::shared_ptr<gmpi::api::IDialogHost> mDialogHost;
  gmpi::shared_ptr<gmpi::api::ITextEdit> mTextEdit;
  gmpi::sdk::TextEditCallback mTextEditCallback;

  gmpi::drawing::Rect mBounds{};
  cachedBlur mGlow;
  double mValue{kDefaultValue};
  std::string mDisplay{"0.0 dB"};

  bool mEditing{};
  bool mHoverReadout{};
  bool mDragging{};
  float mDragStartY{};
  double mDragStartValue{};
};
