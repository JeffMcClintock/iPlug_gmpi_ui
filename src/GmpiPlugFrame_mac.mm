#include "GmpiPlugFrame.h"

#include "backends/DrawingFrameMac.h"

// Defined in DrawingFrameMac.mm alongside createNativeView, but not declared
// in its header.
void gmpi_onCloseNativeView(void* ptr);

struct GmpiPlugFrame::Impl
{
  void* view{}; // an NSView*, kept opaque so this file needs no Cocoa types
};

GmpiPlugFrame::GmpiPlugFrame()
: mImpl(std::make_unique<Impl>())
{
}

GmpiPlugFrame::~GmpiPlugFrame() = default;

void* GmpiPlugFrame::Open(void* pParent, gmpi::api::IUnknown* pClient, int width, int height)
{
  // createNativeView builds an NSView subclass that owns the CoreGraphics
  // context, does the queryInterface/setHost dance on the client, and forwards
  // its mouse events - the Cocoa counterpart of DrawingFrame on Windows.
  //
  // A null parent is fine: the view is created unparented and the caller adds
  // it, which is what the AUv2 view factory does.
  //
  // The second argument is GMPI-UI's "parameter host", used only by SynthEdit-
  // style hosting where the frame talks to parameters directly. Ours go
  // through iPlug2, so it stays null.
  //
  // The cast is to the forward-declared global `IUnknown` that GMPI-UI's Cocoa
  // header uses to avoid dragging Objective-C into C++ headers; it is the same
  // pointer.
  mImpl->view = createNativeView(pParent, nullptr, (class IUnknown*) pClient, width, height);
  return mImpl->view;
}

void GmpiPlugFrame::Close()
{
  if (mImpl->view)
  {
    // Stops the view's timer and calls setHost(nullptr) on the client. The
    // view itself is released by the host that adopted it.
    gmpi_onCloseNativeView(mImpl->view);
    mImpl->view = nullptr;
  }
}
