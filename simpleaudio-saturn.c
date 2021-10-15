/*
* simpleaudio-saturn.c
*
* Copyright (C) 2011-2012 Kamal Mostafa <kamal@whence.com>
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <jo/jo.h>
#include "saturn-minimodem.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "simpleaudio.h"
#include "simpleaudio_internal.h"

#define UNUSED(x) (void)(x)
#define FLUSH_BUFFER_MIN 26000

extern int g_isRunning;

PCM g_PcmChannel = {(_Mono | _PCM16Bit),
                0,
                127,
                0,
                0x0,
                0,
                0,
                0,
                0 };

// typedef struct
// {
//     Uint8	mode ;		/* Mode */
//     Uint8	channel ;	/* PCM Channel Number */
//     Uint8	level ;		/* 0 ~ 127 */
//     Sint8	pan ;		/* -128 ~ +127 */
//     Uint16	pitch ;
//     Uint8	eflevelR ;	/* Effect level for Right(mono) 0 ~ 7 */
//     Uint8	efselectR ;	/* Effect select for Right(mono) 0 ~ 15 */
//     Uint8	eflevelL ;	/* Effect level for Left 0 ~ 7 */
//     Uint8	efselectL ;	/* Effect select for Left 0 ~ 15 */
// } PCM ;

bool sa_saturn_flush_buffer();

// copied from jo engine
// I needed to modify this because Jo Engine does not let you adjust the channel frequency
void jo_audio_play_sound_on_channel2(jo_sound * const sound, const unsigned char channel)
{
    int result = 0;

#ifdef JO_DEBUG
    if (sound == JO_NULL)
    {
        jo_core_error("sound is null");
        return;
    }
    if (channel >= JO_SOUND_MAX_CHANNEL)
    {
        jo_core_error("channel (%d) is too high (max=%d)", channel, JO_SOUND_MAX_CHANNEL);
        return;
    }
#endif

    if (slPCMStat(&g_PcmChannel))
        return;
    //slSndFlush();
    sound->current_playing_channel = channel;
    //__jo_internal_pcm2[(int)channel].mode = (Uint8)sound->mode;
    result = slPCMOn(&g_PcmChannel, sound->data, sound->data_length);
    if(result < 0)
    {
        jo_core_error("slPCMon fail: %d", result);
    }
}

/*
* Sega Saturn backend for simpleaudio
*/

static ssize_t
sa_saturn_read( simpleaudio *sa, void *buf, size_t nframes )
{
    UNUSED(sa);
    UNUSED(buf);
    UNUSED(nframes);

    // reading not supported
    return -1;
}

unsigned char* g_AudioBuffer = NULL;
unsigned int g_AudioBufferSize = 0;
unsigned int g_MaxAudioBufferSize = FLUSH_BUFFER_MIN * 6;

unsigned int totalBytes = 0;

// plays whatever is in the audio buffer
bool sa_saturn_flush_buffer()
{
    jo_sound sound = {0};

    if(sa_saturn_is_buffer_flushed())
    {
        return true;
    }

    if (slPCMStat(&g_PcmChannel))
    {
        g_isRunning = 1;
        jo_core_error("Found channel in flush buffer");
        return false;
    }

    if(g_AudioBufferSize < 20000)
    {
        jo_printf(2, 23, "Small buffer size, possible error: %d                       ", g_AudioBufferSize);
    }

    if(g_AudioBufferSize < FLUSH_BUFFER_MIN)
    {
        jo_memset(g_AudioBuffer + g_AudioBufferSize, 0, FLUSH_BUFFER_MIN - g_AudioBufferSize);
        g_AudioBufferSize = FLUSH_BUFFER_MIN;
    }
    else
    {
        //jo_core_error("Buffer is big enough what the heck %x %x", g_AudioBufferSize, FLUSH_BUFFER_MIN);
    }

    sound.current_playing_channel = 0;
    sound.data = (char*)g_AudioBuffer;
    sound.data_length = g_AudioBufferSize;
    sound.mode = JoSoundMono16Bit;

    jo_audio_play_sound_on_channel2(&sound, 0);

    g_AudioBufferSize = 0;

    return true;
}

// returns true if the saturn's audio buffer's have been flushed
bool sa_saturn_is_buffer_flushed()
{
    if(g_AudioBufferSize == 0)
    {
        return true;
    }

    return false;
}

// buffers the audio to play on the Saturn
// When sa_saturn_flush_buffer() is called, these buffer is sent to the audio system
static ssize_t sa_saturn_write(simpleaudio *sa, void *buf, size_t nframes)
{
    size_t nbytes = nframes * sa->backend_framesize;

    totalBytes += nbytes;

    if(g_AudioBuffer == NULL)
    {
        g_AudioBufferSize = nbytes;
        g_AudioBuffer = jo_malloc(g_MaxAudioBufferSize);
        if(g_AudioBuffer == NULL)
        {
            jo_core_error("Failed to jo_malloc");
            return -1;
        }
        jo_memset(g_AudioBuffer, 0, g_MaxAudioBufferSize);
        memcpy(g_AudioBuffer, buf, nbytes);
    }
    else
    {
        if(g_AudioBufferSize + nbytes > g_MaxAudioBufferSize)
        {
            jo_core_error("Out of memory!! %d %d", g_MaxAudioBufferSize + nbytes, g_MaxAudioBufferSize);
            return -1;
        }

        //g_AudioBuffer = my_realloc(g_AudioBuffer, g_AudioBufferSize, g_AudioBufferSize + nbytes);
        if(g_AudioBuffer == NULL)
        {
            jo_core_error("Failed to realloc");
            return -1;
        }

        // BUGBUG: remove this
        memcpy(g_AudioBuffer + g_AudioBufferSize, buf, nbytes);
        g_AudioBufferSize = g_AudioBufferSize + nbytes;
    }

    if (slPCMStat(&g_PcmChannel))
    {
        g_isRunning = 1;
        //jo_core_error("Found channel");
        return 1;
    }

    if(g_AudioBufferSize < FLUSH_BUFFER_MIN)
    {
        return nframes;
    }

    return nframes;
}

static void
sa_saturn_close( simpleaudio *sa )
{
    UNUSED(sa);

    // do I need to do anything here??
    return;
}

// macros taken from https://github.com/ponut64/68k
static const int logtbl[] = {
/* 0 */		0,
/* 1 */		1,
/* 2 */		2, 2,
/* 4 */		3, 3, 3, 3,
/* 8 */		4, 4, 4, 4, 4, 4, 4, 4,
/* 16 */	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
/* 32 */	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
/* 64 */	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
/* 128 */	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
            8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
    };

#define PCM_MSK1(a)				((a)&0x0001)
#define PCM_MSK3(a)				((a)&0x0007)
#define PCM_MSK4(a)				((a)&0x000F)
#define PCM_MSK5(a)				((a)&0x001F)
#define PCM_MSK10(a)			((a)&0x03FF)

#define PCM_SCSP_FREQUENCY					(44100L)

#define PCM_CALC_OCT(smpling_rate) 											\
        ((int)logtbl[PCM_SCSP_FREQUENCY / ((smpling_rate) + 1)])

#define PCM_CALC_SHIFT_FREQ(oct)											\
        (PCM_SCSP_FREQUENCY >> (oct))

#define PCM_CALC_FNS(smpling_rate, shift_freq)								\
        ((((smpling_rate) - (shift_freq)) << 10) / (shift_freq))

#define PCM_SET_PITCH_WORD(oct, fns)										\
        ((int)((PCM_MSK4(-(oct)) << 11) | PCM_MSK10(fns)))

static int
sa_saturn_open_stream(
        simpleaudio *sa,
        const char *backend_device,
        sa_direction_t sa_stream_direction,
        sa_format_t sa_format,
        unsigned int rate, unsigned int channels,
        char *app_name, char *stream_name )
{
    UNUSED(backend_device);
    UNUSED(sa_stream_direction);
    UNUSED(sa_format);
    UNUSED(rate);
    UNUSED(channels);
    UNUSED(app_name);
    UNUSED(stream_name);

    switch ( sa->format ) {

        case SA_SAMPLE_FORMAT_S16:
            break;
        default:
            return 0;
    }

    sa->backend_handle = NULL;
    sa->backend_framesize = sa->channels * sa->samplesize;

    // based on these values configure the PCM channel
    int octr;
    int shiftr;
    int fnsr;

    octr = PCM_CALC_OCT(sa->rate);
    shiftr = PCM_CALC_SHIFT_FREQ(octr);
    fnsr = PCM_CALC_FNS(sa->rate, shiftr);

    g_PcmChannel.pitch = PCM_SET_PITCH_WORD(octr, fnsr);

    return 1;
}

const struct simpleaudio_backend simpleaudio_backend_segasaturn = {
    sa_saturn_open_stream,
    sa_saturn_read,
    sa_saturn_write,
    sa_saturn_close,
};
