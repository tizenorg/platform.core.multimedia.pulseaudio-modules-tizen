/* audiofilters-sec-ladspa.c */

/*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************/

#ifdef USE_DLOG
#include <dlog.h>
#endif
#include <audiofilters/SoundAlive_play_wrapper.h>
#include "ladspa.h"

/*****************************************************************************/

/* Definitions for system */

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef int bool;

#ifdef USE_DLOG
#ifdef DLOG_TAG
#undef DLOG_TAG
#endif
#define DLOG_TAG "AUDIOFILTERS_SEC"
#define AUDIOFILTER_LOG_ERROR(...)    SLOG(LOG_ERROR, DLOG_TAG, __VA_ARGS__)
#define AUDIOFILTER_LOG_WARN(...)     SLOG(LOG_WARN,  DLOG_TAG, __VA_ARGS__)
#define AUDIOFILTER_LOG_INFO(...)     SLOG(LOG_INFO,  DLOG_TAG, __VA_ARGS__)
#define AUDIOFILTER_LOG_DEBUG(...)    SLOG(LOG_DEBUG, DLOG_TAG, __VA_ARGS__)
#define AUDIOFILTER_LOG_VERBOSE(...)  SLOG(LOG_DEBUG, DLOG_TAG, __VA_ARGS__)
#else
#define AUDIOFILTER_LOG_ERROR(...)    fprintf(stderr, __VA_ARGS__)
#define AUDIOFILTER_LOG_WARN(...)     fprintf(stderr, __VA_ARGS__)
#define AUDIOFILTER_LOG_INFO(...)     fprintf(stdout, __VA_ARGS__)
#define AUDIOFILTER_LOG_DEBUG(...)    fprintf(stdout, __VA_ARGS__)
#define AUDIOFILTER_LOG_VERBOSE(...)  fprintf(stdout, __VA_ARGS__)
#endif

#define AUDIOFILTER_RETURN_FALSE_IF_FAIL(expr) do { \
    if (!expr) { \
        AUDIOFILTER_LOG_ERROR("%s failed", #expr); \
        return FALSE; \
    } \
} while (0)

#define FILTER_BUFFER_SIZE  (64 * 1024)
#define FORMAT16BIT_MUL     (32768)
#define FORMAT16BIT_DIV     (1./32768.)

/* The port numbers for the plugin: */

#define FILTER_CONTROL_GAIN         0
#define FILTER_CONTROL_PRESET       1
#define FILTER_INPUT1               2
#define FILTER_OUTPUT1              3
#define FILTER_INPUT2               4
#define FILTER_OUTPUT2              5
#define FILTER_PORTS_MONO_MAX       4
#define FILTER_PORTS_STEREO_MAX     6

/* Definitions for SoundAlive */

#define DEFAULT_SAMPLE_SIZE         2
#define DEFAULT_SAMPLE_INT          4
#define DEFAULT_SAMPLE_SHORT        2
#define DEFAULT_VOLUME              0
#define DEFAULT_GAIN                1
#define DEFAULT_SAMPLE_RATE         48000

#define DEAFULT_CHANNELS            2
#define BITDEPTH_16                 16
#define BITDEPTH_24                 24

#define MAX_CHANNELS                6
#define DEFAULT_FILTER_ACTION       FILTER_NONE
#define DEFAULT_FILTER_OUTPUT_MODE  OUTPUT_SPK
#define DEFAULT_PRESET_MODE         PRESET_NORMAL
#define DEFAULT_SQUARE_ROW          2
#define DEFAULT_SQUARE_COL          2
#define SQUARE_MAX                  4
#define SQUARE_MIN                  0

#define SOUNDALIVE_KEY_MAX_LEN      256
#define SB_AM_CONF_BUFFER_SIZE      512

enum FilterActionType {
    FILTER_NONE,
    FILTER_PRESET,
    FILTER_CUSTOM,
    FILTER_SQUARE
};

enum SampleRate {
    SAMPLERATE_192000Hz,
    SAMPLERATE_176400Hz,
    SAMPLERATE_128000Hz,
    SAMPLERATE_96000Hz,
    SAMPLERATE_88200Hz,
    SAMPLERATE_64000Hz,
    SAMPLERATE_48000Hz,
    SAMPLERATE_44100Hz,
    SAMPLERATE_32000Hz,
    SAMPLERATE_24000Hz,
    SAMPLERATE_22050Hz,
    SAMPLERATE_16000Hz,
    SAMPLERATE_11025Hz,
    SAMPLERATE_8000Hz,
    SAMPLERATE_NUM
};

enum OutputMode {
    OUTPUT_SPK,
    OUTPUT_EAR,
    OUTPUT_OTHERS,  /* for MIRRORING devices */
    OUTPUT_BT,
    OUTPUT_DOCK,
    OUTPUT_MULTIMEDIA_DOCK,
    OUTPUT_USB_AUDIO,
    OUTPUT_HDMI,
    OUTPUT_NUM
};

typedef enum {
    DOCK_NONE      = 0,
    DOCK_DESKDOCK  = 1,
    DOCK_CARDOCK   = 2,
    DOCK_AUDIODOCK = 7,
    DOCK_SMARTDOCK = 8
} DOCK_STATUS;

enum PresetMode {
    PRESET_NORMAL,
    PRESET_POP,
    PRESET_ROCK,
    PRESET_DANCE,
    PRESET_JAZZ,
    PRESET_CLASSIC,
    PRESET_VOCAL,
    PRESET_BASS_BOOST,
    PRESET_TREBLE_BOOST,
    PRESET_MTHEATER,
    PRESET_EXTERNALIZATION,
    PRESET_CAFE,
    PRESET_CONCERT_HALL,
    PRESET_VOICE,
    PRESET_MOVIE,
    PRESET_VIRT51,
    PRESET_HIPHOP,
    PRESET_RNB,
    PRESET_FLAT,
    PRESET_TUBE,
    PRESET_VIRT71,
    PRESET_STUDIO,
    PRESET_CLUB,
    PRESET_NUM
};

enum {
    CUSTOM_EXT_3D_LEVEL,
    CUSTOM_EXT_BASS_LEVEL,
    CUSTOM_EXT_CLARITY_LEVEL,
    CUSTOM_EXT_PARAM_MAX
};

enum {
    AM_BAND_01,
    AM_BAND_02,
    AM_BAND_03,
    AM_BAND_NUM
};

#define CUSTOM_EQ_BAND_MAX 7
#define AM_FREQ_NUM 7
#define AM_COEF_NUM 6

/* Settings for the Sound Alive Effect Properties */

typedef struct _soundalive soundalive_t;

struct _soundalive {
    void *so_handle;
    unsigned int samplerate;
    unsigned int channels;
    unsigned int bitdepth;
    unsigned int bitwidth;
    SA_Play_Handle *soundalive_play_handle;

    bool sa_enable;
    bool high_latency;

    bool update_sa_filter;
    int square_row;
    int square_col;
    int preset_mode;
    int custom_eq[CUSTOM_EQ_BAND_MAX];
    int custom_ext[CUSTOM_EXT_PARAM_MAX];
    int current_volume;

#ifdef SOUNDALIVE_ENABLE_SB
    SB_Handle *sb_handle;
#endif

    int filter_action;
    int filter_output_mode;

    bool sb_onoff;
    short lowcut_freq;
    short gain_level;
    short clarity_level;
    bool am_onoff;
    bool am_band_onoff[AM_BAND_NUM];
#ifdef USE_SOUNDBOOSTER_VER4
    int am_band_coef[AM_BAND_NUM][AM_FREQ_NUM][AM_COEF_NUM];
#else
    int am_band_coef[AM_BAND_NUM][AM_FREQ_NUM * AM_COEF_NUM];
#endif

    int volume_type;

#ifdef USE_PA_AUDIO_FILTER
    bool use_pa_audio_filter;
#endif
};

/*****************************************************************************/

/* The structure used to hold port connection information and state
   (actually gain controls require no further state). */

typedef struct {

    /* Ports:
       ------ */

    LADSPA_Data  *m_pfControlGainValue;
    LADSPA_Data  *m_pfControlPresetValue;
    LADSPA_Data  *m_pfInputBuffer1;
    LADSPA_Data  *m_pfOutputBuffer1;
    LADSPA_Data  *m_pfInputBuffer2;  /* (Not used for mono) */
    LADSPA_Data  *m_pfOutputBuffer2; /* (Not used for mono) */

    LADSPA_Data  *m_pfFilterBuffer;  /* NOT A PORT : for format conversion purpose */
    soundalive_t *m_psSoundAlive;    /* NOT A PORT : for access SoundAlive parameters */

} AudioFilterSec;

LADSPA_Handle instantiateAudioFilterSec(const LADSPA_Descriptor *Descriptor, unsigned long SampleRate);
void connectPortToAudioFilterSec(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data *DataLocation);
void runMonoAudioFilterSec(LADSPA_Handle Instance, unsigned long SampleCount);
void runStereoAudioFilterSec(LADSPA_Handle Instance, unsigned long SampleCount);
void cleanupAudioFilterSec(LADSPA_Handle Instance);
void _init(void);
void deleteDescriptor(LADSPA_Descriptor *psDescriptor);
void _fini(void);

/*****************************************************************************/

/* Sound Alive filters */
static bool __effect_soundalive_init (AudioFilterSec *u) {
    soundalive_t *sa;

    AUDIOFILTER_LOG_INFO("%s: %d", __func__, __LINE__);

    /* SoundAlive Effect Field */
    if (u->m_psSoundAlive == NULL) {
        u->m_psSoundAlive = malloc(sizeof(struct _soundalive));
        if (u->m_psSoundAlive == NULL)
            return FALSE;
    }

    /* initialize SoundAlive properties */
    sa = u->m_psSoundAlive;
    sa->soundalive_play_handle = NULL;
    sa->current_volume = DEFAULT_VOLUME;
    sa->filter_action = DEFAULT_FILTER_ACTION;
    sa->filter_output_mode = DEFAULT_FILTER_OUTPUT_MODE;

    /* defined for the test should be set, properly. */
    sa->samplerate = DEFAULT_SAMPLE_RATE;
    sa->channels = DEAFULT_CHANNELS;
    sa->bitdepth = BITDEPTH_16;
    sa->preset_mode = -1;
    sa->sa_enable = TRUE;

    return TRUE;
}

static bool __effect_soundalive_create(AudioFilterSec *u) {
    soundalive_t *sa;
    SoundAlive_BitDepth_T bitdepth;
    SA_Channel chConf[MAX_CHANNELS];
    int in_channel;

    AUDIOFILTER_RETURN_FALSE_IF_FAIL(u->m_psSoundAlive);
    sa = u->m_psSoundAlive;

    if (sa->soundalive_play_handle) {
        SoundAlive_play_Reset(sa->soundalive_play_handle);
        sa->soundalive_play_handle = NULL;
    }

    chConf[0] = ChnL;
    chConf[1] = ChnR;
    in_channel = sa->channels;

    if (sa->bitdepth == BITDEPTH_16)
        bitdepth = SOUNDALIVE_BITDEPTH_16;
    else
        bitdepth = SOUNDALIVE_BITDEPTH_24;

    sa->soundalive_play_handle =
        SoundAlive_play_TrackStart( sa->samplerate, in_channel, chConf, bitdepth);

    AUDIOFILTER_LOG_INFO("m_psSoundAlive->soundalive_play_handle : 0x%x", (unsigned int)sa->soundalive_play_handle);

    return (sa->soundalive_play_handle == NULL ? FALSE : TRUE);
}

static bool __effect_soundalive_reset(AudioFilterSec *u) {
    soundalive_t *sa;

    AUDIOFILTER_RETURN_FALSE_IF_FAIL(u->m_psSoundAlive);
    sa = u->m_psSoundAlive;

    if (sa->soundalive_play_handle) {
        SoundAlive_play_Reset(sa->soundalive_play_handle);
        sa->soundalive_play_handle = NULL;
    }

    if (u->m_psSoundAlive)
        free(u->m_psSoundAlive);

    return TRUE;
}

static bool __effect_soundalive_set_preset(AudioFilterSec *u, int preset) {
    soundalive_t *sa;
    int filter_ret = 0;

    AUDIOFILTER_RETURN_FALSE_IF_FAIL(u->m_psSoundAlive);
    AUDIOFILTER_RETURN_FALSE_IF_FAIL((preset >= SA_NORMAL) && (preset < SA_PRESET_NUM));

    sa = u->m_psSoundAlive;

    if (sa->preset_mode != preset) {
        sa->preset_mode = preset;

        AUDIOFILTER_LOG_INFO("preset_mode: %d", sa->preset_mode);

        if (sa->soundalive_play_handle) {
            filter_ret = SoundAlive_play_Set_Preset(sa->soundalive_play_handle, sa->preset_mode);
            if (filter_ret < 0) {
                AUDIOFILTER_LOG_INFO("SoundAlive_play_Set_Preset() failed : %d", filter_ret);
                return FALSE;
            }
        }
    }

    return TRUE;
}

static bool __effect_soundalive_process_filter(AudioFilterSec *u, void *in_buf, void *out_buf, int size) {
    soundalive_t *sa;
    bool result = TRUE;
    int sa_ret = 0;
    int sample_n;
    int in_channel;

    AUDIOFILTER_RETURN_FALSE_IF_FAIL(u->m_psSoundAlive);

    sa = u->m_psSoundAlive;
    in_channel = sa->channels;

    if (!sa->soundalive_play_handle) {
        return TRUE;
    }

    if (sa->bitdepth == BITDEPTH_16) {
        sample_n = size / in_channel / DEFAULT_SAMPLE_SHORT;
    } else {
        sample_n = size / in_channel / DEFAULT_SAMPLE_INT;
    }

    sa_ret = SoundAlive_play(sa->soundalive_play_handle, out_buf, in_buf, sample_n, sa->current_volume);

    if (sa_ret < 0) {
        AUDIOFILTER_LOG_ERROR("SoundAlive_play failed : %d", sa_ret);
        result = FALSE;
    }

    return result;
}

/*****************************************************************************/

/* Construct a new plugin instance. */
LADSPA_Handle instantiateAudioFilterSec(const LADSPA_Descriptor *Descriptor, unsigned long SampleRate) {
    AudioFilterSec *psAudioFilterSec;

    LADSPA_Handle Instance = malloc(sizeof(AudioFilterSec));
    memset(Instance, 0, sizeof(AudioFilterSec));

    psAudioFilterSec = (AudioFilterSec *)Instance;
    psAudioFilterSec->m_pfFilterBuffer = (LADSPA_Data *) malloc(FILTER_BUFFER_SIZE * sizeof(char));
    memset(psAudioFilterSec->m_pfFilterBuffer, 0, FILTER_BUFFER_SIZE);

    if (__effect_soundalive_init(psAudioFilterSec)) {
        if (__effect_soundalive_create(psAudioFilterSec)) {
            /* set SA_NORMAL preset by default */
            if (__effect_soundalive_set_preset(psAudioFilterSec, 0)) {
                AUDIOFILTER_LOG_INFO("sa initialized ok.");
            }
        }
    }

    return Instance;
}

/*****************************************************************************/

/* Connect a port to a data location. */
void connectPortToAudioFilterSec(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data *DataLocation) {
    AudioFilterSec *psAudioFilterSec;

    psAudioFilterSec = (AudioFilterSec *)Instance;
    switch (Port) {
    case FILTER_CONTROL_GAIN:
        psAudioFilterSec->m_pfControlGainValue = DataLocation;
        break;
    case FILTER_CONTROL_PRESET:
        psAudioFilterSec->m_pfControlPresetValue = DataLocation;
        break;
    case FILTER_INPUT1:
        psAudioFilterSec->m_pfInputBuffer1 = DataLocation;
        break;
    case FILTER_OUTPUT1:
        psAudioFilterSec->m_pfOutputBuffer1 = DataLocation;
        break;
    case FILTER_INPUT2:
        /* (This should only happen for stereo.) */
        psAudioFilterSec->m_pfInputBuffer2 = DataLocation;
        break;
    case FILTER_OUTPUT2:
        /* (This should only happen for stereo.) */
        psAudioFilterSec->m_pfOutputBuffer2 = DataLocation;
        break;
    }
}

/*****************************************************************************/

void runMonoAudioFilterSec(LADSPA_Handle Instance, unsigned long SampleCount) {
    LADSPA_Data *pfInput;
    LADSPA_Data *pfOutput;
    LADSPA_Data fGain;
    LADSPA_Data fPreset;
    AudioFilterSec *psAudioFilterSec;
    unsigned long lSampleIndex, lSampleIndex2;
    short *psInput;
    short *psOutput;

    psAudioFilterSec = (AudioFilterSec *)Instance;

    fGain = *(psAudioFilterSec->m_pfControlGainValue);
    fPreset = *(psAudioFilterSec->m_pfControlPresetValue);
    __effect_soundalive_set_preset(psAudioFilterSec, (int)fPreset);

    /* convert flat -> short */
    pfInput = psAudioFilterSec->m_pfInputBuffer1;
    pfOutput = psAudioFilterSec->m_pfOutputBuffer1;

    psInput = (short *) psAudioFilterSec->m_pfFilterBuffer;

    /* convert float -> short(interleaved pcm) */
    for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++) {
        *(psInput++) = (short) (*(pfInput + lSampleIndex) * FORMAT16BIT_MUL);
        *(psInput++) = (short) (*(pfInput + lSampleIndex) * FORMAT16BIT_MUL);
    }
    psInput -= (SampleCount * 2);
    psOutput = psInput;

    if(psAudioFilterSec->m_psSoundAlive->sa_enable) {
        __effect_soundalive_process_filter(psAudioFilterSec, (void *) psInput, (void *) psOutput, SampleCount * sizeof(short) * 2);
    }

    /* convert short(interleaved pcm) -> float */
    for (lSampleIndex = 0, lSampleIndex2 = 0; lSampleIndex < SampleCount; lSampleIndex++) {
        *(pfOutput++) = (float) (*(psOutput + lSampleIndex2) * FORMAT16BIT_DIV * fGain);
        lSampleIndex2++;
        lSampleIndex2++;
    }
}

/*****************************************************************************/

void runStereoAudioFilterSec(LADSPA_Handle Instance, unsigned long SampleCount) {
    LADSPA_Data *pfInputL;
    LADSPA_Data *pfInputR;
    LADSPA_Data *pfOutputL;
    LADSPA_Data *pfOutputR;
    LADSPA_Data fGain;
    LADSPA_Data fPreset;
    AudioFilterSec *psAudioFilterSec;
    unsigned long lSampleIndex, lSampleIndex2;
    short *psInput;
    short *psOutput;

    psAudioFilterSec = (AudioFilterSec *)Instance;

    fGain = *(psAudioFilterSec->m_pfControlGainValue);
    fPreset = *(psAudioFilterSec->m_pfControlPresetValue);
    __effect_soundalive_set_preset(psAudioFilterSec, (int)fPreset);

    pfInputL = psAudioFilterSec->m_pfInputBuffer1;
    pfInputR = psAudioFilterSec->m_pfInputBuffer2;
    pfOutputL = psAudioFilterSec->m_pfOutputBuffer1;
    pfOutputR = psAudioFilterSec->m_pfOutputBuffer2;

    psInput = (short *) psAudioFilterSec->m_pfFilterBuffer;

    /* convert float -> short(interleaved pcm) */
    for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++) {
        *(psInput++) = (short) (*(pfInputL + lSampleIndex) * FORMAT16BIT_MUL);
        *(psInput++) = (short) (*(pfInputR + lSampleIndex) * FORMAT16BIT_MUL);
    }
    psInput -= (SampleCount * 2);
    psOutput = psInput;

    if(psAudioFilterSec->m_psSoundAlive->sa_enable) {
        __effect_soundalive_process_filter(psAudioFilterSec, (void *) psInput, (void *) psOutput, SampleCount * sizeof(short) * 2);
    }

    /* convert short(interleaved pcm) -> float */
    for (lSampleIndex = 0, lSampleIndex2 = 0; lSampleIndex < SampleCount; lSampleIndex++) {
        *(pfOutputL++) = (float) (*(psOutput + lSampleIndex2) * FORMAT16BIT_DIV * fGain);
        lSampleIndex2++;
        *(pfOutputR++) = (float) (*(psOutput + lSampleIndex2) * FORMAT16BIT_DIV * fGain);
        lSampleIndex2++;
    }
}

/*****************************************************************************/

/* Throw away a simple delay line. */
void cleanupAudioFilterSec(LADSPA_Handle Instance) {
    AudioFilterSec *psAudioFilterSec = (AudioFilterSec *)Instance;

    __effect_soundalive_reset(psAudioFilterSec);
    if (psAudioFilterSec->m_pfFilterBuffer)
        free(psAudioFilterSec->m_pfFilterBuffer);
    free(Instance);
}

/*****************************************************************************/

LADSPA_Descriptor *g_psMonoDescriptor = NULL;
LADSPA_Descriptor *g_psStereoDescriptor = NULL;

/*****************************************************************************/

/* _init() is called automatically when the plugin library is first loaded. */
void _init() {
    char **pcPortNames;
    LADSPA_PortDescriptor *piPortDescriptors;
    LADSPA_PortRangeHint *psPortRangeHints;

    g_psMonoDescriptor
        = (LADSPA_Descriptor *) malloc(sizeof(LADSPA_Descriptor));
    g_psStereoDescriptor 
        = (LADSPA_Descriptor *) malloc(sizeof(LADSPA_Descriptor));

    /* for MONO */
    if (g_psMonoDescriptor) {
        g_psMonoDescriptor->UniqueID
            = 10000;
        g_psMonoDescriptor->Label
            = strdup("audiofiltersec_mono");
        g_psMonoDescriptor->Properties
            = LADSPA_PROPERTY_HARD_RT_CAPABLE;
        g_psMonoDescriptor->Name 
            = strdup("Mono AudioFilterSec");
        g_psMonoDescriptor->Maker
            = strdup("Samsung Electronics (LADSPA plugins)");
        g_psMonoDescriptor->Copyright
            = strdup("None");
        g_psMonoDescriptor->PortCount
            = FILTER_PORTS_MONO_MAX;
        piPortDescriptors
            = (LADSPA_PortDescriptor *) calloc(FILTER_PORTS_MONO_MAX, sizeof(LADSPA_PortDescriptor));
        g_psMonoDescriptor->PortDescriptors
            = (const LADSPA_PortDescriptor *)piPortDescriptors;
        piPortDescriptors[FILTER_CONTROL_GAIN]
            = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[FILTER_CONTROL_PRESET]
            = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[FILTER_INPUT1]
            = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[FILTER_OUTPUT1]
            = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        pcPortNames
            = (char **) calloc(FILTER_PORTS_MONO_MAX, sizeof(char *));
        g_psMonoDescriptor->PortNames 
            = (const char **)pcPortNames;
        pcPortNames[FILTER_CONTROL_GAIN]
            = strdup("Gain");
        pcPortNames[FILTER_CONTROL_PRESET]
            = strdup("Preset");
        pcPortNames[FILTER_INPUT1]
            = strdup("Input");
        pcPortNames[FILTER_OUTPUT1]
            = strdup("Output");
        psPortRangeHints = ((LADSPA_PortRangeHint *) calloc(FILTER_PORTS_MONO_MAX, sizeof(LADSPA_PortRangeHint)));
        g_psMonoDescriptor->PortRangeHints
            = (const LADSPA_PortRangeHint *)psPortRangeHints;
        psPortRangeHints[FILTER_CONTROL_GAIN].HintDescriptor
            = (LADSPA_HINT_BOUNDED_BELOW 
             | LADSPA_HINT_LOGARITHMIC
             | LADSPA_HINT_DEFAULT_1);
        psPortRangeHints[FILTER_CONTROL_GAIN].LowerBound 
            = 0;
        psPortRangeHints[FILTER_CONTROL_PRESET].HintDescriptor
            = (LADSPA_HINT_INTEGER
             | LADSPA_HINT_DEFAULT_0);
        psPortRangeHints[FILTER_INPUT1].HintDescriptor
            = 0;
        psPortRangeHints[FILTER_OUTPUT1].HintDescriptor
            = 0;
        g_psMonoDescriptor->instantiate 
            = instantiateAudioFilterSec;
        g_psMonoDescriptor->connect_port 
            = connectPortToAudioFilterSec;
        g_psMonoDescriptor->activate
            = NULL;
        g_psMonoDescriptor->run
            = runMonoAudioFilterSec;
        g_psMonoDescriptor->run_adding
            = NULL;
        g_psMonoDescriptor->set_run_adding_gain
            = NULL;
        g_psMonoDescriptor->deactivate
            = NULL;
        g_psMonoDescriptor->cleanup
            = cleanupAudioFilterSec;
    }

    /* for STEREO */
    if (g_psStereoDescriptor) {
        g_psStereoDescriptor->UniqueID
            = 10001;
        g_psStereoDescriptor->Label
            = strdup("audiofiltersec_stereo");
        g_psStereoDescriptor->Properties
            = LADSPA_PROPERTY_HARD_RT_CAPABLE;
        g_psStereoDescriptor->Name 
            = strdup("Stereo AudioFilterSec");
        g_psStereoDescriptor->Maker
            = strdup("Samsung Electronics (LADSPA plugins)");
        g_psStereoDescriptor->Copyright
            = strdup("None");
        g_psStereoDescriptor->PortCount
            = FILTER_PORTS_STEREO_MAX;
        piPortDescriptors
            = (LADSPA_PortDescriptor *) calloc(FILTER_PORTS_STEREO_MAX, sizeof(LADSPA_PortDescriptor));
        g_psStereoDescriptor->PortDescriptors
            = (const LADSPA_PortDescriptor *)piPortDescriptors;
        piPortDescriptors[FILTER_CONTROL_GAIN]
            = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[FILTER_CONTROL_PRESET]
            = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
        piPortDescriptors[FILTER_INPUT1]
            = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[FILTER_OUTPUT1]
            = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[FILTER_INPUT2]
            = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
        piPortDescriptors[FILTER_OUTPUT2]
            = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
        pcPortNames
            = (char **) calloc(FILTER_PORTS_STEREO_MAX, sizeof(char *));
        g_psStereoDescriptor->PortNames 
            = (const char **)pcPortNames;
        pcPortNames[FILTER_CONTROL_GAIN]
            = strdup("Gain");
        pcPortNames[FILTER_CONTROL_PRESET]
            = strdup("Preset");
        pcPortNames[FILTER_INPUT1]
            = strdup("Input (Left)");
        pcPortNames[FILTER_OUTPUT1]
            = strdup("Output (Left)");
        pcPortNames[FILTER_INPUT2]
            = strdup("Input (Right)");
        pcPortNames[FILTER_OUTPUT2]
            = strdup("Output (Right)");
        psPortRangeHints = ((LADSPA_PortRangeHint *) calloc(FILTER_PORTS_STEREO_MAX, sizeof(LADSPA_PortRangeHint)));
        g_psStereoDescriptor->PortRangeHints
            = (const LADSPA_PortRangeHint *) psPortRangeHints;
        psPortRangeHints[FILTER_CONTROL_GAIN].HintDescriptor
            = (LADSPA_HINT_BOUNDED_BELOW 
             | LADSPA_HINT_LOGARITHMIC
             | LADSPA_HINT_DEFAULT_1);
        psPortRangeHints[FILTER_CONTROL_GAIN].LowerBound 
            = 0;
        psPortRangeHints[FILTER_CONTROL_PRESET].HintDescriptor
            = (LADSPA_HINT_INTEGER
             | LADSPA_HINT_DEFAULT_0);
        psPortRangeHints[FILTER_INPUT1].HintDescriptor
            = 0;
        psPortRangeHints[FILTER_OUTPUT1].HintDescriptor
            = 0;
        psPortRangeHints[FILTER_INPUT2].HintDescriptor
            = 0;
        psPortRangeHints[FILTER_OUTPUT2].HintDescriptor
            = 0;
        g_psStereoDescriptor->instantiate 
            = instantiateAudioFilterSec;
        g_psStereoDescriptor->connect_port 
            = connectPortToAudioFilterSec;
        g_psStereoDescriptor->activate
            = NULL;
        g_psStereoDescriptor->run
            = runStereoAudioFilterSec;
        g_psStereoDescriptor->run_adding
            = NULL;
        g_psStereoDescriptor->set_run_adding_gain
            = NULL;
        g_psStereoDescriptor->deactivate
            = NULL;
        g_psStereoDescriptor->cleanup
            = cleanupAudioFilterSec;
    }
}

/*****************************************************************************/

void deleteDescriptor(LADSPA_Descriptor *psDescriptor) {
    unsigned long lIndex;
    if (psDescriptor) {
        free((char *)psDescriptor->Label);
        free((char *)psDescriptor->Name);
        free((char *)psDescriptor->Maker);
        free((char *)psDescriptor->Copyright);
        free((LADSPA_PortDescriptor *)psDescriptor->PortDescriptors);
        for (lIndex = 0; lIndex < psDescriptor->PortCount; lIndex++)
            free((char *)(psDescriptor->PortNames[lIndex]));
        free((char **)psDescriptor->PortNames);
        free((LADSPA_PortRangeHint *)psDescriptor->PortRangeHints);
        free(psDescriptor);
    }
}

/*****************************************************************************/

/* _fini() is called automatically when the library is unloaded. */
void _fini() {
    deleteDescriptor(g_psMonoDescriptor);
    deleteDescriptor(g_psStereoDescriptor);
}

/*****************************************************************************/

/* Return a descriptor of the requested plugin type. There are two
   plugin types available in this library (mono and stereo). */
const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index) {
    /* Return the requested descriptor or null if the index is out of range. */
    switch (Index) {
    case 0:
        return g_psMonoDescriptor;
    case 1:
        return g_psStereoDescriptor;
    default:
        return NULL;
    }
}

/*****************************************************************************/

/* EOF */
