#pragma once

#include <memory>

namespace gmpi::api { struct IUnknown; }

/*
================================ STEP 2 of 5 =================================
The bridge. This is the only platform-specific code in the project, and the
only file you would need to think about when porting this integration to some
other plugin framework.

iPlug2 asks for a native window handle:

    void* OpenWindow(void* pParent);   // HWND on Windows, NSView* on macOS

GMPI-UI can produce exactly that, but through two unrelated APIs:

    Windows   gmpi::hosting::DrawingFrame  - a C++ class you own, which
              creates a child HWND and renders into it with Direct2D
    macOS     createNativeView()           - a C function returning an NSView*
              that renders with CoreGraphics

Rather than #ifdef at the call site, both hide behind Open/Close here, with one
implementation file each (GmpiPlugFrame_win.cpp, GmpiPlugFrame_mac.mm).

Note this header pulls in no platform headers at all - no <Windows.h>, no
Cocoa. That is what keeps the UNICODE build setting described in step 1 from
leaking into the rest of the plugin.
==============================================================================
*/
class GmpiPlugFrame
{
public:
  GmpiPlugFrame();
  ~GmpiPlugFrame();

  // Create the native window and attach `pClient` (your GMPI-UI editor) to it.
  //
  // Returns the handle to give back to the host, or nullptr on failure.
  // `pParent` may be null: the AUv2 view factory creates the view first and
  // parents it afterwards, which the macOS path handles.
  void* Open(void* pParent, gmpi::api::IUnknown* pClient, int width, int height);

  // Safe to call when never opened, and safe to call twice.
  void Close();

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};
