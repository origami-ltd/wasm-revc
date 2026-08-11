#ifdef __EMSCRIPTEN__
#include "common.h"
#include "crossplatform.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "CdStream.h"
#include "rwcore.h"
#include "MemoryMgr.h"

/*
 * Browser CD streaming: same contract as the POSIX version, no threads.
 *
 * The POSIX build hands each request to a worker thread and blocks the caller on a semaphore
 * until it lands. Neither half of that survives here — the page is single-threaded, and the
 * main thread is the one thread that must never block on another.
 *
 * It does not need to. The archive is mounted as a custom emscripten FS node whose read is
 * already synchronous underneath: a pre-warmed worker pulls chunks into a SharedArrayBuffer
 * while this thread spin-waits (see packages/runtime ArchiveStreamer). So read() below already
 * behaves like the blocking read the game expects, and the whole request/queue/semaphore dance
 * collapses into doing the read inline.
 *
 * Status values follow the original exactly, including the surprise that a *successful* read
 * leaves nStatus as STREAM_NONE — CStreaming treats anything else as a failure.
 */

struct CdReadInfo
{
	uint32 nSectorOffset;
	uint32 nSectorsToRead;
	void *pBuffer;
	bool bLocked;
	bool bReading;
	int32 nStatus;
	int32 hFile;
};

char gCdImageNames[MAX_CDIMAGES+1][64];
int32 gNumImages;
int32 gNumChannels;

int32 gImgFiles[MAX_CDIMAGES]; // -1: error 0:unused otherwise: fd
char *gImgNames[MAX_CDIMAGES];

CdReadInfo *gpReadInfo;

int32 lastPosnRead;

int _gdwCdStreamFlags;

#ifdef FLUSHABLE_STREAMING
bool flushStream[MAX_CDCHANNELS];
#endif

void
CdStreamInitThread(void)
{
	// No thread to start.
}

void
CdStreamInit(int32 numChannels)
{
	_gdwCdStreamFlags = O_RDONLY;

	gNumImages = 0;
	gNumChannels = numChannels;
	ASSERT( gNumChannels != 0 );

	gpReadInfo = (CdReadInfo *)calloc(numChannels, sizeof(CdReadInfo));
	ASSERT( gpReadInfo != nil );

	CdStreamInitThread();
}

uint32
GetGTA3ImgSize(void)
{
	ASSERT( gImgFiles[0] > 0 );
	struct stat statbuf;

	if (stat(gImgNames[0], &statbuf) == -1) {
		// Try case-insensitivity
		char* real = casepath(gImgNames[0], false);
		if (real)
		{
			int found = stat(real, &statbuf);
			free(real);
			if (found != -1)
				return (uint32)statbuf.st_size;
		}
		debug("cannot get size of gta3.img\n");
		ASSERT(0);
		return 0;
	}
	return (uint32)statbuf.st_size;
}

int32
CdStreamRead(int32 channel, void *buffer, uint32 offset, uint32 size)
{
	ASSERT( channel < gNumChannels );
	ASSERT( buffer != nil );

	lastPosnRead = size + offset;

	ASSERT( _GET_INDEX(offset) < MAX_CDIMAGES );
	int32 hImage = gImgFiles[_GET_INDEX(offset)];
	ASSERT( hImage > 0 );

	CdReadInfo *pChannel = &gpReadInfo[channel];
	ASSERT( pChannel != nil );

	pChannel->hFile = hImage - 1;
	pChannel->nStatus = STREAM_NONE;
	pChannel->nSectorOffset = _GET_OFFSET(offset);
	pChannel->nSectorsToRead = size;
	pChannel->pBuffer = buffer;
	pChannel->bLocked = 0;
	pChannel->bReading = true;

	// The read the worker thread would have done, done here.
	lseek(pChannel->hFile, (size_t)pChannel->nSectorOffset * (size_t)CDSTREAM_SECTOR_SIZE, SEEK_SET);
	if (read(pChannel->hFile, pChannel->pBuffer, pChannel->nSectorsToRead * CDSTREAM_SECTOR_SIZE) == -1)
		pChannel->nStatus = STREAM_ERROR;
	else
		pChannel->nStatus = STREAM_NONE; // success — see the note at the top

	pChannel->nSectorsToRead = 0;
	pChannel->bReading = false;

	return STREAM_SUCCESS;
}

int32
CdStreamGetStatus(int32 channel)
{
	ASSERT( channel < gNumChannels );
	CdReadInfo *pChannel = &gpReadInfo[channel];
	ASSERT( pChannel != nil );

	if ( pChannel->bReading )
		return STREAM_READING;

	if ( pChannel->nSectorsToRead != 0 )
		return STREAM_WAITING;

	if ( pChannel->nStatus != STREAM_NONE )
	{
		int32 status = pChannel->nStatus;
		pChannel->nStatus = STREAM_NONE;

		return status;
	}

	return STREAM_NONE;
}

int32
CdStreamGetLastPosn(void)
{
	return lastPosnRead;
}

int32
CdStreamSync(int32 channel)
{
	ASSERT( channel < gNumChannels );
	CdReadInfo *pChannel = &gpReadInfo[channel];
	ASSERT( pChannel != nil );

	// Nothing is ever in flight: CdStreamRead completed the read before it returned.
#ifdef FLUSHABLE_STREAMING
	if (flushStream[channel]) {
		pChannel->nSectorsToRead = 0;
		pChannel->bReading = false;
		flushStream[channel] = false;
		return STREAM_NONE;
	}
#endif

	pChannel->bReading = false;

	return pChannel->nStatus;
}

void
AddToQueue(Queue *queue, int32 item)
{
	ASSERT( queue != nil );
	ASSERT( queue->items != nil );
	queue->items[queue->tail] = item;

	queue->tail = (queue->tail + 1) % queue->size;

	if ( queue->head == queue->tail )
		queue->head = (queue->head + 1) % queue->size;
}

int32
GetFirstInQueue(Queue *queue)
{
	ASSERT( queue != nil );
	if ( queue->head == queue->tail )
		return -1;

	ASSERT( queue->items != nil );
	return queue->items[queue->head];
}

void
RemoveFirstInQueue(Queue *queue)
{
	ASSERT( queue != nil );
	if ( queue->head == queue->tail )
	{
		ASSERT(queue->head != queue->tail);
		return;
	}

	queue->head = (queue->head + 1) % queue->size;
}

void
CdStreamShutdown(void)
{
	if (gpReadInfo)
		free(gpReadInfo);
	gpReadInfo = nil;
}

bool
CdStreamAddImage(char const *path)
{
	ASSERT(path != nil);
	ASSERT(gNumImages < MAX_CDIMAGES);

	gImgFiles[gNumImages] = open(path, _gdwCdStreamFlags);

	// Fix case sensitivity and backslashes.
	if (gImgFiles[gNumImages] == -1) {
		char* real = casepath(path, false);
		if (real)
		{
			gImgFiles[gNumImages] = open(real, _gdwCdStreamFlags);
			free(real);
		}
	}

	if ( gImgFiles[gNumImages] == -1 )
		return false;

	gImgNames[gNumImages] = strdup(path);
	gImgFiles[gNumImages]++; // because -1: error 0: not used

	strcpy(gCdImageNames[gNumImages], path);

	gNumImages++;

	return true;
}

char *
CdStreamGetImageName(int32 cd)
{
	ASSERT(cd < MAX_CDIMAGES);
	if ( gImgFiles[cd] > 0)
		return gCdImageNames[cd];

	return nil;
}

void
CdStreamRemoveImages(void)
{
	for ( int32 i = 0; i < gNumChannels; i++ ) {
#ifdef FLUSHABLE_STREAMING
		flushStream[i] = 1;
#endif
		CdStreamSync(i);
	}

	for ( int32 i = 0; i < gNumImages; i++ )
	{
		close(gImgFiles[i] - 1);
		free(gImgNames[i]);
		gImgFiles[i] = 0;
	}

	gNumImages = 0;
}

int32
CdStreamGetNumImages(void)
{
	return gNumImages;
}

#endif // __EMSCRIPTEN__
