#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Drawing.h"
#include "RefCountMacros.h"
#include "helpers/CachedBlur.h"
#include "helpers/NativeUi.h"

/*
The UI. Pure GMPI-UI - it knows nothing about iPlug2, and talks back through
IKnobHost in normalized (0..1) units.
*/
struct IKnobHost
{
  virtual void OnKnobGestureBegin() = 0;
  virtual void OnKnobValueChanged(double normalized) = 0;
  virtual void OnKnobGestureEnd() = 0;
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

// Everything is derived from the rect the host gave us, so the same code fits
// whatever DIP size the frame reports at any DPI.
struct Layout
{
  Point centre{};
  float arcRadius{};
  float bodyRadius{};
  float unit{}; // stroke widths and type sizes scale off this

  explicit Layout(Rect b)
  {
    const float w = b.right - b.left;
    const float h = b.bottom - b.top;

    arcRadius = std::min(w * 0.29f, h * 0.31f);
    bodyRadius = arcRadius * 0.635f;
    unit = arcRadius / 104.0f;
    centre = {(b.left + b.right) * 0.5f, b.top + h * 0.53f};
  }
};

inline Point onCircle(Point centre, float radius, float degrees)
{
  const float a = degrees * (pi / 180.0f);
  return {centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)};
}

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

    if (mDrawingHost)
      mDrawingHost->invalidateRect(nullptr);
  }

  // IDrawingClient / IInputClient (one override serves both)
  gmpi::ReturnCode setHost(gmpi::api::IUnknown* pHost) override
  {
    mDrawingHost = {};
    mInputHost = {};

    if (pHost)
    {
      pHost->queryInterface(&gmpi::api::IDrawingHost::guid, mDrawingHost.put_void());
      pHost->queryInterface(&gmpi::api::IInputHost::guid, mInputHost.put_void());
    }
    return gmpi::ReturnCode::Ok;
  }

  gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
  {
    *returnDesiredSize = *availableSize;
    return gmpi::ReturnCode::Ok;
  }

  gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
  {
    mBounds = *finalRect;
    mGlow.invalidate();
    return gmpi::ReturnCode::Ok;
  }

  gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
  {
    *returnRect = mBounds;
    return gmpi::ReturnCode::Ok;
  }

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

      // The halo: the same arc rendered into an offscreen mask, gaussian-blurred
      // and tinted (helpers/CachedBlur.h). The result is cached, so the blur only
      // re-runs when the value or the size actually changes.
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

      // The crisp arc on top of its own halo. This one carries the gradient.
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

      const float halfWidth = layout.bodyRadius * 0.98f;

      auto label = centred(10.5f * unit, FontWeight::SemiBold);
      g.drawTextU("G A I N", label,
                  {centre.x - halfWidth, centre.y - 42.0f * unit, centre.x + halfWidth, centre.y - 22.0f * unit},
                  dimBrush);

      auto readout = centred(31.0f * unit, FontWeight::SemiBold);
      g.drawTextU(mDisplay, readout,
                  {centre.x - halfWidth, centre.y - 22.0f * unit, centre.x + halfWidth, centre.y + 22.0f * unit},
                  brightBrush);

      auto title = centred(11.0f * unit, FontWeight::Medium);
      g.drawTextU("iPlug2  \xC2\xB7  GMPI-UI", title,
                  {mBounds.left, mBounds.top + 14.0f * unit, mBounds.right, mBounds.top + 34.0f * unit},
                  dimBrush);

      auto hint = centred(10.0f * unit, FontWeight::Regular);
      auto hintBrush = g.createSolidColorBrush(Color{textDim.r, textDim.g, textDim.b, 0.7f});
      g.drawTextU("drag to adjust  \xC2\xB7  shift = fine  \xC2\xB7  double-click = reset", hint,
                  {mBounds.left, mBounds.bottom - 32.0f * unit, mBounds.right, mBounds.bottom - 12.0f * unit},
                  hintBrush);
    }

    return gmpi::ReturnCode::Ok;
  }

  gmpi::ReturnCode hitTest(gmpi::drawing::Point, int32_t) override { return gmpi::ReturnCode::Ok; }
  gmpi::ReturnCode setHover(bool) override { return gmpi::ReturnCode::Ok; }

  gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
  {
    if (0 == (flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton)))
      return gmpi::ReturnCode::Unhandled;

    if (flags & static_cast<int32_t>(gmpi::api::PointerFlags::Double))
    {
      mHost.OnKnobGestureBegin();
      mHost.OnKnobValueChanged(kDefaultValue);
      mHost.OnKnobGestureEnd();
      return gmpi::ReturnCode::Handled;
    }

    mDragging = true;
    mDragStartY = point.y;
    mDragStartValue = mValue;

    if (mInputHost)
      mInputHost->setCapture();

    mHost.OnKnobGestureBegin();
    return gmpi::ReturnCode::Handled;
  }

  gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
  {
    if (!mDragging)
      return gmpi::ReturnCode::Unhandled;

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

  gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
  {
    *returnInterface = {};
    GMPI_QUERYINTERFACE(gmpi::api::IDrawingClient);
    GMPI_QUERYINTERFACE(gmpi::api::IInputClient);
    return gmpi::ReturnCode::NoSupport;
  }
  GMPI_REFCOUNT

private:
  // 0 dB on a -60..+12 dB scale.
  static constexpr double kDefaultValue = 60.0 / 72.0;

  IKnobHost& mHost;
  gmpi::shared_ptr<gmpi::api::IDrawingHost> mDrawingHost;
  gmpi::shared_ptr<gmpi::api::IInputHost> mInputHost;

  gmpi::drawing::Rect mBounds{};
  cachedBlur mGlow;
  double mValue{kDefaultValue};
  std::string mDisplay{"0.0 dB"};

  bool mDragging{};
  float mDragStartY{};
  double mDragStartValue{};
};
