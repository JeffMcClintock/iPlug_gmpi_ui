#include "GmpiPlugFrame.h"

// Brings in Direct2D and the Win32 hosting window. This TU is compiled with
// UNICODE defined (see CMakeLists.txt); nothing that includes it is.
#include "backends/DrawingFrameWin.h"

struct GmpiPlugFrame::Impl
{
  // Creates a child HWND, owns the Direct2D swap chain, runs a ~60Hz repaint
  // timer, and translates Win32 mouse messages into IInputClient calls.
  gmpi::hosting::DrawingFrame frame;
};

GmpiPlugFrame::GmpiPlugFrame()
: mImpl(std::make_unique<Impl>())
{
}

GmpiPlugFrame::~GmpiPlugFrame() = default;

void* GmpiPlugFrame::Open(void* pParent, gmpi::api::IUnknown* pClient, int width, int height)
{
  if (!pParent)
    return nullptr;

  // attachClient BEFORE open(). attachClient queryInterfaces the client for
  // IDrawingClient/IInputClient and calls setHost; open() then sizes and lays
  // out whatever is attached. Do it the other way round and the view sits at
  // zero size until something else triggers a re-layout.
  mImpl->frame.attachClient(pClient);

  // Passing an explicit size makes the child window exactly this big. Omit it
  // and the frame sizes itself to the parent's client rect instead - which is
  // not what you want here, because the host has not sized the parent yet.
  const gmpi::drawing::SizeL size{width, height};
  mImpl->frame.open(pParent, &size);

  return mImpl->frame.getWindowHandle();
}

void GmpiPlugFrame::Close()
{
  // Stops the timer and destroys the child window.
  mImpl->frame.close();

  // Then let the client go. detachClient calls setHost(nullptr) on the view,
  // which is its cue to drop its references to the frame and stop asking a
  // dead window to repaint.
  mImpl->frame.detachClient();
}
