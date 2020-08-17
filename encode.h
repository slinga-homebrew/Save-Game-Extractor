#pragma once
#include <jo/jo.h>
#include "main.h"
#include "libcorrect/correct.h"

#define TRANSMISSION_MAGIC_SIZE     4
#define TRANSMISSION_MAGIC          "SGEX"
#define TRANSMISSION_HEADER_SIZE    sizeof(TRANSMISSION_HEADER)
#define MD5_HASH_SIZE               16

#define CODEWORD_SIZE 255ul
#define PARITY_BYTES 32ul
#define DATA_CHUNK_SIZE (CODEWORD_SIZE - PARITY_BYTES)

#define SYNC_BYTE           (unsigned char)0xAB
#define ESCAPE_SYNC_BYTE    (unsigned char)0x9F
#define ESCAPE_BYTE         (unsigned char)0x54

#define RS_FIRST_CONSECUTIVE_ROOT   1
#define RS_ROOT_GAP                 1
#define RS_NUM_ROOTS                32

// structure preceding the save file
// this needs to be Base64 encoded before being sent
typedef struct _TRANSMISSION_HEADER
{
    char magic[TRANSMISSION_MAGIC_SIZE]; // magic bytes be SGEX
    unsigned char md5Hash[MD5_HASH_SIZE];
    char saveFilename[MAX_SAVE_FILENAME]; // save filename
    unsigned int saveFileSize;  // size of the file in bytes
    unsigned char saveFileData[0]; // saveFileSize number of bytes of save data
} TRANSMISSION_HEADER, *PTRANSMISSION_HEADER;

extern correct_reed_solomon* g_reedSolomon;

int initializeTransmissionHeader(unsigned char* md5Hash, unsigned int md5HashSize, char* saveFilename, unsigned int saveFileSize);
unsigned int countEscapeBytes(unsigned char* buffer, unsigned int bufferSize);
unsigned int escapeBuffer(unsigned char** buffer, unsigned int* bufferSize);
unsigned int reedSolomonOutSize(unsigned int dataSize);
int reedSolomonEncode(unsigned char* inBuf, unsigned int inSize, unsigned char* outBuf);
