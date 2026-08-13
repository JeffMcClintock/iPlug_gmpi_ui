#include "GmpiPlugFrame.h"

#include "backends/DrawingFrameWin.h"

struct GmpiPlugFrame::Impl
{
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

  // attachClient before open: open() sizes and lays out whatever client is
  // attached, so a client added afterwards would have zero bounds until the
  // next resize.
  mImpl->frame.attachClient(pClient);

  const gmpi::drawing::SizeL size{width, height};
  mImpl->frame.open(pParent, &size);

  return mImpl->frame.getWindowHandle();
}

void GmpiPlugFrame::Close()
{
  mImpl->frame.close();

  // detachClient calls setHost(nullptr) on the view, so it stops trying to
  // invalidate a window that no longer exists.
  mImpl->frame.detachClient();
}
