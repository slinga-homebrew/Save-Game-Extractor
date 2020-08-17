#pragma once

#include <jo/jo.h>

#define UNUSED(x) (void)(x)

// return values for SaturnMinimodem_transfer
#define TRANSFER_ERROR   -1
#define TRANSFER_PROGRESS 1
#define TRANSFER_COMPLETE 2
#define TRANSFER_BUSY     3

// Saturn minimodem API
int SaturnMinimodem_init(void);
int SaturnMinimodem_initTransfer(unsigned char* data, unsigned int size);
int SaturnMinimodem_transfer(void);
int SaturnMinimodem_transferStatus(unsigned int* bytesTransmitted, unsigned int* bytesTotal);

// BUGBUG: move this prototype somewhere else;
bool sa_saturn_is_buffer_flushed(void);

// missing function prototypes
void bzero(void *s, unsigned int n); // bugbug get rid of this
void *memcpy(void *dest, const void *src, unsigned int n);
int strlen(const char *s);

// reimplemented floating point functions
float sinf(float x);
long int lroundf(float x);
float fmodf(float x, float y);

void* my_realloc(void* buffer, unsigned int oldSize, unsigned int newSize);

// bugbug put this prototype somewhere else
bool sa_saturn_flush_buffer();
