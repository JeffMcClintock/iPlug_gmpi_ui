#pragma once

#define PLUG_NAME "IPlugGmpiGain"
#define PLUG_MFR "GMPI"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'Gmpg'
#define PLUG_MFR_ID 'GMPI'
#define PLUG_URL_STR "https://github.com/JeffMcClintock/iPlug_gmpi_ui"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Jeff McClintock"
#define PLUG_CLASS_NAME IPlugGmpiGain

#define BUNDLE_NAME "IPlugGmpiGain"
#define BUNDLE_MFR "GMPI"
#define BUNDLE_DOMAIN "com"

#define PLUG_CHANNEL_IO "2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 360
#define PLUG_HEIGHT 340
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY IPlugGmpiGain_Entry
#define AUV2_ENTRY_STR "IPlugGmpiGain_Entry"
#define AUV2_FACTORY IPlugGmpiGain_Factory
#define AUV2_VIEW_CLASS IPlugGmpiGain_View
#define AUV2_VIEW_CLASS_STR "IPlugGmpiGain_View"

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
