/*
 * Copyright (c) 2024 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * AAC streamer/receiver.
 * From /dev/shm/fshare_frame_buffer to a RTP host.
 */

#ifndef _R_RTSP_SERVER_H
#define _R_RTSP_SERVER_H

//#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <getopt.h>

#define SAMPLING_FREQ 16000
#define NUM_CHANNELS 1

#define BUFFER_FILE_MSTAR "/dev/fshare_frame_buf"
#define BUFFER_FILE_ALLWINNER "/dev/shm/fshare_frame_buf"
#define BUFFER_SHM "fshare_frame_buf"
#define READ_LOCK_FILE "fshare_read_lock"
#define WRITE_LOCK_FILE "fshare_write_lock"

#define Y203C 200
#define Y23 201
#define Y25 202
#define Y30 203
#define H201C 204
#define H305R 205
#define H307 206

#define Y20GA 300
#define Y25GA 301
#define Y30QA 302
#define Y501GC 303

#define Y21GA 400
#define Y211GA 401
#define Y213GA 402
#define Y291GA 403
#define H30GA 404
#define R30GB 405
#define R35GB 406
#define R40GA 407
#define H51GA 408
#define H52GA 409
#define H60GA 410
#define Y28GA 411
#define Y29GA 412
#define Y623 413
#define Q321BR_LSX 414
#define QG311R 415
#define B091QP 416

#define BUF_OFFSET_MSTAR 230 //228
#define FRAME_HEADER_SIZE_MSTAR 19

#define BUF_OFFSET_Y20GA 300
#define FRAME_HEADER_SIZE_Y20GA 22

#define BUF_OFFSET_Y25GA 300
#define FRAME_HEADER_SIZE_Y25GA 22

#define BUF_OFFSET_Y30QA 300
#define FRAME_HEADER_SIZE_Y30QA 22

#define BUF_OFFSET_Y501GC 368
#define FRAME_HEADER_SIZE_Y501GC 24

#define BUF_OFFSET_Y21GA 368
#define FRAME_HEADER_SIZE_Y21GA 28

#define BUF_OFFSET_Y211GA 368
#define FRAME_HEADER_SIZE_Y211GA 28

#define BUF_OFFSET_Y213GA 368
#define FRAME_HEADER_SIZE_Y213GA 28

#define BUF_OFFSET_Y291GA 368
#define FRAME_HEADER_SIZE_Y291GA 28

#define BUF_OFFSET_H30GA 368
#define FRAME_HEADER_SIZE_H30GA 28

#define BUF_OFFSET_R30GB 300
#define FRAME_HEADER_SIZE_R30GB 22

#define BUF_OFFSET_R35GB 300
#define FRAME_HEADER_SIZE_R35GB 26

#define BUF_OFFSET_R40GA 300
#define FRAME_HEADER_SIZE_R40GA 26

#define BUF_OFFSET_H51GA 368
#define FRAME_HEADER_SIZE_H51GA 28

#define BUF_OFFSET_H52GA 368
#define FRAME_HEADER_SIZE_H52GA 28

#define BUF_OFFSET_H60GA 368
#define FRAME_HEADER_SIZE_H60GA 28

#define BUF_OFFSET_Y28GA 368
#define FRAME_HEADER_SIZE_Y28GA 28

#define BUF_OFFSET_Y29GA 368
#define FRAME_HEADER_SIZE_Y29GA 28

#define BUF_OFFSET_Y623 368
#define FRAME_HEADER_SIZE_Y623 28

#define BUF_OFFSET_Q321BR_LSX 300
#define FRAME_HEADER_SIZE_Q321BR_LSX 26

#define BUF_OFFSET_QG311R 300
#define FRAME_HEADER_SIZE_QG311R 26

#define BUF_OFFSET_B091QP 300
#define FRAME_HEADER_SIZE_B091QP 26

#define MILLIS_10 10000
#define MILLIS_25 25000

#define TYPE_NONE 0
#define TYPE_AAC 65521

#define OUTPUT_BUFFER_SIZE_AUDIO 32768

typedef struct
{
    unsigned char *buffer;                  // pointer to the base of the input buffer
    char filename[256];                     // name of the buffer file
    unsigned int size;                      // size of the buffer file
    unsigned int offset;                    // offset where stream starts
    unsigned char *read_index;              // read absolute index
} cb_input_buffer;

// Frame position inside the output buffer, needed to use DiscreteFramer instead Framer.
typedef struct
{
    unsigned char *ptr;                     // pointer to the frame start
    unsigned int counter;                   // frame counter
    unsigned int size;                      // frame size
} cb_output_frame;

typedef struct
{
    unsigned char *buffer;                  // pointer to the base of the output buffer
    unsigned int size;                      // size of the output buffer
    int type;                               // type of the stream in this buffer
    unsigned char *write_index;             // write absolute index
    cb_output_frame output_frame[42];       // array of frames that buffer contains 42 = SPS + PPS + iframe + GOP
    int output_frame_size;                  // number of frames that buffer contains
    unsigned int frame_read_index;          // index of the next frame to read
    unsigned int frame_write_index;         // index of the next frame to write
    pthread_mutex_t mutex;                  // mutex of the structure
} cb_output_buffer;

struct __attribute__((__packed__)) frame_header {
    uint32_t len;
    uint32_t counter;
    uint32_t time;
    uint16_t type;
    uint16_t stream_counter;
};

struct __attribute__((__packed__)) frame_header_19 {
    uint32_t len;
    uint16_t counter;
    uint16_t type;
    uint32_t u1;
    uint32_t time;
    uint16_t stream_counter;
    uint8_t u2;
};
struct __attribute__((__packed__)) frame_header_22 {
    uint32_t len;
    uint32_t counter;
    uint32_t u1;
    uint32_t time;
    uint16_t type;
    uint16_t stream_counter;
    uint16_t u4;
};

struct __attribute__((__packed__)) frame_header_24 {
    uint32_t len;
    uint32_t counter;
    uint32_t u1;
    uint32_t time;
    uint16_t type;
    uint16_t stream_counter;
    uint16_t u4;
    uint16_t u5;
};

struct __attribute__((__packed__)) frame_header_26 {
    uint32_t len;
    uint32_t counter;
    uint32_t u1;
    uint32_t u2;
    uint32_t time;
    uint16_t type;
    uint16_t stream_counter;
    uint16_t u4;
};

struct __attribute__((__packed__)) frame_header_28 {
    uint32_t len;
    uint32_t counter;
    uint32_t u1;
    uint32_t u2;
    uint32_t time;
    uint16_t type;
    uint16_t stream_counter;
    uint32_t u4;
};

long long current_timestamp();

#endif
