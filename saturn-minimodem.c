#include <jo/jo.h>

#include "saturn-minimodem.h"

#include "simpleaudio.h"
#include "databits.h"

#define DATA_RATE 1200.0f // how many bits per second. This is the -r parameter in minimodem
#define SAMPLE_RATE 44100 // the frequency. This is the -R parameter in minimodem

#define NUM_START_BITS 4
#define NUM_STOP_BITS 4
#define NUM_SYNC_BYTES 2
#define SYNC_BYTE 0xAB

simpleaudio* tx_sa_out;
float tx_bfsk_mark_f;
unsigned int tx_bit_nsamples;
unsigned int tx_flush_nsamples;

int tx_transmitting = 0;
int tx_print_eot = 0;
int tx_leader_bits_len = 2;
int tx_trailer_bits_len = 2;

extern PCM g_PcmChannel;
extern unsigned int g_AudioBufferSize;

// locals moved to globals to try and make a library out of minimodem
int g_tx_interactive = 0;
simpleaudio *g_sa_out;
float g_bfsk_data_rate = 0.0;
float g_bfsk_mark_f = 0;
float g_bfsk_space_f = 0;
unsigned int g_bfsk_n_data_bits = 0;
int g_bfsk_nstartbits = -1;
float g_bfsk_nstopbits = -1;
int g_invert_start_stop = 0;
int g_bfsk_msb_first = 0;
unsigned int g_bfsk_do_tx_sync_bytes = 0;
unsigned long long g_bfsk_sync_byte = -1;
int g_txcarrier = 0;
databits_encoder *g_bfsk_databits_encode = databits_encode_ascii8;

unsigned char* g_TransferBuffer = NULL;
unsigned int g_TransferBufferSize = 0;
unsigned int g_TransferProgress = 0;

int g_isRunning = 0;

// Computes the sine of arg (measured in radians).
float sinf(float x)
{
    float temp = jo_sin_radf(x);
    return temp;
}

// round the floating point number to the nearest integer
long int lroundf(float x)
{
    jo_fixed temp;
    float rounded;
    float decimalFloat;
    int integerPart;

    if(x >= 32768.0f || x <= -382768.0f)
    {
        jo_core_error("float is out of range!! %f", x);
        return -1;
    }

    temp = jo_float2fixed(x);
    integerPart = jo_fixed2int(temp);
    rounded = integerPart;

    if(x < 0)
    {
        decimalFloat = x - rounded; // decimal part only

        if(decimalFloat > 0.5)
        {
            rounded = x - decimalFloat + 1;
        }
        else
        {
            rounded = x - decimalFloat;
        }

        temp = jo_float2fixed(rounded);
        integerPart = jo_fixed2int(temp);
    }
    else
    {
        decimalFloat = x - rounded; // decimal part only

        if(decimalFloat > 0.5)
        {
            rounded = x - decimalFloat + 1;
        }
        else
        {
            rounded = x - decimalFloat;
        }

        temp = jo_float2fixed(rounded);
        integerPart = jo_fixed2int(temp);
    }

    return integerPart;
}

// rounds a floating pointer number down
float roundDown(float x)
{
    float result;
    jo_fixed temp;
    int integerPart;
    int decimalPart;

    temp = jo_float2fixed(x);
    integerPart = jo_fixed2int(temp);
    decimalPart = temp & 0xffff;

    // if there is no decimal part just return without rounding
    if(decimalPart == 0)
    {
        //return integerPart;
    }
    else if(integerPart < 0)
    {
        // negative values are already rounded up, add one
        integerPart = integerPart + 1;
    }

    temp = jo_int2fixed(integerPart);
    result = jo_fixed2float(temp);

    // Bugbug: check negative values
    // I don't believe we ever get called with them
    if(x < 0)
    {
        jo_core_error("x: %f rounded %f:", x, result);
        return -1;
    }

    return result;
}

// floating point modulus
// I believe y is always 1.0 when we are called
// I believe x is always positive when we are called
float fmodf(float x, float y)
{
    if(x == 0)
    {
        jo_core_error("x is zero!!");
        return 0.0f;
    }

    if(y == 0)
    {
        jo_core_error("y is zero!!");
        return 0.0f;
    }

    if(y != 1.0)
    {
        jo_core_error("y is not 1!!");
        return 0.0f;
    }

    float quotient = x / y;
    float roundedQuotient = roundDown(quotient);
    float mod = x -(roundedQuotient * y);

    if(x < 0)
    {
        jo_core_error("x is negative!!\n");
    }

    if(y < 0)
    {
        jo_core_error("y is negative!!\n");
    }

    return mod;
}

// bzero drop in
void bzero(void *s, size_t n)
{
    jo_memset(s, 0, n);
}

/*
 * rudimentary BFSK transmitter
 */
static void fsk_transmit_frame(
	simpleaudio *sa_out,
	unsigned int bits,
	unsigned int n_data_bits,
	size_t bit_nsamples,
	float bfsk_mark_f,
	float bfsk_space_f,
	float bfsk_nstartbits,
	float bfsk_nstopbits,
	int invert_start_stop,
	int bfsk_msb_first
	)
{
    //jo_core_error("ftf %d %d %d", bits, n_data_bits, bit_nsamples);
    unsigned int i;
    if ( bfsk_nstartbits > 0 )
    {
	simpleaudio_tone(sa_out, invert_start_stop ? bfsk_mark_f : bfsk_space_f,
			bit_nsamples * bfsk_nstartbits);
    }// start

    for ( i=0; i<n_data_bits; i++ ) {				// data
        unsigned int bit;
        if (bfsk_msb_first) {
            bit = ( bits >> (n_data_bits - i - 1) ) & 1;
        } else {
            bit = ( bits >> i ) & 1;
        }

        float tone_freq = bit == 1 ? bfsk_mark_f : bfsk_space_f;

        //jo_core_error("bit freq %d", bit);

        simpleaudio_tone(sa_out, tone_freq, bit_nsamples);
    }

    if ( bfsk_nstopbits > 0 )
	simpleaudio_tone(sa_out, invert_start_stop ? bfsk_space_f : bfsk_mark_f,
			bit_nsamples * bfsk_nstopbits);		// stop
}

// returns true if x is a valid B64 character
bool isB64Char(char x)
{
    if(x >= 'A' && x <= 'Z')
    {
        return true;
    }

    if(x >= 'a' && x <= 'z')
    {
        return true;
    }

    if(x >= '0' && x <= '9')
    {
        return true;
    }

    // special characters
    // '=' is reserved for padding
    if(x == '/' || x == '+' || x == '=')
    {
        return true;
    }

    // BUGBUG: these are new line is not a valid character but let it slide for our test message...
    if(x == '\n' || x == ' ')
    {
        return true;
    }

    jo_core_error("hello world 0x%x", x);
    return false;
}



void tx_stop_transmit_sighandler(int sig)
{
    UNUSED(sig);

    int j;
    for ( j=0; j<tx_trailer_bits_len; j++ )
        simpleaudio_tone(tx_sa_out, tx_bfsk_mark_f, tx_bit_nsamples);

    if ( tx_flush_nsamples )
        simpleaudio_tone(tx_sa_out, 0, tx_flush_nsamples);

    tx_transmitting = 0;
}

// modified version of fsk_transmit_stdin to transmit a buffer of up to 128 bytes
// the buffer is passed in via a global
static void fsk_transmit_buffer(
	simpleaudio *sa_out,
	int tx_interactive,
	float data_rate,
	float bfsk_mark_f,
	float bfsk_space_f,
	int n_data_bits,
	float bfsk_nstartbits,
	float bfsk_nstopbits,
	int invert_start_stop,
	int bfsk_msb_first,
	unsigned int bfsk_do_tx_sync_bytes,
	unsigned int bfsk_sync_byte,
	databits_encoder encode,
	int txcarrier,
    bool flushBufferOnly
	)
{
    UNUSED(txcarrier);

    size_t sample_rate = simpleaudio_get_rate(sa_out);
    size_t bit_nsamples = sample_rate / data_rate + 0.5f;

    int counter = 0;

    tx_sa_out = sa_out;
    tx_bfsk_mark_f = bfsk_mark_f;
    tx_bit_nsamples = bit_nsamples;
    if ( tx_interactive )
        tx_flush_nsamples = 1;// sample_rate/2; // 0.5 sec of zero samples to flush
    else
        tx_flush_nsamples = 0;

    // arbitrary chosen timeout value: 1/25 of a second
    unsigned int idle_carrier_usec = (1000000/25);

    if(flushBufferOnly)
    {
        jo_core_error("in flush only code path should never get here");
        return;
    }

    g_isRunning = 0;

    tx_transmitting = 0;

    int end_of_file = 0;
    unsigned char buf;
    int idle = 0;
    while ( !end_of_file )
    {
        if(g_isRunning == 1)
        {
            //jo_core_error("breaking out of loop because is running = 1");
            end_of_file = 1;
            continue;
        }

        counter++;

        if(1==1)
        {
            // check if the user pressed B
            if(jo_is_pad1_key_pressed(JO_KEY_B))
            {
                end_of_file = 1;
                continue; //Do nothing else
            }

             // check if the transfer is complete
            if(g_TransferProgress >= g_TransferBufferSize)
            {
               end_of_file = 1;
               continue; //Do nothing else
            }
            else
            {
                // grab the next byte for transfer
                buf = g_TransferBuffer[g_TransferProgress];
                g_TransferProgress++;

                if(buf == SYNC_BYTE)
                {
                    jo_core_error("uh oh found sync byte in our transmission");
                    return;
                }
            }

            idle = 0;
        }
        else
            idle = 1;

        if( !idle )
        {
            // fprintf(stderr, "<c=%d>", c);
            unsigned int nwords;
            unsigned int bits[2];
            unsigned int j;
            nwords = encode(bits, buf);

            if ( !tx_transmitting )
            {
                tx_transmitting = 1;
                    // emit leader tone (mark)
                    for ( j=0; j<(unsigned int)tx_leader_bits_len; j++ )
                        simpleaudio_tone(sa_out, invert_start_stop ? bfsk_space_f : bfsk_mark_f, bit_nsamples);
            }
            if ( tx_transmitting < 2)
            {
                tx_transmitting = 2;
                // emit "preamble" of sync bytes
                for ( j=0; j<(unsigned int)bfsk_do_tx_sync_bytes; j++ )
                    fsk_transmit_frame(sa_out, bfsk_sync_byte, n_data_bits,
                        bit_nsamples, bfsk_mark_f, bfsk_space_f,
                        bfsk_nstartbits, bfsk_nstopbits, invert_start_stop, 0);
            }

            // emit data bits
            for ( j=0; j<nwords; j++ )
            {
                fsk_transmit_frame(sa_out, bits[j], n_data_bits,
                        bit_nsamples, bfsk_mark_f, bfsk_space_f,
                        bfsk_nstartbits, bfsk_nstopbits, invert_start_stop, bfsk_msb_first);
            }

            if(counter > 127)
            {
                end_of_file = 1;
            }
        }
        else
        {
            tx_transmitting = 1;
            // emit idle tone (mark)
            simpleaudio_tone(sa_out,
                invert_start_stop ? bfsk_space_f : bfsk_mark_f,
                idle_carrier_usec * sample_rate / 1000000);
        }

    }

    tx_stop_transmit_sighandler(0);
    sa_saturn_flush_buffer();

    tx_transmitting = 0;

    if ( !tx_transmitting )
        return;
}

int SaturnMinimodem_initTransfer(unsigned char* data, unsigned int size)
{
    if(data == NULL)
    {
        return -1;
    }

    if(size == 0)
    {
        return -1;
    }

    g_TransferBuffer = data;
    g_TransferBufferSize = size;
    g_TransferProgress = 0;

    return 0;
}

int SaturnMinimodem_transferStatus(unsigned int* bytesTransferred, unsigned int* totalBytes)
{
    if(bytesTransferred == NULL || totalBytes == NULL)
    {
        return -1;
    }

    if(g_TransferBuffer == NULL || g_TransferBufferSize == 0)
    {
        return -1;
    }

    *bytesTransferred = g_TransferProgress;
    *totalBytes = g_TransferBufferSize;

    return 0;
}

// wrapper function to transfer up to 128 more bytes of the buffer
// SaturnMinimode_initTransfer() must be called first
int SaturnMinimodem_transfer(void)
{
    bool flushBufferOnly = false;

    if(g_TransferBuffer == NULL || g_TransferBufferSize == 0)
    {
        jo_core_error("Call initTransfer first!!\n");
        return TRANSFER_ERROR;
    }

    if (slPCMStat(&g_PcmChannel))
    {
        return TRANSFER_BUSY;
    }

    // check if the transfer is complete
    if(g_TransferProgress >= g_TransferBufferSize)
    {
        if(sa_saturn_is_buffer_flushed() == true)
        {
            // sent all bytes and audio is flushed
            g_TransferProgress = 0;
            return TRANSFER_COMPLETE;
        }

        // bytes sent but audio is not flushed
        // try to flush the audio
        flushBufferOnly = true;
    }
    else
    {
        // transfer is not complete, call fsk_transmit_buffer()
        flushBufferOnly = false;
    }

    fsk_transmit_buffer(g_sa_out,
                    g_tx_interactive,
                    g_bfsk_data_rate,
                    g_bfsk_mark_f,
                    g_bfsk_space_f,
                    g_bfsk_n_data_bits,
                    g_bfsk_nstartbits,
                    g_bfsk_nstopbits,
                    g_invert_start_stop,
                    g_bfsk_msb_first,
                    g_bfsk_do_tx_sync_bytes,
                    g_bfsk_sync_byte,
                    g_bfsk_databits_encode,
                    g_txcarrier,
                    flushBufferOnly);

    // we called fsk_transmit_buffer but we are not necessarily complete
    return TRANSFER_PROGRESS;
}

// initializes the state for calling the minimodem functions
int SaturnMinimodem_init(void)
{
    int TX_mode = -1;
    float band_width = 0;
    unsigned int bfsk_inverted_freqs = 0;
    int autodetect_shift;
    char *filename = NULL;

    // fsk_confidence_threshold : signal-to-noise squelch control
    //
    // The minimum SNR-ish confidence level seen as "a signal".
    float fsk_confidence_threshold = 1.5;

    // fsk_confidence_search_limit : performance vs. quality
    //
    // If we find a frame with confidence > confidence_search_limit,
    // quit searching for a better frame.  confidence_search_limit has a
    // dramatic effect on peformance (high value yields low performance, but
    // higher decode quality, for noisy or hard-to-discern signals (Bell 103,
    // or skewed rates).
    float fsk_confidence_search_limit = 2.3f;

    sa_backend_t sa_backend = SA_BACKEND_SEGASATURN;
    char *sa_backend_device = NULL;
    sa_format_t sample_format = SA_SAMPLE_FORMAT_S16;
    unsigned int sample_rate = SAMPLE_RATE;
    unsigned int nchannels = 1; // FIXME: only works with one channel

    float tx_amplitude = 1.0;
    unsigned int tx_sin_table_len = 4096;
    int output_mode_raw_nbits = 0;

    enum {
        MINIMODEM_OPT_UNUSED=256,	// placeholder
        MINIMODEM_OPT_MSBFIRST,
        MINIMODEM_OPT_STARTBITS,
        MINIMODEM_OPT_STOPBITS,
        MINIMODEM_OPT_INVERT_START_STOP,
        MINIMODEM_OPT_SYNC_BYTE,
        MINIMODEM_OPT_LUT,
        MINIMODEM_OPT_FLOAT_SAMPLES,
        MINIMODEM_OPT_RX_ONE,
        MINIMODEM_OPT_BENCHMARKS,
        MINIMODEM_OPT_BINARY_OUTPUT,
        MINIMODEM_OPT_BINARY_RAW,
        MINIMODEM_OPT_PRINT_FILTER,
        MINIMODEM_OPT_XRXNOISE,
        MINIMODEM_OPT_PRINT_EOT,
        MINIMODEM_OPT_TXCARRIER
    };

    TX_mode = 1;

    if ( TX_mode == -1 )
        TX_mode = 0;

    // The receive code requires floating point samples to feed to the FFT
    if ( TX_mode == 0 )
        sample_format = SA_SAMPLE_FORMAT_FLOAT;

    g_bfsk_data_rate = DATA_RATE;
    g_bfsk_n_data_bits = 8;

    //if ( bfsk_data_rate == 0.0f )
    //  usage();

    //if ( output_mode_binary || output_mode_raw_nbits )
    //    bfsk_databits_decode = databits_decode_binary;

    if ( output_mode_raw_nbits ) {
        g_bfsk_nstartbits = 0;
        g_bfsk_nstopbits = 0;
        g_bfsk_n_data_bits = output_mode_raw_nbits;
    }

    if ( g_bfsk_data_rate >= 400 ) {

        autodetect_shift = - ( g_bfsk_data_rate * 5 / 6 );
        if ( g_bfsk_mark_f == 0 )
            g_bfsk_mark_f  = g_bfsk_data_rate / 2 + 600;
        if ( g_bfsk_space_f == 0 )
            g_bfsk_space_f = g_bfsk_mark_f - autodetect_shift;
        if ( band_width == 0 )
            band_width = 200;
    } else if ( g_bfsk_data_rate >= 100 ) {

        autodetect_shift = 200;
        if ( g_bfsk_mark_f == 0 )
            g_bfsk_mark_f  = 1270;
        if ( g_bfsk_space_f == 0 )
            g_bfsk_space_f = g_bfsk_mark_f - autodetect_shift;
        if ( band_width == 0 )
            band_width = 50;	// close enough
    } else {

        autodetect_shift = 170;
        if ( g_bfsk_mark_f == 0 )
            g_bfsk_mark_f  = 1585;
        if ( g_bfsk_space_f == 0 )
            g_bfsk_space_f = g_bfsk_mark_f - autodetect_shift;
        if ( band_width == 0 ) {
            band_width = 10;	// FIXME chosen arbitrarily
        }
    }

    // defaults: 1 start bit, 1 stop bit
    if ( g_bfsk_nstartbits < 0 )
        g_bfsk_nstartbits = 1;
    if ( g_bfsk_nstopbits < 0 )
        g_bfsk_nstopbits = 1.0;

    // do not transmit any leader tone if no start bits
    if ( g_bfsk_nstartbits == 0 )
        tx_leader_bits_len = 0;

    if ( bfsk_inverted_freqs ) {
        float t = g_bfsk_mark_f;
        g_bfsk_mark_f = g_bfsk_space_f;
        g_bfsk_space_f = t;
    }

    if ( band_width > g_bfsk_data_rate )
        band_width = g_bfsk_data_rate;

    // sanitize confidence search limit
    if ( fsk_confidence_search_limit < fsk_confidence_threshold )
        fsk_confidence_search_limit = fsk_confidence_threshold;

    char *stream_name = NULL;

    if ( filename ) {
        sa_backend = SA_BACKEND_FILE;
        stream_name = filename;
    }

    // transmit
    if ( TX_mode ) {

        simpleaudio_tone_init(tx_sin_table_len, tx_amplitude);

        g_tx_interactive = 0;
        if ( ! stream_name ) {
            g_tx_interactive = 1;
            stream_name = "output audio";
        }

        g_sa_out = simpleaudio_open_stream(sa_backend, sa_backend_device,
                        SA_STREAM_PLAYBACK,
                        sample_format, sample_rate, nchannels,
                        "test", stream_name);
        if ( !g_sa_out )
        {
            jo_core_error("sa_out is null");
            return 1;
        }

        g_bfsk_nstartbits = NUM_START_BITS;
        g_bfsk_nstopbits = NUM_STOP_BITS;


        g_bfsk_do_tx_sync_bytes = NUM_SYNC_BYTES;
        g_bfsk_sync_byte = SYNC_BYTE;

        return 0;
    }

    return -1;
}
