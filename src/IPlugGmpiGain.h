#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "GainView.h"
#include "GmpiPlugFrame.h"

using namespace iplug;

enum EParams
{
  kGain = 0,
  kNumParams
};

/*
================================ STEP 3 of 5 =================================
The plugin. An ordinary iPlug2 plugin in every respect except that it draws
itself with GMPI-UI, which comes down to three overrides:

  OpenWindow / CloseWindow    create and destroy the editor
  OnParamChangeUI             push parameter values into it

Deriving from IKnobHost as well is what closes the loop the other way: the view
calls us when the user moves something.

Note what is NOT here. No IGraphics, no IControl, no lambda building a UI in
the constructor, no resource loading. With UI NONE (see step 1) iPlug2's editor
side is just `void* OpenWindow(void*)`, and everything above that line is ours.
==============================================================================
*/
class IPlugGmpiGain final : public Plugin, public IKnobHost
{
public:
  IPlugGmpiGain(const InstanceInfo& info);
  ~IPlugGmpiGain();

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
#endif

  // --- iPlug2 -> us -----------------------------------------------------
  // The host is opening the editor and wants a native window handle back.
  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;

  // A parameter changed, on the UI thread. Fires for host automation, preset
  // loads and our own edits alike, which is why the view only ever needs this
  // one path to stay in sync.
  void OnParamChangeUI(int paramIdx, EParamSource source) override;

  // --- view -> us -------------------------------------------------------
  void OnKnobGestureBegin() override;
  void OnKnobValueChanged(double normalized) override;
  void OnKnobGestureEnd() override;
  void OnKnobTextEntered(const std::string& text) override;

private:
  void PushValueToView();

  GmpiPlugFrame mFrame;

  // Refcounted, so we hold a raw pointer and release() it rather than using
  // unique_ptr - the frame holds references of its own while attached.
  GainView* mView{};
};
