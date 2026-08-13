#include "IPlugGmpiGain.h"
#include "IPlug_include_in_plug_src.h"

IPlugGmpiGain::IPlugGmpiGain(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kGain)->InitGain("Gain", 0.0, -60.0, 12.0, 0.1);
}

IPlugGmpiGain::~IPlugGmpiGain()
{
  CloseWindow();
}

#if IPLUG_DSP
void IPlugGmpiGain::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const double gain = GetParam(kGain)->DBToAmp();
  const int nChans = NOutChansConnected();

  for (int c = 0; c < nChans; c++)
  {
    for (int s = 0; s < nFrames; s++)
      outputs[c][s] = inputs[c][s] * gain;
  }
}
#endif

void* IPlugGmpiGain::OpenWindow(void* pParent)
{
  if (!mView)
    mView = new GainView(*this);

  void* handle = mFrame.Open(pParent, static_cast<gmpi::api::IDrawingClient*>(mView), PLUG_WIDTH, PLUG_HEIGHT);

  if (!handle)
  {
    mView->release();
    mView = nullptr;
    return nullptr;
  }

  OnUIOpen(); // seeds the view with the current parameter values
  return handle;
}

void IPlugGmpiGain::CloseWindow()
{
  mFrame.Close();

  if (mView)
  {
    mView->release();
    mView = nullptr;
  }

  IEditorDelegate::CloseWindow();
}

void IPlugGmpiGain::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (paramIdx == kGain)
    PushValueToView();
}

void IPlugGmpiGain::PushValueToView()
{
  if (!mView)
    return;

  WDL_String display;
  GetParam(kGain)->GetDisplayWithLabel(display);

  mView->SetValue(GetParam(kGain)->GetNormalized(), display.Get());
}

void IPlugGmpiGain::OnKnobGestureBegin()
{
  BeginInformHostOfParamChangeFromUI(kGain);
}

void IPlugGmpiGain::OnKnobValueChanged(double normalized)
{
  SendParameterValueFromUI(kGain, normalized);
}

void IPlugGmpiGain::OnKnobGestureEnd()
{
  EndInformHostOfParamChangeFromUI(kGain);
}

void IPlugGmpiGain::OnKnobTextEntered(const std::string& text)
{
  const IParam* param = GetParam(kGain);

  // StringToValue understands the parameter's own units and clamps to range,
  // so "-6", "-6.5 dB" and "nonsense" all land somewhere sane.
  const double normalized = param->ToNormalized(param->StringToValue(text.c_str()));

  BeginInformHostOfParamChangeFromUI(kGain);
  SendParameterValueFromUI(kGain, normalized);
  EndInformHostOfParamChangeFromUI(kGain);

  // The typed text is not necessarily how the parameter formats itself, so
  // push the canonical display string back to the view.
  PushValueToView();
}
