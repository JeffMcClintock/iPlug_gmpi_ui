#include "IPlugGmpiGain.h"
#include "IPlug_include_in_plug_src.h"

IPlugGmpiGain::IPlugGmpiGain(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 1))
{
  // -60..+12 dB, defaulting to unity. The parameter owns its units and its
  // formatting, which is what lets the view stay unit-agnostic.
  GetParam(kGain)->InitGain("Gain", 0.0, -60.0, 12.0, 0.1);
}

IPlugGmpiGain::~IPlugGmpiGain()
{
  // Belt and braces: hosts are supposed to call CloseWindow before destroying
  // the plugin, and most do. CloseWindow is idempotent, so this costs nothing
  // when they did.
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

// ===========================================================================
// Editor lifecycle
// ===========================================================================

void* IPlugGmpiGain::OpenWindow(void* pParent)
{
  if (!mView)
    mView = new GainView(*this);

  // Hand the view to the platform frame, which creates the native window and
  // wires it up. The cast picks one of the view's interfaces; the frame calls
  // queryInterface for the rest.
  void* handle = mFrame.Open(pParent, static_cast<gmpi::api::IDrawingClient*>(mView), PLUG_WIDTH, PLUG_HEIGHT);

  if (!handle)
  {
    mView->release();
    mView = nullptr;
    return nullptr;
  }

  // Seeds the editor with every current parameter value, by calling
  // OnParamChangeUI once per parameter. Without it a reopened editor shows
  // stale values until something happens to change one.
  OnUIOpen();

  // The host puts this handle in its window: an HWND on Windows, an NSView* on
  // macOS.
  return handle;
}

void IPlugGmpiGain::CloseWindow()
{
  mFrame.Close();

  if (mView)
  {
    mView->release(); // refcounted - see GMPI_REFCOUNT in GainView.h
    mView = nullptr;
  }

  IEditorDelegate::CloseWindow();
}

// ===========================================================================
// Parameters: iPlug2 -> view
// ===========================================================================

void IPlugGmpiGain::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (paramIdx == kGain)
    PushValueToView();
}

void IPlugGmpiGain::PushValueToView()
{
  // The editor may not exist: OnParamChangeUI fires whether or not the window
  // is open.
  if (!mView)
    return;

  WDL_String display;
  GetParam(kGain)->GetDisplayWithLabel(display);

  // The view wants 0..1 plus something to print. It never learns what a
  // decibel is.
  mView->SetValue(GetParam(kGain)->GetNormalized(), display.Get());
}

// ===========================================================================
// Parameters: view -> iPlug2
//
// Begin/End bracket the gesture so the host records a drag as one continuous
// automation move. Skipping them mostly works and then misbehaves in exactly
// the situation that is hardest to debug - writing automation in a live take.
// ===========================================================================

void IPlugGmpiGain::OnKnobGestureBegin()
{
  BeginInformHostOfParamChangeFromUI(kGain);
}

void IPlugGmpiGain::OnKnobValueChanged(double normalized)
{
  // Sets the parameter, tells the host, and calls OnParamChangeUI - so the
  // view is refreshed by the same path host automation uses. There is no
  // separate "update the UI after a UI edit" step, and no feedback loop.
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

  // What the user typed is not necessarily how the parameter formats itself
  // ("-6" becomes "-6.0 dB"), so push the canonical string back.
  PushValueToView();
}
