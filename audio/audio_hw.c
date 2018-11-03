/*
 * Copyright (C) 2012 The Android Open Source Project
 * Author: Suryandaru Triandana <syndtr@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

#include "ril_interface.h"

typedef enum {
    AUDIO_FLAG_MODE_NORMAL              = (1 << AUDIO_MODE_NORMAL),
    AUDIO_FLAG_MODE_RINGTONE            = (1 << AUDIO_MODE_RINGTONE),
    AUDIO_FLAG_MODE_IN_CALL             = (1 << AUDIO_MODE_IN_CALL),
    AUDIO_FLAG_MODE_IN_COMMUNICATION    = (1 << AUDIO_MODE_IN_COMMUNICATION),
    AUDIO_FLAG_MODE_ALL                 = (AUDIO_FLAG_MODE_NORMAL |
                                           AUDIO_FLAG_MODE_RINGTONE |
                                           AUDIO_FLAG_MODE_IN_CALL |
                                           AUDIO_FLAG_MODE_IN_COMMUNICATION),

} audio_flag_mode_t;

/* ALSA cards for QSS */
#define CARD_QSS_DEFAULT 0

/* ALSA ports for QSS */
#define PORT_HIFI 0
#define PORT_MODEM 1

/* number of frames per period */
#define PLAYBACK_PERIOD_SIZE (1024 * 2)
/* number of periods for playback */
#define PLAYBACK_PERIOD_COUNT 4
/* number of frames per period */
#define CAPTURE_PERIOD_SIZE 1024
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 4
/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (PLAYBACK_PERIOD_SIZE * 2)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

#define DEFAULT_SAMPLING_RATE 44100
#define VX_SAMPLING_RATE 8000

struct pcm_config pcm_config_hifi = {
    .channels = 2,
    .rate = DEFAULT_SAMPLING_RATE,
    .period_size = PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = PLAYBACK_PERIOD_SIZE * 2,
    .avail_min = PLAYBACK_PERIOD_SIZE,
};

struct pcm_config pcm_config_hifi_ul = {
    .channels = 2,
    .rate = DEFAULT_SAMPLING_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_vx = {
    .channels = 2,
    .rate = VX_SAMPLING_RATE,
    .period_size = 160,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};

struct qss_mixer_cfg {
    char *name;
    audio_devices_t devices;
    audio_devices_t and_devices_not;
    audio_flag_mode_t mode;
    audio_flag_mode_t keep; /* keep value when off */
    bool onetime;
    char *strval;
    int intval;

    struct mixer_ctl *ctl;
    int state;
    struct qss_mixer_cfg *pnext;
};

struct qss_mixer_cfg out_mixer_cfgs[] = {
    /* DAC1 */
    {
        .name    = "DAC1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "DAC1R Mixer AIF1.1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
    }, {
        .name    = "DAC1L Mixer AIF1.1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
    },

    /* AIF2DAC - Modem */
    {
#if 0
        .name    = "AIF2DAC Mono Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
        .onetime = true,
    }, {
#endif
        .name    = "AIF2DAC Volume",
        .intval  = 90,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
        .onetime = true,
    }, {
        .name    = "DAC1R Mixer AIF2 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "DAC1L Mixer AIF2 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* Mixer */
    {
        .name    = "Right Output Mixer DAC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "Left Output Mixer DAC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* Sidetone */
    {
        .name    = "DAC1 Right Sidetone Volume",
        .intval  = 4,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .onetime = true,
    }, {
        .name    = "DAC1 Left Sidetone Volume",
        .intval  = 4,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .onetime = true,
    }, {
        .name    = "DAC1R Mixer Right Sidetone Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "DAC1L Mixer Left Sidetone Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* Headphone */
    {
        .name    = "Right Headphone Mux",
        .strval  = "DAC",
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_WIRED_HEADSET |
                   AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
        .onetime = true,
    }, {
        .name    = "Left Headphone Mux",
        .strval  = "DAC",
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_WIRED_HEADSET |
                   AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
        .onetime = true,
    }, {
        .name    = "Headphone Volume",
        .intval  = 43,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_WIRED_HEADSET |
                   AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
        .onetime = true,
    }, {
        .name    = "Headphone Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_WIRED_HEADSET |
                   AUDIO_DEVICE_OUT_WIRED_HEADPHONE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* Speaker */
    {
        .name    = "SPKR DAC1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "SPKL DAC1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "Speaker Mixer Volume",
        .intval  = 3,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .onetime = true,
    }, {
        .name    = "Speaker Boost Volume",
        .intval  = 6,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .onetime = true,
    }, {
        .name    = "Speaker Volume",
        .intval  = 63,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .onetime = true,
    }, {
        .name    = "Speaker Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* Earpiece */
    {
        .name    = "Earpiece Mixer Right Output Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "Earpiece Mixer Left Output Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "Earpiece Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },
};

struct qss_mixer_cfg in_mixer_cfgs[] = {
    /* Main mic */
    {
        .name    = "IN1L Volume",
        .intval  = 31,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "IN1L ZC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "IN1L Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "IN1L PGA IN1LP Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "IN1L PGA IN1LN Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "MIXINL IN1L Volume",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "MIXINL IN1L Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL |
                   AUDIO_FLAG_MODE_IN_COMMUNICATION,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_SPEAKER,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* 2nd mic */
    {
        .name    = "IN2L Volume",
        .intval  = 31,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "IN2L ZC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "IN2L Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "IN2L PGA IN2LP Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "IN2L PGA IN2LN Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "MIXINL IN2L Volume",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .onetime = true,
    }, {
        .name    = "MIXINL IN2L Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
        .and_devices_not = AUDIO_DEVICE_OUT_EARPIECE,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },

    /* ADC1 */
    {
        .name    = "AIF1ADC1R Mixer ADC/DMIC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_ALL,
    }, {
        .name    = "AIF1ADC1L Mixer ADC/DMIC Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_ALL,
        .devices = AUDIO_DEVICE_IN_ALL,
    },
    
    /* AIF2DAC2 - Modem */
    {
        .name    = "AIF2DAC2R Mixer AIF1.1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_IN_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "AIF2DAC2L Mixer AIF1.1 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_IN_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    }, {
        .name    = "DAC2 Switch",
        .intval  = 1,
        .mode    = AUDIO_FLAG_MODE_IN_CALL,
        .devices = AUDIO_DEVICE_IN_ALL,
        .keep    = AUDIO_FLAG_MODE_IN_CALL,
    },
};

struct qss_stream_out;
struct qss_stream_in;

struct qss_audio_device {
    struct audio_hw_device device;

    pthread_mutex_t lock;
    struct mixer *mixer;
    struct {
        struct qss_mixer_cfg *in;
        unsigned int in_nr;
        struct qss_mixer_cfg *out;
        unsigned int out_nr;
    } mixer_cfg;
    audio_mode_t mode;
    audio_devices_t devices;
    struct qss_stream_out *active_output;
    struct qss_stream_in *active_input;
    bool mic_mute;
    bool in_call;
    float voice_volume;
    bool bluetooth_nrec;
    struct pcm *pcm_modem_dl;
    struct pcm *pcm_modem_ul;

    /* RIL */
    struct ril_handle ril;
};

struct qss_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;
    struct qss_audio_device *dev;
    struct pcm_config config;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    bool standby;
    int write_threshold;
};

struct qss_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;
    struct qss_audio_device *dev;
    struct pcm_config config;
    struct pcm *pcm;
    unsigned int requested_rate;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    int read_status;
    bool standby;
};

inline void snd_mixer_apply_cfg(struct qss_mixer_cfg *cfg)
{
    int j, n = mixer_ctl_get_num_values(cfg->ctl);
    for (j = 0; j < n; j++)
        mixer_ctl_set_value(cfg->ctl, j, cfg->state ? cfg->intval : 0);
}

static void snd_mixer_apply(struct mixer *mixer, struct qss_mixer_cfg *cfgs,
                        int cfgs_nr, audio_mode_t mode, audio_devices_t devices,
                        bool on, bool reset)
{
    struct qss_mixer_cfg *cfg;
    const char *string;
    int i = 0, j, n, state = 0;
    int keep, mode_flag = (1 << mode);
    struct qss_mixer_cfg *pnext = NULL;

    for (i = cfgs_nr - 1; i >= 0; i--) {
        cfg = &cfgs[i];
        keep = (cfg->keep & mode_flag);

        /* check state */
        if (!reset)
            state = ((on || keep || cfg->onetime) && (cfg->mode & mode_flag) &&
                     (cfg->devices & devices) && (!cfg->and_devices_not ||
                     (cfg->and_devices_not & devices) == 0));

        if (cfg->onetime && !state)
            continue;

        if (!reset && (cfg->state == state || (cfg->state && cfg->onetime)))
            continue;

        /* get ctl */
        if (!cfg->ctl) {
            cfg->ctl = mixer_get_ctl_by_name(mixer, cfg->name);
            if (!cfg->ctl) {
                ALOGE("unable to get mixer ctl '%s'", cfg->name);
                continue;
            }
        }

        /* lookup enum intval */
        if (cfg->strval) {
            if (mixer_ctl_get_type(cfg->ctl) != MIXER_CTL_TYPE_ENUM) {
                ALOGE("mixer ctl '%s' is not enum", cfg->name);
                continue;
            }
            n = mixer_ctl_get_num_enums(cfg->ctl);
            for (j = 0; j < n; j++) {
                string = mixer_ctl_get_enum_string(cfg->ctl, j);
                if (!strcmp(string, cfg->strval)) {
                    cfg->intval = j;
                    cfg->strval = 0;
                    break;
                }
            }
            if (cfg->strval) {
                ALOGE("mixer ctl '%s' invalid enum '%s'", cfg->name, cfg->strval);
                continue;
            }
        }

        ALOGD("apply mixer '%s' state=%d(%d) devices=%d(%d) devices_masked=%d", cfg->name, state, cfg->state,
            cfg->devices, devices, cfg->devices & devices);
        cfg->state = state;
        if (cfg->state) {
            cfg->pnext = pnext;
            pnext = cfg;
            ALOGD("DEFERED --");
        } else {
            snd_mixer_apply_cfg(cfg);
            cfg->pnext = NULL;
        }
    }

    while(pnext) {
        ALOGD("apply defered mixer '%s' state=%d", pnext->name, pnext->state);
        snd_mixer_apply_cfg(pnext);
        pnext = pnext->pnext;
    }
}

static void set_incall_device(struct qss_audio_device *adev)
{
    int device_type;

    switch(adev->devices & AUDIO_DEVICE_OUT_ALL) {
        case AUDIO_DEVICE_OUT_EARPIECE:
            device_type = SOUND_AUDIO_PATH_HANDSET;
            break;
        case AUDIO_DEVICE_OUT_SPEAKER:
        case AUDIO_DEVICE_OUT_AUX_DIGITAL:
            device_type = SOUND_AUDIO_PATH_SPEAKER;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
            device_type = SOUND_AUDIO_PATH_HEADSET;
            break;
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
            device_type = SOUND_AUDIO_PATH_HEADPHONE;
            break;
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
            if (adev->bluetooth_nrec)
                device_type = SOUND_AUDIO_PATH_BLUETOOTH;
            else
                device_type = SOUND_AUDIO_PATH_BLUETOOTH_NO_NR;
            break;
        default:
            device_type = SOUND_AUDIO_PATH_HANDSET;
            break;
    }

    /* if output device isn't supported, open modem side to handset by default */
    ril_set_call_audio_path(&adev->ril, device_type);
}

static void out_do_set_route(struct qss_audio_device *adev, bool on)
{
    ALOGD("%s: BEGIN, on=%d, devices=%x\n", __func__, on, adev->devices);
    snd_mixer_apply(adev->mixer, adev->mixer_cfg.out, adev->mixer_cfg.out_nr, adev->mode,
                    adev->devices, on, false);
}

static void out_set_route(struct qss_stream_out *out)
{
    out_do_set_route(out->dev, !out->standby);
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return DEFAULT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;
    size_t size = (PLAYBACK_PERIOD_SIZE * DEFAULT_SAMPLING_RATE) / out->config.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct qss_stream_out *out)
{
    struct qss_audio_device *adev = out->dev;

    ALOGD("%s: BEGIN cur=%x\n", __func__, out->standby);
    if (!out->standby) {
        pcm_close(out->pcm);
        out_do_set_route(adev, false);
        out->pcm = NULL;

        out->standby = true;
    }

    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;
    int status;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;
    struct qss_audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;

    ALOGD("%s: kvpairs %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        ALOGD("%s: set route to=%x cur=%x", __func__, val, (adev->devices & AUDIO_DEVICE_OUT_ALL));
        if (((adev->devices & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->devices |= val;
            pthread_mutex_lock(&out->lock);
            out_set_route(out);
            set_incall_device(adev);
            pthread_mutex_unlock(&out->lock);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    str_parms_destroy(parms);

    return 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;

    return (PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct qss_stream_out *out)
{
    struct qss_audio_device *adev = out->dev;

    ALOGD("%s: BEGIN\n", __func__);
    
    out->pcm = pcm_open(CARD_QSS_DEFAULT, PORT_HIFI, PCM_OUT | PCM_MMAP | PCM_NOIRQ,
                        &out->config);

    if (!pcm_is_ready(out->pcm)) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        return -ENOMEM;
    }

    out->resampler->reset(out->resampler);

    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;
    struct qss_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    int kernel_frames;
    void *buf;
    int ret;

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        out_do_set_route(adev, true);
        ret = start_output_stream(out);
        if (ret != 0) {
            out_do_set_route(adev, false);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = false;
    }
    pthread_mutex_unlock(&adev->lock);

    /* only use resampler if required */
    if (out->config.rate != DEFAULT_SAMPLING_RATE) {
        out->resampler->resample_from_input(out->resampler,
                                            (int16_t *)buffer,
                                            &in_frames,
                                            (int16_t *)out->buffer,
                                            &out_frames);
        buf = out->buffer;
    } else {
        out_frames = in_frames;
        buf = (void *)buffer;
    }

    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */
    do {
        struct timespec time_stamp;

        if (pcm_get_htimestamp(out->pcm, (unsigned int *)&kernel_frames, &time_stamp) < 0)
            break;
        kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

        if (kernel_frames > out->write_threshold) {
            unsigned long time = (unsigned long)
                    (((int64_t)(kernel_frames - out->write_threshold) * 1000000) /
                            DEFAULT_SAMPLING_RATE);
            if (time < MIN_WRITE_SLEEP_US)
                time = MIN_WRITE_SLEEP_US;
            usleep(time);
        }
    } while (kernel_frames > out->write_threshold);

    ret = pcm_mmap_write(out->pcm, (void *)buf, out_frames * frame_size);

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret)
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/** audio_stream_in implementation **/
static int check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;
    size_t device_rate;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_hifi_ul.period_size * sample_rate) / pcm_config_hifi_ul.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}


static void in_do_set_route(struct qss_audio_device *adev, bool on)
{
    ALOGD("%s: BEGIN, on=%d, devices=%x\n", __func__, on, adev->devices);
    snd_mixer_apply(adev->mixer, adev->mixer_cfg.in, adev->mixer_cfg.in_nr, adev->mode,
                    adev->devices, on, false);
}

static void in_set_route(struct qss_stream_in *in)
{
    in_do_set_route(in->dev, !in->standby);
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;

    return get_input_buffer_size(in->requested_rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->config.channels);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;

    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct qss_stream_in *in)
{
    struct qss_audio_device *adev = in->dev;

    ALOGD("%s: BEGIN, cur=%d\n", __func__, in->standby);
    if (!in->standby) {
        pcm_close(in->pcm);
        in_do_set_route(adev, false);
        in->pcm = NULL;

        in->standby = true;
    }

    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;
    int status;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;
    struct qss_audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;

    ALOGD("%s: kvpairs %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        ALOGD("%s: set route to=%x cur=%x", __func__, val, (adev->devices & AUDIO_DEVICE_IN_ALL));
        if (((adev->devices & AUDIO_DEVICE_IN_ALL) != val) && (val != 0)) {
            adev->devices &= ~AUDIO_DEVICE_IN_ALL;
            adev->devices |= val;
            pthread_mutex_lock(&in->lock);
            in_set_route(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    str_parms_destroy(parms);

    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct qss_stream_in *in)
{
    struct qss_audio_device *adev = in->dev;

    in->pcm = pcm_open(CARD_QSS_DEFAULT, PORT_HIFI, PCM_IN, &in->config);

    ALOGD("%s: BEGIN\n", __func__);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }

    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }

    return 0;
}

static int in_get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct qss_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct qss_stream_in *)((char *)buffer_provider -
                                   offsetof(struct qss_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm,
                                   (void*)in->buffer,
                                   in->config.period_size *
                                       audio_stream_frame_size(&in->stream.common));
        if (in->read_status != 0) {
            ALOGE("in_get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                                                in->config.channels;

    return in->read_status;

}

static void in_release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct qss_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct qss_stream_in *)((char *)buffer_provider -
                                   offsetof(struct qss_stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

/* in_read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t in_read_frames(struct qss_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_frame_size(&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { raw : NULL, },
                    frame_count : frames_rd,
            };
            in_get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_frame_size(&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size(&in->stream.common));
                frames_rd = buf.frame_count;
            }
            in_release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;
    struct qss_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);
    int ret = 0;

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        in_do_set_route(adev, true);
        ret = start_input_stream(in);
        if (ret != 0) {
            in_do_set_route(adev, false);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        in->standby = false;
    }
    pthread_mutex_unlock(&adev->lock);

    if (in->resampler)
        ret = in_read_frames(in, buffer, frames_rq);
    else
        ret = pcm_read(in->pcm, buffer, bytes);

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    pthread_mutex_unlock(&in->lock);

    if (ret)
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               in_get_sample_rate(&stream->common));

    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static void force_all_standby(struct qss_audio_device *adev)
{
    struct qss_stream_in *in;
    struct qss_stream_out *out;
    
    ALOGD("%s: BEGIN, in=%p out=%p\n", __func__, adev->active_input, adev->active_output);

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}

static void set_all_route(struct qss_audio_device *adev)
{
    struct qss_stream_in *in = adev->active_input;
    struct qss_stream_out *out = adev->active_output;
    
    ALOGD("%s: BEGIN, in=%p out=%p\n", __func__, adev->active_input, adev->active_output);

    if (in) {
        pthread_mutex_lock(&in->lock);
        in_set_route(in);
        pthread_mutex_unlock(&in->lock);
    } else {
        in_do_set_route(adev, false);
    }

    if (out) {
        pthread_mutex_lock(&out->lock);
        out_set_route(out);
        pthread_mutex_unlock(&out->lock);
    } else {
        out_do_set_route(adev, false);
    }

    set_incall_device(adev);
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;
    struct qss_stream_out *out;
    int ret;

    out = (struct qss_stream_out *)calloc(1, sizeof(struct qss_stream_out));
    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;

    out->config = pcm_config_hifi;

    out->dev = adev;
    out->standby = true;
    out->write_threshold = PLAYBACK_PERIOD_COUNT * PLAYBACK_PERIOD_SIZE;

    /* resampler */
    ret = create_resampler(DEFAULT_SAMPLING_RATE,
                           DEFAULT_SAMPLING_RATE,
                           2,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &out->resampler);
    if (ret)
        goto err_free;

    /* resampler buffer */
    out->buffer = malloc(RESAMPLER_BUFFER_SIZE);
    if (!out->buffer) {
        ret = -ENOMEM;
        goto err_free;
    }

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;
    
    pthread_mutex_lock(&adev->lock);
    adev->active_output = out;
    pthread_mutex_unlock(&adev->lock);

    return 0;

err_free:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct qss_stream_out *out = (struct qss_stream_out *)stream;

    /* put output on standby */
    out_standby(&stream->common);
    
    pthread_mutex_lock(&out->dev->lock);
    out->dev->active_output = NULL;
    pthread_mutex_unlock(&out->dev->lock);

    /* free things */
    free(out->buffer);
    release_resampler(out->resampler);
    free(out);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        pthread_mutex_lock(&adev->lock);
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bluetooth_nrec = true;
        else
            adev->bluetooth_nrec = false;
        set_incall_device(adev);
        pthread_mutex_unlock(&adev->lock);
    }

    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return NULL;
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;
    enum ril_sound_type sound_type;

    adev->voice_volume = volume;
    
    ALOGD("%s: route=%x volume=%f", __func__, adev->devices, volume);

    if (adev->mode == AUDIO_MODE_IN_CALL) {
        switch(adev->devices & AUDIO_DEVICE_OUT_ALL) {
            case AUDIO_DEVICE_OUT_EARPIECE:
                sound_type = SOUND_TYPE_VOICE;
                break;
            case AUDIO_DEVICE_OUT_SPEAKER:
            case AUDIO_DEVICE_OUT_AUX_DIGITAL:
                sound_type = SOUND_TYPE_SPEAKER;
                break;
            case AUDIO_DEVICE_OUT_WIRED_HEADSET:
            case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                sound_type = SOUND_TYPE_HEADSET;
                break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
                sound_type = SOUND_TYPE_BTVOICE;
                break;
            default:
                sound_type = SOUND_TYPE_VOICE;
                break;
        }

        ril_set_call_volume(&adev->ril, sound_type, volume);
    }

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}


static int start_call(struct qss_audio_device *adev)
{
    ALOGE("Opening modem PCMs");

    /* Open modem PCM channels */
    if (!adev->pcm_modem_dl) {
        adev->pcm_modem_dl = pcm_open(0, PORT_MODEM, PCM_OUT, &pcm_config_vx);
        if (!pcm_is_ready(adev->pcm_modem_dl)) {
            ALOGE("cannot open PCM modem DL stream: %s", pcm_get_error(adev->pcm_modem_dl));
            goto err_open_dl;
        }
    }

    if (!adev->pcm_modem_ul) {
        adev->pcm_modem_ul = pcm_open(0, PORT_MODEM, PCM_IN, &pcm_config_vx);
        if (!pcm_is_ready(adev->pcm_modem_ul)) {
            ALOGE("cannot open PCM modem UL stream: %s", pcm_get_error(adev->pcm_modem_ul));
            goto err_open_ul;
        }
    }

    pcm_start(adev->pcm_modem_dl);
    pcm_start(adev->pcm_modem_ul);

    return 0;

err_open_ul:
    pcm_close(adev->pcm_modem_ul);
    adev->pcm_modem_ul = NULL;
err_open_dl:
    pcm_close(adev->pcm_modem_dl);
    adev->pcm_modem_dl = NULL;

    return -ENOMEM;
}

static void end_call(struct qss_audio_device *adev)
{
    ALOGE("Closing modem PCMs");

    pcm_stop(adev->pcm_modem_dl);
    pcm_stop(adev->pcm_modem_ul);
    pcm_close(adev->pcm_modem_dl);
    pcm_close(adev->pcm_modem_ul);
    adev->pcm_modem_dl = NULL;
    adev->pcm_modem_ul = NULL;
}

static void select_mode(struct qss_audio_device *adev)
{
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGE("Entering IN_CALL state, in_call=%d", adev->in_call);
        if (!adev->in_call) {
            force_all_standby(adev);
            if ((adev->devices & AUDIO_DEVICE_OUT_ALL) == AUDIO_DEVICE_OUT_SPEAKER)
               adev->devices = AUDIO_DEVICE_OUT_EARPIECE |
                               AUDIO_DEVICE_IN_BUILTIN_MIC;
            else
               adev->devices &= ~AUDIO_DEVICE_OUT_SPEAKER;
            set_all_route(adev);
            start_call(adev);
            ril_set_call_clock_sync(&adev->ril, SOUND_CLOCK_START);
            adev_set_voice_volume(&adev->device, adev->voice_volume);
            adev->in_call = true;
        }
    } else {
        ALOGE("Leaving IN_CALL state, in_call=%d, mode=%d",
             adev->in_call, adev->mode);
        if (adev->in_call) {
            adev->in_call = false;
            end_call(adev);
            force_all_standby(adev);
            set_all_route(adev);
        }
    }
}

static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;

    ALOGD("%s: BEGIN, mode=%x cur=%x\n", __func__, mode, adev->mode);
    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)dev;
    struct qss_stream_in *in;
    int channel_count = popcount(config->channel_mask);
    int ret;
    
    ALOGD("%s: channel_count=%d raw=%x", __func__, channel_count, config->channel_mask);
#if 0
    config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
    channel_count = 2;
#endif

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in = (struct qss_stream_in *)calloc(1, sizeof(struct qss_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->config = pcm_config_hifi_ul;
    in->config.channels = channel_count;

    in->dev = adev;
    in->standby = true;

    /* setup resampler if required */
    in->requested_rate = config->sample_rate;
    if (in->requested_rate != in->config.rate) {
        in->buffer = malloc(in->config.period_size *
                        audio_stream_frame_size(&in->stream.common));
        if (!in->buffer) {
            ret = -ENOMEM;
            goto err_free;
        }

        in->buf_provider.get_next_buffer = in_get_next_buffer;
        in->buf_provider.release_buffer = in_release_buffer;

        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->config.channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
        if (ret) {
            free(in->buffer);
            goto err_free;
        }
    }

    *stream_in = &in->stream;
    //*channels = channel_count;

    pthread_mutex_lock(&adev->lock);
    adev->active_input = in;
    pthread_mutex_unlock(&adev->lock);

    return 0;

err_free:
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct qss_stream_in *in = (struct qss_stream_in *)stream;

    /* put input on standby */
    in_standby(&stream->common);

    pthread_mutex_lock(&in->dev->lock);
    in->dev->active_input = NULL;
    pthread_mutex_unlock(&in->dev->lock);

    /* free things */
    if (in->resampler) {
        free(in->buffer);
        release_resampler(in->resampler);
    }
    free(in);
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct qss_audio_device *adev = (struct qss_audio_device *)device;

    /* RIL */
    ril_close(&adev->ril);

    mixer_close(adev->mixer);
    free(adev);
    return 0;
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    return (/* OUT */
            AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_ALL_SCO |
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_AUX_DIGITAL |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_ALL_SCO |
            AUDIO_DEVICE_IN_DEFAULT);
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct qss_audio_device *adev;
    int ret;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct qss_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *) module;
    adev->device.common.close = adev_close;

    adev->device.get_supported_devices = adev_get_supported_devices;
    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;

    adev->mixer = mixer_open(CARD_QSS_DEFAULT);
    if (!adev->mixer) {
        ALOGE("cannot open mixer ctl on card %d", CARD_QSS_DEFAULT);
        ret = -ENODEV;
        goto err_free;
    }

    pthread_mutex_lock(&adev->lock);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->devices = AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_IN_BUILTIN_MIC;
    adev->voice_volume = 1.0f;
    adev->bluetooth_nrec = true;
    adev->pcm_modem_dl = NULL;
    adev->pcm_modem_ul = NULL;
    
    /* mixers cfgs */
    adev->mixer_cfg.in = in_mixer_cfgs;
    adev->mixer_cfg.in_nr = sizeof(in_mixer_cfgs) / sizeof(struct qss_mixer_cfg);
    adev->mixer_cfg.out = out_mixer_cfgs;
    adev->mixer_cfg.out_nr = sizeof(out_mixer_cfgs) / sizeof(struct qss_mixer_cfg);
    
    /* reset mixers */
    snd_mixer_apply(adev->mixer, adev->mixer_cfg.in, adev->mixer_cfg.in_nr,
                    0, 0, false, true);
    snd_mixer_apply(adev->mixer, adev->mixer_cfg.out, adev->mixer_cfg.out_nr,
                    0, 0, false, true);

    /* RIL */
    ret = ril_open(&adev->ril);
    if (ret) {
        ALOGE("cannot open ril interface err=%d", ret);
        goto err_mixer;
    }

    *device = &adev->device.common;
    pthread_mutex_unlock(&adev->lock);

    return 0;

err_mixer:
    pthread_mutex_unlock(&adev->lock);
    mixer_close(adev->mixer);
err_free:
    free(adev);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Aries audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
