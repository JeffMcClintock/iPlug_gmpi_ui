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

class IPlugGmpiGain final : public Plugin, public IKnobHost
{
public:
  IPlugGmpiGain(const InstanceInfo& info);
  ~IPlugGmpiGain();

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
#endif

  // IEditorDelegate - with NO_IGRAPHICS these are ours to implement.
  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  void OnParamChangeUI(int paramIdx, EParamSource source) override;

  // IKnobHost
  void OnKnobGestureBegin() override;
  void OnKnobValueChanged(double normalized) override;
  void OnKnobGestureEnd() override;
  void OnKnobTextEntered(const std::string& text) override;

private:
  void PushValueToView();

  GmpiPlugFrame mFrame;
  GainView* mView{};
};
