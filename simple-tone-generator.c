/*
* simple-tone-generator.c
*
* Copyright (C) 2011-2016 Kamal Mostafa <kamal@whence.com>
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

//#include <math.h>
//#include <strings.h>
//#include <stdlib.h>
//#include <assert.h>
//#include <stdio.h>

#include "saturn-minimodem.h"
#include "simpleaudio.h"

static float tone_mag = 1.0;

static unsigned int sin_table_len = 0;
static short *sin_table_short = NULL;
//static float *sin_table_float;

void
simpleaudio_tone_init( unsigned int new_sin_table_len, float mag )
{

    tone_mag = mag;

    if ( sin_table_len != 0 )
    {
        jo_core_error("simpleaudio_tone_init called twice!!");
        return;

        /* We never use this code
        sin_table_short = my_realloc(sin_table_short, sin_table_len * sizeof(short), new_sin_table_len * sizeof(short));
//        sin_table_float = realloc(sin_table_float, sin_table_len * sizeof(float));
        if ( !sin_table_short) {
            jo_core_error("simpleaudio_tone_init: out of memory");
            return;
        }

        unsigned int i;
        unsigned short mag_s = 32767.0f * tone_mag + 0.5f;
        if ( tone_mag > 1.0f ) // clamp to 1.0 to avoid overflow
            mag_s = 32767;
        if ( mag_s < 1 ) // "short epsilon"
            mag_s = 1;

        for ( i=0; i<sin_table_len; i++ )
        {
            sin_table_short[i] = lroundf( mag_s * sinf((float)M_PI*2*i/sin_table_len));
        }

        for ( i=0; i<sin_table_len; i++ )
        {
            if(i >= 2048)
            {
                sin_table_float[i] = tone_mag * sinf((float)M_PI*2*i/sin_table_len);
            }
        }
        */

    } else {
        if ( sin_table_short ) {
            jo_free(sin_table_short);
            sin_table_short = NULL;
        }
        /*
        if ( sin_table_float ) {
            jo_free(sin_table_float);
            sin_table_float = NULL;
        }
        */
    }

    sin_table_len = new_sin_table_len;
}

/*
* in: turns (0.0 to 1.0)    out: (-32767 to +32767)
*/
static inline short
sin_lu_short( float turns )
{
    int t = (float)sin_table_len * turns + 0.5f;
    t %= sin_table_len;
    return sin_table_short[t];
}

/*
* in: turns (0.0 to 1.0)    out: -1.0 to +1.0
*/
/*
static inline float
sin_lu_float( float turns )
{
    int t = (float)sin_table_len * turns + 0.5f;
    t %= sin_table_len;
    return sin_table_float[t];
}
*/


/* "current" phase state of the tone generator */
static float sa_tone_cphase = 0.0;

void
simpleaudio_tone_reset()
{
    sa_tone_cphase = 0.0;
}

void
simpleaudio_tone(simpleaudio *sa_out, float tone_freq, size_t nsamples_dur)
{
    unsigned int framesize = simpleaudio_get_framesize(sa_out);

    void *buf = jo_malloc(nsamples_dur * framesize);
    if(buf == NULL)
    {
        jo_core_error("simpleaudio_tone: jo_malloc fail");
        return;
    }

    if ( tone_freq != 0 ) {

        float wave_nsamples = simpleaudio_get_rate(sa_out) / tone_freq;
        size_t i;

#define TURNS_TO_RADIANS(t)	( (float)M_PI*2 * (t) )
#define SINE_PHASE_TURNS	( (float)i/wave_nsamples + sa_tone_cphase )
#define SINE_PHASE_RADIANS	TURNS_TO_RADIANS(SINE_PHASE_TURNS)

        switch ( simpleaudio_get_format(sa_out) ) {

            /*
            case SA_SAMPLE_FORMAT_FLOAT:
            {
                float *float_buf = buf;
                if ( sin_table_float ) {
                for ( i=0; i<nsamples_dur; i++ )
                    float_buf[i] = sin_lu_float(SINE_PHASE_TURNS);
                } else {
                for ( i=0; i<nsamples_dur; i++ )
                    float_buf[i] = tone_mag * sinf(SINE_PHASE_RADIANS);
                }
            }
            break;
            */

            case SA_SAMPLE_FORMAT_S16:
            {
                short *short_buf = buf;
                if ( sin_table_short ) {
                for ( i=0; i<nsamples_dur; i++ )
                    short_buf[i] = sin_lu_short(SINE_PHASE_TURNS);
                } else {
                unsigned short mag_s = 32767.0f * tone_mag + 0.5f;
                if ( tone_mag > 1.0f ) // clamp to 1.0 to avoid overflow
                    mag_s = 32767;
                if ( mag_s < 1 ) // "short epsilon"
                    mag_s = 1;
                for ( i=0; i<nsamples_dur; i++ )
                    short_buf[i] = lroundf( mag_s * sinf(SINE_PHASE_RADIANS) );
                }
                break;
            }

            default:
                jo_core_error("Invalid format");
                return;
                break;
            }
            sa_tone_cphase = fmodf(sa_tone_cphase + (float)nsamples_dur/wave_nsamples, 1.0);

        } else {

            bzero(buf, nsamples_dur * framesize);
            sa_tone_cphase = 0.0;

        }

    int result = simpleaudio_write(sa_out, buf, nsamples_dur);
    if( result <= 0 )
    {
        jo_core_error("simpleaudio_write failed!! %d", result);
        jo_free(buf);
        return;
    }

    jo_free(buf);
}
