/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2023 Live Networks, Inc.  All rights reserved.
// File sinks
// Implementation

#if (defined(__WIN32__) || defined(_WIN32)) && !defined(_WIN32_WCE)
#include <io.h>
#include <fcntl.h>
#endif
#include "ADTS2PCMFileSink.hh"
#include "GroupsockHelper.hh"
#include "OutputFile.hh"

#include "rAudioStreamerReceiver.h"
#include "speaker.h"
#include "fdk-aac/aacdecoder_lib.h"

////////// ADTS2PCMFileSink //////////

extern int packet_counter;
extern int gpio;
extern int speaker_counter;
extern int debug;

unsigned samplingFrequencyTable[16] = {
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000,
    7350, 0, 0, 0
};

ADTS2PCMFileSink::ADTS2PCMFileSink(UsageEnvironment& env, FILE* fid,
                                   int sampleRate, int numChannels,
                                   unsigned bufferSize)
    : MediaSink(env), fOutFid(fid), fSampleRate(sampleRate),
      fNumChannels(numChannels), fBufferSize(bufferSize),
      fSamePresentationTimeCounter(0), fPacketCounter(0) {

    fBuffer = new unsigned char[bufferSize];
    fPrevPresentationTime.tv_sec = ~0; fPrevPresentationTime.tv_usec = 0;

    int i;
    for (i = 0; i < 16; i++) {
        if (sampleRate == (signed) samplingFrequencyTable[i]) {
            fSampleRateIndex = i;
            break;
        }
    }
    if (i == 16) fSampleRateIndex = 8;

    fChannelConfiguration = fNumChannels;
    if (fChannelConfiguration == 8) fChannelConfiguration--;

    fAACHandle = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (fAACHandle == NULL) {
        fprintf(stderr, "Couldn't open AAC decoder\n");
    }
}

ADTS2PCMFileSink::~ADTS2PCMFileSink() {
    aacDecoder_Close(fAACHandle);
    delete[] fBuffer;
    if (fOutFid != NULL) fclose(fOutFid);
}

ADTS2PCMFileSink* ADTS2PCMFileSink::createNew(UsageEnvironment& env,
                                              char const* fileName,
                                              int sampleRate, int numChannels,
                                              unsigned bufferSize) {
    do {
        FILE* fid;
        fid = OpenOutputFile(env, fileName);
        if (fid == NULL) break;

        return new ADTS2PCMFileSink(env, fid, sampleRate, numChannels, bufferSize);
    } while (0);

    return NULL;
}

Boolean ADTS2PCMFileSink::continuePlaying() {
    if (fSource == NULL) return False;

    fSource->getNextFrame(fBuffer, fBufferSize, afterGettingFrame, this,
                          onSourceClosure, this);

    return True;
}

void ADTS2PCMFileSink::afterGettingFrame(void* clientData, unsigned frameSize,
                                         unsigned numTruncatedBytes,
                                         struct timeval presentationTime,
                                         unsigned /*durationInMicroseconds*/) {

    ADTS2PCMFileSink* sink = (ADTS2PCMFileSink*)clientData;
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
}

void ADTS2PCMFileSink::addData(unsigned char* data, unsigned dataSize,
                               struct timeval presentationTime) {
    // Write to our file:
    if (fOutFid != NULL && data != NULL) {

        unsigned char aacHeader[7];
        unsigned char *aacHeaderPtr = &aacHeader[0];
        unsigned int aacHeaderSize = 7;
        AAC_DECODER_ERROR err;
        unsigned int valid;
        int i;

        dataSize = dataSize + aacHeaderSize;
        aacHeader[0] = 0xFF;
        aacHeader[1] = 0xF1;
        aacHeader[2] = 0x40 | ((fSampleRateIndex << 2) & 0x3c) | ((fChannelConfiguration >> 2) & 0x03);
        aacHeader[3] = ((fChannelConfiguration << 6) & 0xc0) | ((dataSize >> 11) & 0x3);
        aacHeader[4] = (dataSize >> 3) & 0xff;
        aacHeader[5] = ((dataSize << 5) & 0xe0) | 0x1f;
        aacHeader[6] = 0xfc;
        dataSize = dataSize - aacHeaderSize;

        valid = aacHeaderSize;
        err = aacDecoder_Fill(fAACHandle, &aacHeaderPtr, &aacHeaderSize, &valid);
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "Fill failed: %x\n", err);
            return;
        }
        valid = dataSize;
        err = aacDecoder_Fill(fAACHandle, &data, &dataSize, &valid);
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "Fill failed: %x\n", err);
            return;
        }

        err = aacDecoder_DecodeFrame(fAACHandle, fPCMBuffer, 1024, 0);
//        if (err == AAC_DEC_NOT_ENOUGH_BITS)
//            return;
        if (err != AAC_DEC_OK) {
            fprintf(stderr, "Decode failed: %x\n", err);
            return;
        }
        CStreamInfo *info = aacDecoder_GetStreamInfo(fAACHandle);
        if (debug) {
            fprintf(stderr, "Sample Rate: %d\n", info->sampleRate);
            fprintf(stderr, "Frame Size: %d\n", info->frameSize);
            fprintf(stderr, "Num Channels: %d\n", info->numChannels);
        }
        if (packet_counter) {
            fprintf(stderr, "Packet Counter: %d\n", fPacketCounter++);
        }

        if (gpio == 1) {
            speaker(1);
            speaker_counter = 1000; // 1 sec
        }
        if (fSampleRate == info->sampleRate / 2) {
            for (i = 0; i < info->frameSize / 2; i++) {
                fPCMBuffer[i] = fPCMBuffer[2 * i];
            }
            fwrite(fPCMBuffer, sizeof(INT_PCM), info->frameSize / 2, fOutFid);
        } else {
            fwrite(fPCMBuffer, sizeof(INT_PCM), info->frameSize, fOutFid);
        }
    }
}

void ADTS2PCMFileSink::afterGettingFrame(unsigned frameSize,
                                         unsigned numTruncatedBytes,
                                         struct timeval presentationTime) {
    if (numTruncatedBytes > 0) {
        fprintf(stderr, "ADTS2PCMFileSink::afterGettingFrame(): The input frame data was too large for our buffer size (%d)\n", fBufferSize);
        fprintf(stderr, "ADTS2PCMFileSink::afterGettingFrame(): %d bytes of trailing data was dropped!\n", numTruncatedBytes);
        fprintf(stderr, "ADTS2PCMFileSink::afterGettingFrame(): Correct this by increasing the \"bufferSize\" parameter in the \"createNew()\" call to at least %d\n", fBufferSize + numTruncatedBytes);
    }
    addData(fBuffer, frameSize, presentationTime);

    if (fOutFid == NULL || fflush(fOutFid) == EOF) {
        // The output file has closed.  Handle this the same way as if the input source had closed:
        if (fSource != NULL) fSource->stopGettingFrames();
        onSourceClosure();
        return;
    }

    // Then try getting the next frame:
    continuePlaying();
}
