#include "encode.h"

correct_reed_solomon* g_reedSolomon = NULL;

// initializes the transmission header consisting of:
// - 4-byte signature SGEX
// - md5hash
// - filename
// - numBytes
// - numBytes length save data
int initializeTransmissionHeader(unsigned char* md5Hash, unsigned int md5HashSize, char* saveFilename, unsigned int saveFileSize)
{
    PTRANSMISSION_HEADER header = (PTRANSMISSION_HEADER)g_Game.transmissionData;

    if(header == NULL || md5Hash == NULL ||
       md5HashSize != MD5_HASH_SIZE ||
       saveFilename == NULL || saveFileSize == 0)
    {
        jo_core_error("Invalid parameters to initialize transmission header!!");
        return -1;
    }

    jo_memset(header, 0, sizeof(TRANSMISSION_HEADER));

    memcpy(header->magic, TRANSMISSION_MAGIC, TRANSMISSION_MAGIC_SIZE);
    memcpy(header->md5Hash, md5Hash, MD5_HASH_SIZE);
    strncpy(header->saveFilename, saveFilename, MAX_SAVE_FILENAME - 1);
    header->saveFileSize = saveFileSize;

    return 0;
}

// Counts the number of times the escapeByte and the syncByte appears in the buffer
// For every one we find, we need to an additional byte to escape them
unsigned int countEscapeBytes(unsigned char* buffer, unsigned int bufferSize)
{
    unsigned int count = 0;

    for(unsigned int i = 0; i < bufferSize; i++)
    {
        if(buffer[i] == ESCAPE_BYTE)
        {
            count++;
        }
        else if(buffer[i] == SYNC_BYTE)
        {
            count++;
        }
    }

    return count;
}

// escapes all SYNC_BYTEs and and ESCAPE_BYTEs. Resizes the buffer as needed
unsigned int escapeBuffer(unsigned char** buffer, unsigned int* bufferSize)
{
    unsigned int escapeCount = 0;
    unsigned char* newBuf = NULL;
    unsigned int newBufSize = 0;

    escapeCount = countEscapeBytes(*buffer, *bufferSize);

    // found escape bytes, need to reallocate the buffer
    if(escapeCount != 0)
    {
        unsigned int i = 0;
        unsigned int j = 0;

        newBufSize = *bufferSize + escapeCount;

        // we found escape bytes, resize the buffer
        newBuf = jo_malloc(newBufSize);
        if(newBuf == NULL)
        {
            jo_core_error("Failed to reallocate buffer!!");
            return -1;
        }

        // copy over the buffers, escaping the escape sequence
        for(i = 0, j = 0; i < *bufferSize; i++, j++)
        {
            if((*buffer)[i] == ESCAPE_BYTE)
            {
                // escape the escape byte
                newBuf[j] = ESCAPE_BYTE;
                j++;
                newBuf[j] = ESCAPE_BYTE;
            }
            else if((*buffer)[i] == SYNC_BYTE)
            {
                // escape the sync byte
                newBuf[j] = ESCAPE_BYTE;
                j++;
                newBuf[j] = ESCAPE_SYNC_BYTE;
            }
            else
            {
                // no escape
                newBuf[j] = (*buffer)[i];
            }
        }

        // free the old buffer
        jo_free(*buffer);

        *buffer = newBuf;
        *bufferSize = newBufSize;
    }

    return 0;
}

// calculates how many bytes are needed to Reed Solomon encode a buffer
unsigned int reedSolomonOutSize(unsigned int dataSize)
{
    unsigned int numChunks = dataSize/DATA_CHUNK_SIZE;

    // if our data does not fit on a chunk boundary
    // include another chunk
    if(dataSize % DATA_CHUNK_SIZE)
    {
        numChunks++;
    }

    return dataSize + (numChunks*PARITY_BYTES);
}

// Reed Solomon encodes a buffer
// outbuffer must have been previously allocated with a size returned by reedSolomonOutSize
int reedSolomonEncode(unsigned char* inBuf, unsigned int inSize, unsigned char* outBuf)
{
    unsigned int dataWritten = 0;

    for(unsigned int i = 0; i < inSize; i += DATA_CHUNK_SIZE)
    {
        unsigned int chunkSize = 0;

        if(inSize - i >= DATA_CHUNK_SIZE)
        {
            chunkSize = DATA_CHUNK_SIZE;
        }
        else
        {
            chunkSize = inSize  - i;
        }

        correct_reed_solomon_encode(g_reedSolomon, inBuf + i, chunkSize, outBuf + dataWritten);

        dataWritten += CODEWORD_SIZE;
    }

    return 0;
}