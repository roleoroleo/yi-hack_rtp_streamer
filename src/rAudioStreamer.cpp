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
 * Dump aac content from /dev/shm/fshare_frame_buffer and copy it to
 * a circular buffer.
 * Then send the circular buffer to a RTP host.
 */

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

#include "AudioFramedMemorySource.hh"

#include "rAudioStreamerReceiver.h"

#include <getopt.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
    FramedSource* source;
    RTPSink* sink;
    RTCPInstance* rtcpInstance;
    Groupsock* rtpGroupsock;
    Groupsock* rtcpGroupsock;
} sessionState;

Boolean isSSM;

int buf_offset;
int buf_size;
int frame_header_size;

int packet_counter;
int debug;                                  /* Set to 1 to debug this .c */
int model;
int freq;
int chan;

extern unsigned const samplingFrequencyTable[16];

cb_input_buffer input_buffer;
cb_output_buffer output_buffer_audio;

UsageEnvironment* env;

// To make the second and subsequent client for each stream reuse the same
// input stream as the first client (rather than playing the file from the
// start for each client), change the following "False" to "True":
Boolean reuseFirstSource = True;

void play(); // forward
void afterPlaying(void* clientData); // forward

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds

    return milliseconds;
}

void s2cb_memcpy(cb_output_buffer *dest, unsigned char *src, size_t n)
{
    unsigned char *uc_dest = dest->write_index;

    if (uc_dest + n > dest->buffer + dest->size) {
        memcpy(uc_dest, src, dest->buffer + dest->size - uc_dest);
        memcpy(dest->buffer, src + (dest->buffer + dest->size - uc_dest), n - (dest->buffer + dest->size - uc_dest));
        dest->write_index = n + uc_dest - dest->size;
    } else {
        memcpy(uc_dest, src, n);
        dest->write_index += n;
    }
    if (dest->write_index == dest->buffer + dest->size) {
        dest->write_index = dest->buffer;
    }
}

void cb2cb_memcpy(cb_output_buffer *dest, cb_input_buffer *src, size_t n)
{
    unsigned char *uc_src = src->read_index;

    if (uc_src + n > src->buffer + src->size) {
        s2cb_memcpy(dest, uc_src, src->buffer + src->size - uc_src);
        s2cb_memcpy(dest, src->buffer + src->offset, n - (src->buffer + src->size - uc_src));
        src->read_index = src->offset + n + uc_src - src->size;
    } else {
        s2cb_memcpy(dest, uc_src, n);
        src->read_index += n;
    }
}

/* Locate a string in the circular buffer */
unsigned char *cb_memmem(unsigned char *src, int src_len, unsigned char *what, int what_len)
{
    unsigned char *p;

    if (src_len >= 0) {
        p = (unsigned char*) memmem(src, src_len, what, what_len);
    } else {
        // From src to the end of the buffer
        p = (unsigned char*) memmem(src, input_buffer.buffer + input_buffer.size - src, what, what_len);
        if (p == NULL) {
            // And from the start of the buffer size src_len
            p = (unsigned char*) memmem(input_buffer.buffer + input_buffer.offset, src + src_len - (input_buffer.buffer + input_buffer.offset), what, what_len);
        }
    }
    return p;
}

unsigned char *cb_move(unsigned char *buf, int offset)
{
    buf += offset;
    if ((offset > 0) && (buf > input_buffer.buffer + input_buffer.size))
        buf -= (input_buffer.size - input_buffer.offset);
    if ((offset < 0) && (buf < input_buffer.buffer + input_buffer.offset))
        buf += (input_buffer.size - input_buffer.offset);

    return buf;
}

// The second argument is the circular buffer
int cb_memcmp(unsigned char *str1, unsigned char *str2, size_t n)
{
    int ret;

    if (str2 + n > input_buffer.buffer + input_buffer.size) {
        ret = memcmp(str1, str2, input_buffer.buffer + input_buffer.size - str2);
        if (ret != 0) return ret;
        ret = memcmp(str1 + (input_buffer.buffer + input_buffer.size - str2), input_buffer.buffer + input_buffer.offset, n - (input_buffer.buffer + input_buffer.size - str2));
    } else {
        ret = memcmp(str1, str2, n);
    }

    return ret;
}

// The second argument is the circular buffer
void cb2s_memcpy(unsigned char *dest, unsigned char *src, size_t n)
{
    if (src + n > input_buffer.buffer + input_buffer.size) {
        memcpy(dest, src, input_buffer.buffer + input_buffer.size - src);
        memcpy(dest + (input_buffer.buffer + input_buffer.size - src), input_buffer.buffer + input_buffer.offset, n - (input_buffer.buffer + input_buffer.size - src));
    } else {
        memcpy(dest, src, n);
    }
}

// The second argument is the circular buffer
void cb2s_headercpy(unsigned char *dest, unsigned char *src, size_t n)
{
    struct frame_header *fh = (struct frame_header *) dest;
    struct frame_header_19 fh19;
    struct frame_header_22 fh22;
    struct frame_header_24 fh24;
    struct frame_header_26 fh26;
    struct frame_header_28 fh28;
    unsigned char *fp = NULL;

    if (n == sizeof(fh19)) {
        fp = (unsigned char *) &fh19;
    } else if (n == sizeof(fh22)) {
        fp = (unsigned char *) &fh22;
    } else if (n == sizeof(fh24)) {
        fp = (unsigned char *) &fh24;
    } else if (n == sizeof(fh26)) {
        fp = (unsigned char *) &fh26;
    } else if (n == sizeof(fh28)) {
        fp = (unsigned char *) &fh28;
    }
    if (fp == NULL) return;

    if (src + n > input_buffer.buffer + input_buffer.size) {
        memcpy(fp, src, input_buffer.buffer + input_buffer.size - src);
        memcpy(fp + (input_buffer.buffer + input_buffer.size - src), input_buffer.buffer + input_buffer.offset, n - (input_buffer.buffer + input_buffer.size - src));
    } else {
        memcpy(fp, src, n);
    }
    if (n == sizeof(fh19)) {
        fh->len = fh19.len;
        fh->counter = (uint32_t) fh19.counter;
        fh->time = fh19.time;
        fh->type = fh19.type;
        fh->stream_counter = fh19.stream_counter;
    } else if (n == sizeof(fh22)) {
        fh->len = fh22.len;
        fh->counter = fh22.counter;
        fh->time = fh22.time;
        fh->type = fh22.type;
        fh->stream_counter = fh22.stream_counter;
    } else if (n == sizeof(fh24)) {
        fh->len = fh24.len;
        fh->counter = fh24.counter;
        fh->time = fh24.time;
        fh->type = fh24.type;
        fh->stream_counter = fh24.stream_counter;
    } else if (n == sizeof(fh26)) {
        fh->len = fh26.len;
        fh->counter = fh26.counter;
        fh->time = fh26.time;
        fh->type = fh26.type;
        fh->stream_counter = fh26.stream_counter;
    } else if (n == sizeof(fh28)) {
        fh->len = fh28.len;
        fh->counter = fh28.counter;
        fh->time = fh28.time;
        fh->type = fh28.type;
        fh->stream_counter = fh28.stream_counter;
    }
}

void getAACConfigStr(char *configStr, unsigned samplingFrequency, unsigned numChannels)
{
    unsigned samplingFrequencyTable[16] = {
        96000, 88200, 64000, 48000,
        44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000,
        7350, 0, 0, 0
    };

    u_int8_t samplingFrequencyIndex;
    int i;

    for (i = 0; i < 16; i++) {
        if (samplingFrequency == samplingFrequencyTable[i]) {
            samplingFrequencyIndex = i;
            break;
        }
    }
    if (i == 16) samplingFrequencyIndex = 8;

    u_int8_t channelConfiguration = numChannels;
    if (channelConfiguration == 8) channelConfiguration--;

    // Construct the 'AudioSpecificConfig', and from it, the corresponding ASCII string:
    unsigned char audioSpecificConfig[2];
    u_int8_t const audioObjectType = 2;
    audioSpecificConfig[0] = (audioObjectType<<3) | (samplingFrequencyIndex>>1);
    audioSpecificConfig[1] = (samplingFrequencyIndex<<7) | (channelConfiguration<<3);
    sprintf(configStr, "%02X%02X", audioSpecificConfig[0], audioSpecificConfig[1]);
    if (debug) fprintf(stderr, "%lld: configStr %s\n", current_timestamp(), configStr);
}

void *capture(void *ptr)
{
    unsigned char *buf_idx, *buf_idx_cur, *buf_idx_end, *buf_idx_end_prev;
    unsigned char *buf_idx_start = NULL;
    int fshm;

    int frame_type = TYPE_NONE;
    int frame_len = 0;
    int frame_counter = -1;
    int frame_counter_last_valid_audio = -1;

    int i, n;
    cb_output_buffer *cb_current;
    int write_enable = 0;
    int frame_sync = 0;

    struct frame_header fhs[10];
    unsigned char* fhs_addr[10];
    uint32_t last_counter;

    // Opening an existing file
    fshm = shm_open(input_buffer.filename, O_RDWR, 0);
    if (fshm == -1) {
        fprintf(stderr, "error - could not open file %s\n", input_buffer.filename) ;
        exit(EXIT_FAILURE);
    }

    // Map file to memory
    input_buffer.buffer = (unsigned char*) mmap(NULL, input_buffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, fshm, 0);
    if (input_buffer.buffer == MAP_FAILED) {
        fprintf(stderr, "%lld: capture - error - mapping file %s\n", current_timestamp(), input_buffer.filename);
        close(fshm);
        exit(EXIT_FAILURE);
    }
    if (debug) fprintf(stderr, "%lld: capture - mapping file %s, size %d, to %08x\n", current_timestamp(), input_buffer.filename, input_buffer.size, (unsigned int) input_buffer.buffer);

    // Closing the file
    if (debug) fprintf(stderr, "%lld: capture - closing the file %s\n", current_timestamp(), input_buffer.filename);
    close(fshm) ;

    memcpy(&i, input_buffer.buffer + 16, sizeof(i));
    buf_idx = input_buffer.buffer + input_buffer.offset + i;
    buf_idx_cur = buf_idx;
    memcpy(&i, input_buffer.buffer + 4, sizeof(i));
    buf_idx_end = buf_idx + i;
    if (buf_idx_end >= input_buffer.buffer + input_buffer.size) buf_idx_end -= (input_buffer.size - input_buffer.offset);
    buf_idx_end_prev = buf_idx_end;
    last_counter = 0;

    if (debug) fprintf(stderr, "%lld: capture - starting capture main loop\n", current_timestamp());

    // Infinite loop
    while (1) {
        memcpy(&i, input_buffer.buffer + 16, sizeof(i));
        buf_idx = input_buffer.buffer + input_buffer.offset + i;
        memcpy(&i, input_buffer.buffer + 4, sizeof(i));
        buf_idx_end = buf_idx + i;
        if (buf_idx_end >= input_buffer.buffer + input_buffer.size) buf_idx_end -= (input_buffer.size - input_buffer.offset);
        // Check if the header is ok
        memcpy(&i, input_buffer.buffer + 12, sizeof(i));
        if (buf_idx_end != input_buffer.buffer + input_buffer.offset + i) {
            if (debug) fprintf(stderr, "%lld: capture - buf_idx_end != input_buffer.buffer + input_buffer.offset + i\n", current_timestamp());
            usleep(1000);
            continue;
        }

        if (buf_idx_end == buf_idx_end_prev) {
            if (debug) fprintf(stderr, "%lld: capture - buf_idx_end == buf_idx_end_prev\n", current_timestamp());
            usleep(10000);
            continue;
        }

        buf_idx_cur = buf_idx_end_prev;
        frame_sync = 1;
        i = 0;

        while (buf_idx_cur != buf_idx_end) {
            cb2s_headercpy((unsigned char *) &fhs[i], buf_idx_cur, frame_header_size);
            // Check the len
            if (fhs[i].len > input_buffer.size - input_buffer.offset) {
                frame_sync = 0;
                if (debug) fprintf(stderr, "%lld: capture - fhs[i].len > input_buffer.size - input_buffer.offset\n", current_timestamp());
                break;
            }
            fhs_addr[i] = buf_idx_cur;
            buf_idx_cur = cb_move(buf_idx_cur, fhs[i].len + frame_header_size);
            i++;
            // Check if the sync is lost
            if (i == 10) {
                frame_sync = 0;
                if (debug) fprintf(stderr, "%lld: capture - i=10\n", current_timestamp());
                break;
            }
        }

        if (frame_sync == 0) {
            buf_idx_end_prev = buf_idx_end;
            if (debug) fprintf(stderr, "%lld: capture - frame_sync == 0\n", current_timestamp());
            usleep(10000);
            continue;
        }

        n = i;
        // Ignore last frame, it could be corrupted
        if (n > 1) {
            buf_idx_end_prev = fhs_addr[n - 1];
            n--;
        } else {
            if (debug) fprintf(stderr, "%lld: capture - ! n > 1\n", current_timestamp());
            usleep(10000);
            continue;
        }

        if (n > 0) {
            if (fhs[0].counter != last_counter + 1) {
                fprintf(stderr, "%lld: capture - warning - %d frame(s) lost\n",
                            current_timestamp(), fhs[0].counter - (last_counter + 1));
            }
            last_counter = fhs[n - 1].counter;
        }

        for (i = 0; i < n; i++) {
            buf_idx_cur = fhs_addr[i];
            frame_len = fhs[i].len;
            buf_idx_cur = cb_move(buf_idx_cur, frame_header_size);

            // Autodetect stream type (only the 1st time)
            if ((freq == -1) && (chan == -1)) {
                int n = 0;
                unsigned char *h = buf_idx_cur;

                n = ((*h & 0xFF) == 0xFF);
                n += ((*(h + 1) & 0xF0) == 0xF0);
                if (n == 2) {
                    h += 2;
                    freq = (*h & 0x3c) >> 2;
                    chan = (*h & 0x01) << 2;
                    h++;
                    chan += (*h & 0xc0) >> 6;

                    freq = samplingFrequencyTable[freq];
                    if (chan == 8) chan--;
                    if (debug) fprintf(stderr, "%lld: aac detected - frequency: %d - channels: %d\n",
                                current_timestamp(), freq, chan);
                }
            }

            write_enable = 1;
            frame_counter = fhs[i].stream_counter;
            if (fhs[i].type & 0x0100) {
                frame_type = TYPE_AAC;
            } else {
                frame_type = TYPE_NONE;
            }

            if (frame_type == TYPE_AAC) {
                if ((65536 + frame_counter - frame_counter_last_valid_audio) % 65536 > 1) {
                    if (debug) fprintf(stderr, "%lld: aac in - warning - %d AAC frame(s) lost - frame_counter: %d - frame_counter_last_valid: %d\n",
                                current_timestamp(), (65536 + frame_counter - frame_counter_last_valid_audio - 1) % 65536, frame_counter, frame_counter_last_valid_audio);
                    frame_counter_last_valid_audio = frame_counter;
                } else {
                    if (debug) fprintf(stderr, "%lld: aac in - frame detected - frame_len: %d - frame_counter: %d - audio AAC\n",
                                current_timestamp(), frame_len, fhs[i].stream_counter);

                    frame_counter_last_valid_audio = frame_counter;
                }
                buf_idx_start = buf_idx_cur;
            } else {
                write_enable = 0;
            }

            // Send the frame to the ouput buffer
            if (write_enable) {
                if (frame_type == TYPE_AAC) {
                    cb_current = &output_buffer_audio;
                } else {
                    cb_current = NULL;
                }

                if (cb_current != NULL) {
                    if (debug) fprintf(stderr, "%lld: aac in - frame_len: %d - cb_current->size: %d\n", current_timestamp(), frame_len, cb_current->size);
                    if (frame_len > (signed) cb_current->size) {
                        fprintf(stderr, "%lld: aac in - error - frame size exceeds buffer size\n", current_timestamp());
                    } else {
                        pthread_mutex_lock(&(cb_current->mutex));
                        input_buffer.read_index = buf_idx_start;

                        cb_current->output_frame[cb_current->frame_write_index].ptr = cb_current->write_index;
                        cb_current->output_frame[cb_current->frame_write_index].counter = frame_counter;

                        cb2cb_memcpy(cb_current, &input_buffer, frame_len);

                        cb_current->output_frame[cb_current->frame_write_index].size = frame_len;
                        if (debug) {
                            fprintf(stderr, "%lld: aac in - frame_len: %d - frame_counter: %d - resolution: %d\n", current_timestamp(), frame_len, frame_counter, frame_type);
                            fprintf(stderr, "%lld: aac in - frame_write_index: %d/%d\n", current_timestamp(), cb_current->frame_write_index, cb_current->output_frame_size);
                        }
                        cb_current->frame_write_index = (cb_current->frame_write_index + 1) % cb_current->output_frame_size;
                        pthread_mutex_unlock(&(cb_current->mutex));
                    }
                }
            }
        }

        usleep(25000);
    }

    // Unreacheable path

    // Unmap file from memory
    if (munmap(input_buffer.buffer, input_buffer.size) == -1) {
        fprintf(stderr, "%lld: capture - error - unmapping file\n", current_timestamp());
    } else {
        if (debug) fprintf(stderr, "%lld: capture - unmapping file %s, size %d, from %08x\n", current_timestamp(), input_buffer.filename, input_buffer.size, (unsigned int) input_buffer.buffer);
    }

    return NULL;
}

void print_usage(char *progname)
{
    fprintf(stderr, "\nUsage: %s [options]\n\n", progname);
    fprintf(stderr, "\t-m MODEL, --model MODEL\n");
    fprintf(stderr, "\t\tset model: y203c, y23, y25, y30, h201c, h305r, h307\n");
    fprintf(stderr, "\t\t           y20ga, y25ga, y30qa, y501gc\n");
    fprintf(stderr, "\t\t           y21ga, y211ga, y213ga, y291ga, h30ga, r30gb, r35gb, r40ga, h51ga, h52ga, h60ga, y28ga, y29ga, y623, q321br_lsx, qg311r or b091qp (default y21ga)\n");
    fprintf(stderr, "\t-x TYPE, --xcast TYPE\n");
    fprintf(stderr, "\t\tset unicast, multicast or ssm (source-specific multicast)\n");
    fprintf(stderr, "\t-a ADDRESS,  --address ADDRESS\n");
    fprintf(stderr, "\t\tset unicast destination address\n");
    fprintf(stderr, "\t-i,   --ipv6\n");
    fprintf(stderr, "\t\tuse ipv6 instead of ipv4\n");
    fprintf(stderr, "\t-d,   --debug\n");
    fprintf(stderr, "\t\tenable debug\n");
    fprintf(stderr, "\t-h,   --help\n");
    fprintf(stderr, "\t\tprint this help\n");
}

int main(int argc, char** argv)
{
    char cast[16];
    char address[16];
    int ipv6;
    char configStr[5];
    int pth_ret;
    int c, i;

    pthread_t capture_thread;

    FILE *fFS;

    // Setting default
    model = Y21GA;
    debug = 0;
    packet_counter = 0;
    isSSM = False;

    strcpy(cast, "unicast");
    memset(address, 0, sizeof(address));
    ipv6 = 0;
    freq = -1;
    chan = -1;

    while (1) {
        static struct option long_options[] =
        {
            {"model",  required_argument, 0, 'm'},
            {"xcast",  required_argument, 0, 'x'},
            {"address",  required_argument, 0, 'a'},
            {"ipv6",  no_argument, 0, 'i'},
            {"pc",  no_argument, 0, 'p'},
            {"debug",  no_argument, 0, 'd'},
            {"help",  no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "m:a:x:ipdh",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 'm':
            if (strcasecmp("y203c", optarg) == 0) {
                model = Y203C;
            } else if (strcasecmp("y23", optarg) == 0) {
                model = Y23;
            } else if (strcasecmp("y25", optarg) == 0) {
                model = Y25;
            } else if (strcasecmp("y30", optarg) == 0) {
                model = Y30;
            } else if (strcasecmp("h201c", optarg) == 0) {
                model = H201C;
            } else if (strcasecmp("h305r", optarg) == 0) {
                model = H305R;
            } else if (strcasecmp("h307", optarg) == 0) {
                model = H307;

            } else if (strcasecmp("y20ga", optarg) == 0) {
                model = Y20GA;
            } else if (strcasecmp("y25ga", optarg) == 0) {
                model = Y25GA;
            } else if (strcasecmp("y30qa", optarg) == 0) {
                model = Y30QA;
            } else if (strcasecmp("y501gc", optarg) == 0) {
                model = Y501GC;

            } else if (strcasecmp("y21ga", optarg) == 0) {
                model = Y21GA;
            } else if (strcasecmp("y211ga", optarg) == 0) {
                model = Y211GA;
            } else if (strcasecmp("y213ga", optarg) == 0) {
                model = Y213GA;
            } else if (strcasecmp("y291ga", optarg) == 0) {
                model = Y291GA;
            } else if (strcasecmp("h30ga", optarg) == 0) {
                model = H30GA;
            } else if (strcasecmp("r30gb", optarg) == 0) {
                model = R30GB;
            } else if (strcasecmp("r35gb", optarg) == 0) {
                model = R35GB;
            } else if (strcasecmp("r40ga", optarg) == 0) {
                model = R40GA;
            } else if (strcasecmp("h51ga", optarg) == 0) {
                model = H51GA;
            } else if (strcasecmp("h52ga", optarg) == 0) {
                model = H52GA;
            } else if (strcasecmp("h60ga", optarg) == 0) {
                model = H60GA;
            } else if (strcasecmp("y28ga", optarg) == 0) {
                model = Y28GA;
            } else if (strcasecmp("y29ga", optarg) == 0) {
                model = Y29GA;
            } else if (strcasecmp("y623", optarg) == 0) {
                model = Y623;
            } else if (strcasecmp("q321br_lsx", optarg) == 0) {
                model = Q321BR_LSX;
            } else if (strcasecmp("qg311r", optarg) == 0) {
                model = QG311R;
            } else if (strcasecmp("b091qp", optarg) == 0) {
                model = B091QP;
            }
            break;

        case 'x':
            if ((strlen(optarg) < sizeof(cast)) &&
                    ((strcasecmp("unicast", optarg) == 0) ||
                    (strcasecmp("multicast", optarg) == 0) ||
                    (strcasecmp("ssm", optarg) == 0))) {
                strcpy(cast, optarg);
            } else {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            if (strcasecmp("ssm", optarg) == 0) {
                isSSM = True;
            }
            break;

        case 'a':
            if (strlen(optarg) < sizeof(address)) {
                strcpy(address, optarg);
            }
            break;

        case 'i':
            ipv6 = 1;
            break;

        case 'p':
            packet_counter = 1;
            break;

        case 'd':
            debug = 1;
            break;

        case 'h':
            print_usage(argv[0]);
            return -1;
            break;

        case '?':
            /* getopt_long already printed an error message. */
            break;

        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if ((model / 100) == 2) {
        fFS = fopen(BUFFER_FILE_MSTAR, "r");
        if (fFS == NULL) {
            fprintf(stderr, "could not get size of %s\n", BUFFER_FILE_MSTAR);
            exit(EXIT_FAILURE);
        }
    } else {
        fFS = fopen(BUFFER_FILE_ALLWINNER, "r");
        if (fFS == NULL) {
            fprintf(stderr, "could not get size of %s\n", BUFFER_FILE_ALLWINNER);
            exit(EXIT_FAILURE);
        }
    }
    fseek(fFS, 0, SEEK_END);
    buf_size = ftell(fFS);
    // MStar
    if ((model / 100) == 2) {
        buf_size -= 2;
    }
    fclose(fFS);
    if (debug) fprintf(stderr, "%lld: the size of the buffer is %d\n",
            current_timestamp(), buf_size);

    if (model == Y203C) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == Y23) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == Y25) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == Y30) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == H201C) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == H305R) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;
    } else if (model == H307) {
        buf_offset = BUF_OFFSET_MSTAR;
        frame_header_size = FRAME_HEADER_SIZE_MSTAR;

    } else if (model == Y20GA) {
        buf_offset = BUF_OFFSET_Y20GA;
        frame_header_size = FRAME_HEADER_SIZE_Y20GA;
    } else if (model == Y25GA) {
        buf_offset = BUF_OFFSET_Y25GA;
        frame_header_size = FRAME_HEADER_SIZE_Y25GA;
    } else if (model == Y30QA) {
        buf_offset = BUF_OFFSET_Y30QA;
        frame_header_size = FRAME_HEADER_SIZE_Y30QA;
    } else if (model == Y501GC) {
        buf_offset = BUF_OFFSET_Y501GC;
        frame_header_size = FRAME_HEADER_SIZE_Y501GC;

    } else if (model == Y21GA) {
        buf_offset = BUF_OFFSET_Y21GA;
        frame_header_size = FRAME_HEADER_SIZE_Y21GA;
    } else if (model == Y211GA) {
        buf_offset = BUF_OFFSET_Y211GA;
        frame_header_size = FRAME_HEADER_SIZE_Y211GA;
    } else if (model == Y213GA) {
        buf_offset = BUF_OFFSET_Y213GA;
        frame_header_size = FRAME_HEADER_SIZE_Y213GA;
    } else if (model == Y291GA) {
        buf_offset = BUF_OFFSET_Y291GA;
        frame_header_size = FRAME_HEADER_SIZE_Y291GA;
    } else if (model == H30GA) {
        buf_offset = BUF_OFFSET_H30GA;
        frame_header_size = FRAME_HEADER_SIZE_H30GA;
    } else if (model == R30GB) {
        buf_offset = BUF_OFFSET_R30GB;
        frame_header_size = FRAME_HEADER_SIZE_R30GB;
    } else if (model == R35GB) {
        buf_offset = BUF_OFFSET_R35GB;
        frame_header_size = FRAME_HEADER_SIZE_R35GB;
    } else if (model == R40GA) {
        buf_offset = BUF_OFFSET_R40GA;
        frame_header_size = FRAME_HEADER_SIZE_R40GA;
    } else if (model == H51GA) {
        buf_offset = BUF_OFFSET_H51GA;
        frame_header_size = FRAME_HEADER_SIZE_H51GA;
    } else if (model == H52GA) {
        buf_offset = BUF_OFFSET_H52GA;
        frame_header_size = FRAME_HEADER_SIZE_H52GA;
    } else if (model == H60GA) {
        buf_offset = BUF_OFFSET_H60GA;
        frame_header_size = FRAME_HEADER_SIZE_H60GA;
    } else if (model == Y28GA) {
        buf_offset = BUF_OFFSET_Y28GA;
        frame_header_size = FRAME_HEADER_SIZE_Y28GA;
    } else if (model == Y29GA) {
        buf_offset = BUF_OFFSET_Y29GA;
        frame_header_size = FRAME_HEADER_SIZE_Y29GA;
    } else if (model == Y623) {
        buf_offset = BUF_OFFSET_Y623;
        frame_header_size = FRAME_HEADER_SIZE_Y623;
    } else if (model == Q321BR_LSX) {
        buf_offset = BUF_OFFSET_Q321BR_LSX;
        frame_header_size = FRAME_HEADER_SIZE_Q321BR_LSX;
    } else if (model == QG311R) {
        buf_offset = BUF_OFFSET_QG311R;
        frame_header_size = FRAME_HEADER_SIZE_QG311R;
    } else if (model == B091QP) {
        buf_offset = BUF_OFFSET_B091QP;
        frame_header_size = FRAME_HEADER_SIZE_B091QP;
    }

    if ((strcasecmp("unicast", cast) == 0) && (address[0] == '\0')) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    setpriority(PRIO_PROCESS, 0, -10);

    // Fill input and output buffer struct
    strcpy(input_buffer.filename, BUFFER_SHM);
    input_buffer.size = buf_size;
    input_buffer.offset = buf_offset;

    // Audio
    output_buffer_audio.type = TYPE_AAC;
    output_buffer_audio.size = OUTPUT_BUFFER_SIZE_AUDIO;
    output_buffer_audio.buffer = (unsigned char *) malloc(OUTPUT_BUFFER_SIZE_AUDIO * sizeof(unsigned char));
    output_buffer_audio.write_index = output_buffer_audio.buffer;
    output_buffer_audio.frame_read_index = 0;
    output_buffer_audio.frame_write_index = 0;
    output_buffer_audio.output_frame_size = sizeof(output_buffer_audio.output_frame) / sizeof(output_buffer_audio.output_frame[0]);
    if (output_buffer_audio.buffer == NULL) {
        fprintf(stderr, "could not alloc memory\n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < output_buffer_audio.output_frame_size; i++) {
        output_buffer_audio.output_frame[i].ptr = NULL;
        output_buffer_audio.output_frame[i].counter = 0;
        output_buffer_audio.output_frame[i].size = 0;
    }

    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // Start capture thread
    if (pthread_mutex_init(&(output_buffer_audio.mutex), NULL) != 0) {
        fprintf(stderr, "Failed to create mutex\n");
        if(output_buffer_audio.buffer != NULL) free(output_buffer_audio.buffer);
        exit(EXIT_FAILURE);
    }
    pth_ret = pthread_create(&capture_thread, NULL, capture, (void*) NULL);
    if (pth_ret != 0) {
        fprintf(stderr, "Failed to create capture thread\n");
        if(output_buffer_audio.buffer != NULL) free(output_buffer_audio.buffer);
        exit(EXIT_FAILURE);
    }
    pthread_detach(capture_thread);

    sleep(2);

    // Wait for stream type autodetect
    while (1) {
        if ((freq != -1) && (chan != -1)) {
            usleep(10000);
            break;
        }
        usleep(10000);
    }

  // Create 'groupsocks' for RTP and RTCP:
    char destinationAddressStr[16];
    if (ipv6) {
        if (strcasecmp("ssm", cast) == 0) {
            strcpy(destinationAddressStr, "FF3E::FFFF:2A2A");
        } else if (strcasecmp("multicast", cast) == 0) {
            strcpy(destinationAddressStr, "FF1E::FFFF:2A2A");
        } else if (strcasecmp("unicast", cast) == 0) {
            strcpy(destinationAddressStr, address);
        }
    } else {
        if (strcasecmp("ssm", cast) == 0) {
            strcpy(destinationAddressStr, "232.255.42.42");
        } else if (strcasecmp("multicast", cast) == 0) {
            strcpy(destinationAddressStr, "239.255.42.42");
        } else if (strcasecmp("unicast", cast) == 0) {
            strcpy(destinationAddressStr, address);
        }
    }

    const unsigned short rtpPortNum = 6666;
    const unsigned short rtcpPortNum = rtpPortNum+1;
    const unsigned char ttl = 1; // low, in case routers don't admin scope

    NetAddressList destinationAddresses(destinationAddressStr);
    struct sockaddr_storage destinationAddress;
    copyAddress(destinationAddress, destinationAddresses.firstAddress());

    const Port rtpPort(rtpPortNum);
    const Port rtcpPort(rtcpPortNum);

    sessionState.rtpGroupsock
        = new Groupsock(*env, destinationAddress, rtpPort, ttl);
    sessionState.rtcpGroupsock
        = new Groupsock(*env, destinationAddress, rtcpPort, ttl);

    if (strcasecmp("ssm", cast) == 0) {
        sessionState.rtpGroupsock->multicastSendOnly();
        sessionState.rtcpGroupsock->multicastSendOnly();
    }

    getAACConfigStr(configStr, freq, chan);
    unsigned char rtpPayloadFormat = 97; // a dynamic payload type
    sessionState.sink
        = MPEG4GenericRTPSink::createNew(*env, sessionState.rtpGroupsock,
                                        rtpPayloadFormat,
                                        freq,
                                        "audio", "aac-hbr", configStr,
                                        chan);

    // Create (and start) a 'RTCP instance' for this RTP sink:
    const unsigned estimatedSessionBandwidth = 50; // in kbps; for RTCP b/w share
    const unsigned maxCNAMElen = 100;
    unsigned char CNAME[maxCNAMElen+1];
    gethostname((char*)CNAME, maxCNAMElen);
    CNAME[maxCNAMElen] = '\0'; // just in case
    sessionState.rtcpInstance
        = RTCPInstance::createNew(*env, sessionState.rtcpGroupsock,
				  estimatedSessionBandwidth, CNAME,
				  sessionState.sink, NULL /* we're a server */,
				  isSSM);
    // Note: This starts RTCP running automatically

    play();

    env->taskScheduler().doEventLoop(); // does not return

    pthread_mutex_destroy(&(output_buffer_audio.mutex));

    // Free buffers
    if(output_buffer_audio.buffer != NULL) free(output_buffer_audio.buffer);

    delete sessionState.rtcpGroupsock;
    delete sessionState.rtpGroupsock;

    return 0; // only to prevent compiler warning
}

void play()
{
    // Open the source:
    sessionState.source = AudioFramedMemorySource::createNew(*env, &output_buffer_audio, freq, chan);
    if (sessionState.source == NULL) {
        fprintf(stderr, "Unable to open source\n");
        exit(1);
    }

    // Finally, start the streaming:
    fprintf(stderr, "Beginning streaming...\n");
    sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);
}


void afterPlaying(void* /*clientData*/)
{
    fprintf(stderr, "...done streaming\n");

    sessionState.sink->stopPlaying();

    // End this loop by closing the current source:
    Medium::close(sessionState.source);

    // And start another loop:
//    play();
}
