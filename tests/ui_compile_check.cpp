/*
Builds the editor - and only the editor - with no plugin framework underneath.

iPlug2 does not support Linux yet (its own IPlug.cmake says so outright), so
the plugin cannot be built there. GainView, however, is portable by
construction: it is plain GMPI-UI, and GMPI-UI runs on Linux. This target keeps
that honest, and catches the usual portability slips - MSVC-only syntax, a
missing <cmath>, a narrowing conversion Clang rejects - in the one file most
likely to be lifted into another project.

render() is exercised by the compiler even though nothing here calls it:
GainView is not a template, so every member is compiled. What is NOT covered is
actually drawing anything, which needs a real device context.
*/

#include "GainView.h"

#include <cstdio>

namespace
{
struct NullKnobHost : IKnobHost
{
  void OnKnobGestureBegin() override {}
  void OnKnobValueChanged(double) override {}
  void OnKnobGestureEnd() override {}
  void OnKnobTextEntered(const std::string&) override {}
};
} // namespace

int main()
{
  NullKnobHost host;
  GainView view(host);

  // The lifecycle the frame would drive, minus render(). setHost(nullptr) is
  // legal and is what a real host calls at teardown.
  view.setHost(nullptr);

  const gmpi::drawing::Size available{360.0f, 340.0f};
  gmpi::drawing::Size desired{};
  view.measure(&available, &desired);

  const gmpi::drawing::Rect bounds{0.0f, 0.0f, available.width, available.height};
  view.arrange(&bounds);

  // Exercise the input path, which is pure arithmetic against the layout.
  view.onPointerMove({180.0f, 180.0f}, 0);
  view.SetValue(0.5, "-24.0 dB");

  gmpi::drawing::Rect clip{};
  view.getClipArea(&clip);

  std::printf("GainView compiled and laid out: %.0f x %.0f\n",
              clip.right - clip.left, clip.bottom - clip.top);
  return 0;
}
