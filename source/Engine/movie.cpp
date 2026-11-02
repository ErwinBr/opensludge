/*
 *  movie.cpp
 *  Sludge Engine
 *
 *  Created by Rikard Peterson on 2011-02-27.
 *  Copyright 2011 SLUDGE Developers. All rights reserved.
 *
 */

#include <SDL3/SDL.h>
#include <math.h>

#include "specialsettings.h"

#include "libwebm/mkvreader.hpp"
#include "mkvparser/mkvparser.h"

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#define interface (&vpx_codec_vp8_dx_algo)

#include "newfatal.h"
#include "timing.h"
#include "capture.h"
#include "graphics.h"
#include "movie.h"
#include "sludger.h"
#include "shaders.h"

#include "sound.h"
#include "vorbis/codec.h"
#define OV_EXCLUDE_STATIC_CALLBACKS
#include "vorbis/vorbisfile.h"
#include "ogg/ogg.h"



extern int specialSettings;


movieStates movieIsPlaying = nothing;

// We have decoded the whole movie. The audio thread reads this too.
SDL_AtomicInt movieIsEnding;

float movieAspect = 1.6;

typedef struct audioBuffers {
	char * buffer;
	unsigned int size;
	audioBuffers *next;
	Uint32 time_ms;
} audioBuffers;

typedef struct audioQueue {
	audioBuffers *first, *last;
	int size;
	SDL_Mutex *mutex;
	SDL_Condition *cond;
} audioQueue;

audioQueue audioQ;

// The audio thread moves the start of the movie to keep the video in time
// with the sound, so we read and write it atomically.
SDL_AtomicU32 movieStartTick;
Uint32 movieCurrentTime;

long long audioNsPerByte;
long long audioNsPlayed;
long long audioNsBuffered;
long long audioBufferLen;
bool movieSoundPlaying = false;
int movieAudioIndex;
gfx::Texture * yTex = NULL;
gfx::Texture * uTex = NULL;
gfx::Texture * vTex = NULL;

typedef struct videoBuffers {
	GLubyte * ytex;
	GLubyte * utex;
	GLubyte * vtex;
	videoBuffers *next;
	GLsizei w, h;
	Uint32 time_ms;
} videoBuffers;

typedef struct videoQueue {
	videoBuffers *first, *last;
	int size;
	SDL_Mutex *mutex;
	SDL_Condition *cond;
} videoQueue;

videoQueue videoQ;


void audio_queue_init(audioQueue *q) {
	memset(q, 0, sizeof(audioQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCondition();
}
int audio_queue_put(audioQueue *q, char *buffer, unsigned int size, long long time_ms) {

	audioBuffers *audioBuf = new audioBuffers;
	if (!audioBuf)
		return -1;
	audioBuf->buffer = buffer;
	audioBuf->next = NULL;
	audioBuf->size = size;
	audioBuf->time_ms = time_ms;

	SDL_LockMutex(q->mutex);

	if (!q->last)
		q->first = audioBuf;
	else
		q->last->next = audioBuf;
	q->last = audioBuf;
	q->size ++;
	SDL_SignalCondition(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}
inline static int audio_queue_get(audioQueue *q, char **buffer)
{
	audioBuffers *audioBuf;
	int ret = 0;

	SDL_LockMutex(q->mutex);

	audioBuf = q->first;
	if (audioBuf) {
		// Synch video timer to audio
		Uint32 tick = (Uint32) engineTicksMS()+100;
		Uint32 startTick = SDL_GetAtomicU32(&movieStartTick);
		if (abs((long int)((tick - startTick)-(audioBuf->time_ms))) > 300) {
			SDL_SetAtomicU32(&movieStartTick, tick - audioBuf->time_ms);
		}
				
		q->first = audioBuf->next;
		if (!q->first)
			q->last = NULL;
		q->size--;
		*buffer = audioBuf->buffer;
		ret = audioBuf->size;
		delete audioBuf;
	} 
	
	SDL_UnlockMutex(q->mutex);

	return ret;
}

void video_queue_init(videoQueue *q) {
	memset(q, 0, sizeof(videoQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCondition();
}
int video_queue_put(videoQueue *q, GLubyte * ytex, 
					GLubyte * utex, 
					GLubyte * vtex,
					GLsizei w, GLsizei h, 
					long long time_ms) {
	
	videoBuffers *videoBuf = new videoBuffers;
	if (!videoBuf)
		return -1;
	videoBuf->ytex = ytex;
	videoBuf->utex = utex;
	videoBuf->vtex = vtex;
	videoBuf->next = NULL;
	videoBuf->w = w;
	videoBuf->h = h;
	videoBuf->time_ms = time_ms;
	
	SDL_LockMutex(q->mutex);
	
	if (!q->last)
		q->first = videoBuf;
	else
		q->last->next = videoBuf;
	q->last = videoBuf;
	q->size ++;
	SDL_SignalCondition(q->cond);
	
	SDL_UnlockMutex(q->mutex);
	return 0;
}
inline static int video_queue_get(videoQueue *q, 
								  GLubyte ** ytex, 
								  GLubyte ** utex, 
								  GLubyte ** vtex,
								  GLsizei *w, GLsizei *h)
{
	videoBuffers *videoBuf;
	int ret = 0;
	
	SDL_LockMutex(q->mutex);
	
	videoBuf = q->first;
	if (videoBuf) {
		q->first = videoBuf->next;
		if (!q->first)
			q->last = NULL;
		q->size--;
		*ytex = videoBuf->ytex;
		*utex = videoBuf->utex;
		*vtex = videoBuf->vtex;
		*w = videoBuf->w;
		*h = videoBuf->h;
		ret = 1;
		delete videoBuf;
	} 
	
	SDL_UnlockMutex(q->mutex);
	
	return ret;
}


static void die_codec(vpx_codec_ctx_t *ctx, const char *s) {
    //const char *detail = vpx_codec_error_detail(ctx);
	fatal (s, vpx_codec_error(ctx));
}


void setMovieViewport()
{
	float realAspect = (float) realWinWidth / realWinHeight;

	int vpHeight, vpWidth, vpOffsetX, vpOffsetY;
	if (realAspect > movieAspect) {
		vpHeight = realWinHeight;
		vpWidth = (int) (realWinHeight * movieAspect);
		vpOffsetY = 0;
		vpOffsetX = (realWinWidth-vpWidth)/2;
	} else {
		vpWidth = realWinWidth;
		vpHeight = (int)((float) realWinWidth / movieAspect);
		vpOffsetY = (realWinHeight-vpHeight)/2;
		vpOffsetX = 0;
	}

	gfx::renderer->setViewport (vpOffsetX, vpOffsetY, vpWidth, vpHeight);

	const GLfloat bPMVMatrix[] =
	{
	2.0f/640.f,          .0,   .0,  .0,
	        .0, -2.0f/400.f,   .0,  .0,
	        .0,          .0, 1.0f,  .0,
	      -1.0,        1.0f,   .0, 1.0f

	};
	for (int i = 0; i < 16; i++)
	{
		aPMVMatrix[i] = bPMVMatrix[i];
	}
}

static uint64_t xiph_lace_value(unsigned char ** np)
{
	uint64_t lace;
	uint64_t value;
	unsigned char * p = *np;

	lace = *p++;
	value = lace;
	while (lace == 255) {
		lace = *p++;
		value += lace;
	}

	*np = p;

	return value;
}

vorbis_dsp_state vorbisDspState;
long long audioChannels;

// send audio to audio device...
unsigned int feedAudio (void *userdata, unsigned char *data, unsigned int length) {
	static char * buffer = NULL;
	static unsigned int bufOffset = 0;
	static unsigned int bufSize = 0;

	unsigned int got = 0;
	int bufLen;

	if (! buffer) {
		bufSize = audio_queue_get(&audioQ, &buffer);
		bufOffset = 0;
		if (bufSize <= 0) {
			bufSize = 0;
			buffer = NULL;
			// After the last sound of the movie, we end the soundtrack.
			// Until then, we play silence while we wait for the decoder.
			if (SDL_GetAtomicInt (&movieIsEnding)) return 0;
			memset (data, 0, length);
			return length;
		}
	}

	if (length > bufSize-bufOffset)
		bufLen = bufSize-bufOffset;
	else
		bufLen = length;

	memcpy(data, buffer+bufOffset, bufLen);

	bufOffset += bufLen;
	length -= bufLen;
	got += bufLen;

	if (bufSize <= bufOffset) {
		delete [] buffer;
		buffer = NULL;
	}
//	fprintf (stderr, "Sending %d bytes of audio.\n", got);
	
	return got;
}

// The first block of a cluster, or NULL.
static const mkvparser::BlockEntry * firstBlockEntry (const mkvparser::Cluster * cluster)
{
	const mkvparser::BlockEntry * entry = NULL;
	if (cluster->GetFirst (entry) < 0) return NULL;
	return entry;
}

// The block after entry in a cluster, or NULL.
static const mkvparser::BlockEntry * nextBlockEntry (const mkvparser::Cluster * cluster, const mkvparser::BlockEntry * entry)
{
	const mkvparser::BlockEntry * next = NULL;
	if (cluster->GetNext (entry, next) < 0) return NULL;
	return next;
}

// What we play. While a movie plays, the main loop calls showMovieFrame in
// each frame instead of drawing the game.
struct movieState {
	int fileNumber;
	MkvReader reader;
	mkvparser::Segment * segment;
	const mkvparser::Tracks * tracks;
	int videoTrack, audioTrack;
	vpx_codec_ctx_t codec;
	ogg_packet oggPacket;
	vorbis_info vorbisInfo;
	vorbis_comment vorbisComment;
	vorbis_block vorbisBlock;
	// The frame of a block that we decode next
	const mkvparser::Cluster * cluster;
	const mkvparser::BlockEntry * blockEntry;
	const mkvparser::Block * block;
	long long trackNum, time_ns;
	int frameCount, frameCounter;
	unsigned char * frame;
	long frameSize;
	// Whether we run the engine at the frame rate of the movie yet
	bool knowsFrameRate;
	// Pausing
	float pauseFade;
	Uint32 pausedAt;
};
static movieState * movie = NULL;

static const GLfloat movieTexCoords[] = {
	0.0, 0.0,
	1.0, 0.0,
	0.0, 1.0,
	1.0, 1.0
};

static const GLfloat movieVertices[] = {
	0.0, 0.0, 0.1,
	640.0, 0.0, 0.1,
	0.0, 400.0, 0.1,
	640.0, 400.0, 0.1
};

// The pause sign: two black bars with two white bars on them.
static const GLfloat pauseVertices[4][12] = {
	{ 7.0, 7.0, 0.1,	17.0, 7.0, 0.1,		7.0, 29.0, 0.1,		17.0, 29.0, 0.1 },
	{ 27.0, 7.0, 0.1,	37.0, 7.0, 0.1,		27.0, 29.0, 0.1,	37.0, 29.0, 0.1 },
	{ 5.0, 5.0, 0.1,	15.0, 5.0, 0.1,		5.0, 27.0, 0.1,		15.0, 27.0, 0.1 },
	{ 25.0, 5.0, 0.1,	35.0, 5.0, 0.1,		25.0, 27.0, 0.1,	35.0, 27.0, 0.1 },
};

// Start on the frames of the block in movie->blockEntry.
static void readBlock ()
{
	movie->block = movie->blockEntry->GetBlock();
	movie->trackNum = movie->block->GetTrackNumber();
	movie->frameCount = movie->block->GetFrameCount();
	movie->time_ns = movie->block->GetTime(movie->cluster);
	movie->frameCounter = 0;
}

// Move to the next block. Returns false at the end of the movie.
static bool nextBlock ()
{
	movie->blockEntry = nextBlockEntry (movie->cluster, movie->blockEntry);
	if ((movie->blockEntry == NULL) || movie->blockEntry->EOS())
	{
		movie->cluster = movie->segment->GetNext(movie->cluster);
		if ((movie->cluster == NULL) || movie->cluster->EOS()) return false;
		movie->blockEntry = firstBlockEntry (movie->cluster);
		if ((movie->blockEntry == NULL) || movie->blockEntry->EOS()) return false;
	}
	readBlock ();
	return true;
}

// Decode the next frame of a block, video or sound, into its queue.
static void decodeFrame ()
{
	if (movie->frameCounter >= movie->frameCount && ! nextBlock ()) {
		SDL_SetAtomicInt (&movieIsEnding, 1);
		return;
	}

	const mkvparser::Block::Frame& theFrame = movie->block->GetFrame(movie->frameCounter);
	const long size = theFrame.len;
	++ movie->frameCounter;

	if( size > movie->frameSize ) {
		delete [] movie->frame;
		movie->frame = new unsigned char[size];
		if (! checkNew(movie->frame)) return;
		movie->frameSize = size;
	}

	if (movie->trackNum == movie->videoTrack) {

		theFrame.Read(&movie->reader, movie->frame);

		/* Decode the frame */
		if(vpx_codec_decode(&movie->codec, movie->frame, size, NULL, 0))
			die_codec(&movie->codec, "Failed to decode frame");

		// Let's decode an image frame!
		vpx_codec_iter_t  iter = NULL;
		vpx_image_t      *img;
		/* Get frame data */
		while((img = vpx_codec_get_frame(&movie->codec, &iter))) {
			if (img->fmt != VPX_IMG_FMT_I420)
				fatal("Movie error. The movie is not in I420 colour format, which is the only one I can hanlde at the moment.");

			unsigned int y;

			GLubyte * ytex = new GLubyte [img->d_w * img->d_h];
			GLubyte * utex = new GLubyte [(img->d_w >> 1) * (img->d_h >> 1)];
			GLubyte * vtex = new GLubyte [(img->d_w >> 1) * (img->d_h >> 1)];
			if (!ytex || !utex || !vtex)
				fatal (ERROR_OUT_OF_MEMORY);

			unsigned char *buf =img->planes[0];
			for(y=0; y<img->d_h; y++) {
				memcpy(ytex+y*img->d_w, buf, img->d_w);
				buf += img->stride[0];
			}
			buf =img->planes[1];
			for(y=0; y<img->d_h >> 1; y++) {
				memcpy(utex+y*(img->d_w>>1), buf, img->d_w>>1);
				buf += img->stride[1];
			}
			buf =img->planes[2];
			for(y=0; y<img->d_h >> 1; y++) {
				memcpy(vtex+y*(img->d_w>>1), buf, img->d_w>>1);
				buf += img->stride[2];
			}

			video_queue_put(&videoQ, ytex, utex, vtex,
							img->d_w, img->d_h, movie->time_ns/1000000);
		}
	} else if (movie->trackNum == movie->audioTrack) {
		// Use this Audio Track
		if( size > 0 ) {
			theFrame.Read(&movie->reader, movie->frame);
			ogg_packet & oggPacket = movie->oggPacket;
			oggPacket.packet = movie->frame;
			oggPacket.bytes = size;
			oggPacket.b_o_s = false;
			oggPacket.packetno++;
			oggPacket.granulepos = -1;
			if( ! vorbis_synthesis(&movie->vorbisBlock, &oggPacket) ) {
				if (vorbis_synthesis_blockin(&vorbisDspState, &movie->vorbisBlock))
					fprintf (stderr, "Vorbis Synthesis block in error.\n");

			} else {
				fprintf (stderr, "Vorbis Synthesis error.\n");
			}

			float **pcm;

			int numSamples = vorbis_synthesis_pcmout(&vorbisDspState, &pcm);

			if (numSamples > 0) {
				long bytespersample=audioChannels*2;

				char * buffer = new char[bytespersample*numSamples];
				if (! checkNew (buffer)) return;

				// We interleave the channels as signed 16-bit
				// samples, rounded to the nearest value.
				for (int i = 0; i < audioChannels; i ++) {
					const float * src = pcm[i];
					short * dest = ((short *) buffer) + i;
					for (int j = 0; j < numSamples; j ++) {
						long val = lrintf (src[j] * 32768.f);
						if (val > 32767) val = 32767;
						else if (val < -32768) val = -32768;
						*dest = (short) val;
						dest += audioChannels;
					}
				}

				vorbis_synthesis_read(&vorbisDspState,numSamples);
				audioBufferLen = bytespersample*numSamples;
				audio_queue_put(&audioQ, buffer, audioBufferLen, movie->time_ns/1000000);

				if (! movieSoundPlaying && size > 1) {
					fprintf (stderr, "** starting sound ** \n");
					playMovieStream (movieAudioIndex);
					movieSoundPlaying = true;
				}
			}
		}
	}
}

int playMovie (int fileNumber)
{
	if (specialSettings & SPECIAL_SILENT)
		return 0;

	if (movieIsPlaying) return 0;

	movieSoundPlaying = false;

    using namespace mkvparser;

	movie = new movieState;
	memset (&movie->codec, 0, sizeof (movie->codec));
	movie->fileNumber = fileNumber;
	movie->segment = NULL;
	movie->frame = NULL;
	movie->pauseFade = 1.0;
	movie->pausedAt = 0;
	movie->knowsFrameRate = false;

    if (movie->reader.Open(fileNumber))
    {
        warning(ERROR_MOVIE_ODDNESS);
        delete movie;
        movie = NULL;
        return 0;
    }

    long long pos = 0;

    EBMLHeader ebmlHeader;

    ebmlHeader.Parse(&movie->reader, pos);

    long long ret = mkvparser::Segment::CreateInstance(&movie->reader, pos, movie->segment);
    if (ret)
    {
        fatal ("Movie error: Segment::CreateInstance() failed.\n");
    }

    ret  = movie->segment->Load();
    if (ret < 0)
    {
        fatal ("Movie error: Segment::Load() failed.\n");
    }

    const mkvparser::Tracks* pTracks = movie->tracks = movie->segment->GetTracks();

    unsigned long i = 0;
    const unsigned long j = pTracks->GetTracksCount();

    enum { VIDEO_TRACK = 1, AUDIO_TRACK = 2 };
	int videoTrack = -1;
	int audioTrack = -1;
	double audioSampleRate;
	ogg_packet & oggPacket = movie->oggPacket;

    while (i != j)
    {
        const Track* const pTrack = pTracks->GetTrackByIndex(i++);

        if (pTrack == NULL)
            continue;

        const long long trackType = pTrack->GetType();

        if (trackType == VIDEO_TRACK && videoTrack < 0)
        {
			videoTrack = pTrack->GetNumber();
            const VideoTrack* const pVideoTrack =
			static_cast<const VideoTrack*>(pTrack);

            const long long width =  pVideoTrack->GetWidth();
            const long long height = pVideoTrack->GetHeight();

            const double rate = pVideoTrack->GetFrameRate();

			// While the movie plays, a frame of the engine is a frame of
			// the movie.
			if (rate > 0) {
				Init_Special_Timer(rate);
				movie->knowsFrameRate = true;
			}

			movieAspect = (float)width/height;
        }

        if (trackType == AUDIO_TRACK && audioTrack < 0)
        {
			audioTrack = pTrack->GetNumber();
            const AudioTrack* const pAudioTrack =
			static_cast<const AudioTrack*>(pTrack);

            audioChannels =  pAudioTrack->GetChannels();
            audioSampleRate = pAudioTrack->GetSamplingRate();

			size_t audioHeaderSize;
			const unsigned char* audioHeader = pAudioTrack->GetCodecPrivate(audioHeaderSize);

			if (audioHeaderSize < 1) {
				warning("Strange audio track in movie.");
				audioTrack = -1;
				continue;
			}

			unsigned char *p = (unsigned char *)audioHeader;

			unsigned int count = *p++ + 1;
			if (count != 3) {
				warning("Strange audio track in movie.");
				audioTrack = -1;
				continue;
			}

			uint64_t sizes[3], total;

			int i = 0;
			total = 0;
			while (--count) {
				sizes[i] = xiph_lace_value(&p);
				total += sizes[i];
				i += 1;
			}
			sizes[i] = audioHeaderSize - total- (p - audioHeader);

			// initialize vorbis
			vorbis_info_init(&movie->vorbisInfo);
			vorbis_comment_init(&movie->vorbisComment);
			memset(&vorbisDspState,0,sizeof(vorbisDspState));
			memset(&movie->vorbisBlock,0,sizeof(movie->vorbisBlock));

			oggPacket.e_o_s = false;
			oggPacket.granulepos = 0;
			oggPacket.packetno = 0;
			int r;
			for(int i=0;i<3;i++)
			{
				oggPacket.packet = p;
				oggPacket.bytes = sizes[i];
				oggPacket.b_o_s = oggPacket.packetno==0;
				r = vorbis_synthesis_headerin(&movie->vorbisInfo, &movie->vorbisComment, &oggPacket);
				if( r )
					fprintf(stderr,"vorbis_synthesis_headerin failed, error: %d", r);
				oggPacket.packetno++;
				p += sizes[i];
			}

			r = vorbis_synthesis_init(&vorbisDspState, &movie->vorbisInfo);
			if( r )
				fprintf(stderr,"vorbis_synthesis_init failed, error: %d", r);
			r = vorbis_block_init(&vorbisDspState, &movie->vorbisBlock);
			if( r )
				fprintf(stderr,"vorbis_block_init failed, error: %d", r);

			movieAudioIndex = initMovieSound(fileNumber, (int) audioChannels, (int) audioSampleRate, feedAudio);
			fprintf (stderr, "Movie sound inited.\n");
			audio_queue_init(&audioQ);
			audioNsPerByte = (1000000000 / audioSampleRate) / (audioChannels * 2);
			audioNsBuffered = 0;
			audioBufferLen = audioChannels*audioSampleRate;
        }
    }

	if (videoTrack < 0)
		fatal ("Movie error: No video in movie file.");

	if (audioTrack < 0 )
		fatal ("Movie error: No sound found.");

	movie->videoTrack = videoTrack;
	movie->audioTrack = audioTrack;

	video_queue_init(&videoQ);

    const unsigned long clusterCount = movie->segment->GetCount();

    if (clusterCount == 0)
    {
        fatal ("Movie error: Segment has no clusters.\n");
    }

	/* Initialize video codec */
    if(vpx_codec_dec_init(&movie->codec, interface, NULL, 0))
        die_codec(&movie->codec, "Failed to initialize decoder for movie.");

    movie->frameSize = 256*1024;
    movie->frame = new unsigned char[movie->frameSize];
	if (! checkNew (movie->frame)) return false;

    movie->cluster = movie->segment->GetFirst();
	movie->blockEntry = firstBlockEntry (movie->cluster);

	if  ((movie->blockEntry == NULL) || movie->blockEntry->EOS())
	{
		movie->cluster = movie->segment->GetNext(movie->cluster);
		if ((movie->cluster == NULL) || movie->cluster->EOS()) {
			fatal("Error: No movie found in the movie file.");
		}
		movie->blockEntry = firstBlockEntry (movie->cluster);
		if ((movie->blockEntry == NULL) || movie->blockEntry->EOS()) {
			fatal("Error: No movie found in the movie file.");
		}
	}
	readBlock ();

	movieIsPlaying = playing;
	SDL_SetAtomicInt (&movieIsEnding, 0);
	SDL_SetAtomicU32(&movieStartTick, (Uint32) engineTicksMS());
	return 1;
}

static void finishMovie ()
{
	movieIsPlaying = nothing;
	huntKillFreeSound(movie->fileNumber);
    if(vpx_codec_destroy(&movie->codec))
        die_codec(&movie->codec, "Failed to destroy codec");
	vorbis_dsp_clear(&vorbisDspState);
	vorbis_block_clear(&movie->vorbisBlock);
	vorbis_comment_clear(&movie->vorbisComment);
	vorbis_info_clear(&movie->vorbisInfo);
    delete movie->segment;
	gfx::renderer->destroyTexture (yTex);
	gfx::renderer->destroyTexture (uTex);
	gfx::renderer->destroyTexture (vTex);
	yTex = uTex = vTex = NULL;

	// Delete any remaining buffers
	videoBuffers *vB = videoQ.first;
	while ((vB = videoQ.first)) {
		videoQ.first = vB->next;
		delete [] vB->ytex;
		delete [] vB->utex;
		delete [] vB->vtex;
		delete vB;
	}
	videoQ.size = 0;

	audioBuffers *aB = audioQ.first;
	while ((aB = audioQ.first)) {
		audioQ.first = aB->next;
		delete [] aB->buffer;
		delete aB;
	}
	audioQ.size = 0;

	delete [] movie->frame;
	SDL_DestroyCondition(videoQ.cond);
	SDL_DestroyMutex(videoQ.mutex);
	SDL_DestroyCondition(audioQ.cond);
	SDL_DestroyMutex(audioQ.mutex);
	videoQ.cond = audioQ.cond = NULL;
	videoQ.mutex = audioQ.mutex = NULL;
	videoQ.last = NULL;
	audioQ.last = NULL;

	delete movie;
	movie = NULL;

	Init_Timer();
	drawIntoWindow ();
	setPixelCoords (false);
}

// We decode this far ahead of the time of the movie, so that the sound
// never runs out between two frames of the engine.
#define MOVIE_DECODE_AHEAD_MS 500

bool showMovieFrame ()
{
	if (! movie) return false;
	if (movieIsPlaying == nothing) {
		// stopMovie stopped it.
		finishMovie ();
		return false;
	}

	if (movieIsPlaying == playing) {
		Uint32 now = (Uint32) engineTicksMS() - SDL_GetAtomicU32(&movieStartTick);

		while (! SDL_GetAtomicInt (&movieIsEnding) && (videoQ.size < 100 || audioQ.size < 100) &&
				(! videoQ.last || videoQ.last->time_ms <= now + MOVIE_DECODE_AHEAD_MS)) {
			decodeFrame ();
		}

		// Many WebM files don't say their frame rate, so we take it from
		// the times of the first two frames.
		if (! movie->knowsFrameRate && videoQ.first && videoQ.first->next) {
			Uint32 duration = videoQ.first->next->time_ms - videoQ.first->time_ms;
			if (duration > 0) Init_Special_Timer (1000.0 / duration);
			movie->knowsFrameRate = true;
		}

		// We show the last frame that is due, and skip those that we are
		// too late for.
		GLubyte * ytex = NULL, * utex = NULL, * vtex = NULL;
		GLsizei w = 0, h = 0;
		while (videoQ.first && videoQ.first->time_ms <= now + 1) {
			delete [] ytex;
			delete [] utex;
			delete [] vtex;
			video_queue_get(&videoQ, &ytex, &utex, &vtex, &w, &h);
		}
		if (ytex) {
			// One channel for each of Y, U and V; U and V have half the
			// width and height.
			gfx::Texture * * planes[3] = { &yTex, &uTex, &vTex };
			GLubyte * data[3] = { ytex, utex, vtex };
			for (int i = 0; i < 3; i ++) {
				int pw = i ? w>>1 : w;
				int ph = i ? h>>1 : h;
				if (* planes[i]) {
					gfx::renderer->setTextureImage (* planes[i], pw, ph, data[i]);
				} else {
					* planes[i] = gfx::renderer->createTexture (pw, ph, gfx::TEX_R8, data[i]);
				}
				gfx::renderer->setTextureFilter (* planes[i], gfx::FILTER_LINEAR, gfx::FILTER_LINEAR);
			}
			delete [] ytex;
			delete [] utex;
			delete [] vtex;
		} else if (! videoQ.first && SDL_GetAtomicInt (&movieIsEnding) && ! stillPlayingSound (movieAudioIndex)) {
			// We have shown the last frame, and played the sound.
			finishMovie ();
			return false;
		}
	}

	// The movie draws straight into the window, with a viewport of its own.
	drawIntoWindow ();
	setMovieViewport ();
	gfx::renderer->clear (true, false);
	if (yTex) {
		gfx::Texture * const planes[3] = { yTex, uTex, vTex };
		gfx::Quad quad = { movieVertices, { movieTexCoords, NULL, NULL } };
		gfx::renderer->draw (gfx::PipelineState (gfx::PROG_YUV, gfx::BLEND_ALPHA), engineUniforms (), planes, quad);
	}

	if (movieIsPlaying == paused) {
		movie->pauseFade -= 1.0 / 24;
		if (movie->pauseFade < -1.0) movie->pauseFade = 1.0;

		gfx::PipelineState state (gfx::PROG_COLOR, gfx::BLEND_ALPHA);
		for (int i = 0; i < 4; i ++) {
			float c = i < 2 ? 0.0f : 1.0f;
			setPrimaryColor (c, c, c, fabs (movie->pauseFade));
			gfx::draw (state, engineUniforms (), pauseVertices[i]);
		}
	}
	showFrame ();
	return true;
}

int stopMovie ()
{
	int r = movieIsPlaying;
	movieIsPlaying = nothing;
	return r;
}

int pauseMovie()
{
	if (movieIsPlaying == playing) {
		pauseMovieSound(movieAudioIndex, true);
		movie->pausedAt = (Uint32) engineTicksMS();
		movieIsPlaying = paused;
		fprintf (stderr, "** Pausing **\n");
	} else if (movieIsPlaying == paused) {
		pauseMovieSound(movieAudioIndex, false);
		// The movie goes on where we paused it.
		SDL_SetAtomicU32 (&movieStartTick, SDL_GetAtomicU32 (&movieStartTick) + (Uint32) engineTicksMS() - movie->pausedAt);
		fprintf (stderr, "** Restarted movie ** sound: %d\n", movieSoundPlaying);
		movieIsPlaying = playing;
	}
	return movieIsPlaying;
}
