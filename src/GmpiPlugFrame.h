#pragma once

#include <memory>

namespace gmpi::api { struct IUnknown; }

/*
The whole Windows/macOS split of hosting a gmpi_ui view, behind one class.

iPlug2 hands us a parent window in IEditorDelegate::OpenWindow() and wants a
native handle back - HWND on Windows, NSView* on macOS. gmpi_ui provides both,
but through completely different types (a C++ DrawingFrame vs. an Objective-C
view built by a C function), so the plugin talks to this instead.
*/
class GmpiPlugFrame
{
public:
  GmpiPlugFrame();
  ~GmpiPlugFrame();

  // Returns the native handle for iPlug2 to give back to the host, or nullptr
  // on failure. `pParent` may be null: the AUv2 view factory creates the view
  // first and parents it afterwards.
  void* Open(void* pParent, gmpi::api::IUnknown* pClient, int width, int height);
  void Close();

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};
