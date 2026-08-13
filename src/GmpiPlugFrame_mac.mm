#include "GmpiPlugFrame.h"

#include "backends/DrawingFrameMac.h"

// Declared in DrawingFrameMac.mm, but not in the header.
void gmpi_onCloseNativeView(void* ptr);

struct GmpiPlugFrame::Impl
{
  void* view{};
};

GmpiPlugFrame::GmpiPlugFrame()
: mImpl(std::make_unique<Impl>())
{
}

GmpiPlugFrame::~GmpiPlugFrame() = default;

void* GmpiPlugFrame::Open(void* pParent, gmpi::api::IUnknown* pClient, int width, int height)
{
  // The second argument is gmpi_ui's "parameter host", which only SynthEdit-style
  // hosting uses. Parameters reach our view through iPlug2, so it stays null.
  mImpl->view = createNativeView(pParent, nullptr, (class IUnknown*) pClient, width, height);
  return mImpl->view;
}

void GmpiPlugFrame::Close()
{
  if (mImpl->view)
  {
    gmpi_onCloseNativeView(mImpl->view);
    mImpl->view = nullptr;
  }
}
