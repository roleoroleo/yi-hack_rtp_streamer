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
 * Receive a rtp stream with aac audio content.
 * Convert to PCM stream with fdk_aac.
 * And send it to stdout.
 */

#include "liveMedia.hh"
#include "GroupsockHelper.hh"
#include "BasicUsageEnvironment.hh"

#include "rAudioStreamerReceiver.h"
#include "ADTS2PCMFileSink.hh"
#include "speaker.h"

#include "errno.h"
#include "limits.h"
#include "pthread.h"

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
    FramedSource* source;
    ADTS2PCMFileSink* sink;
    RTCPInstance* rtcpInstance;
    Groupsock* rtpGroupsock;
    Groupsock* rtcpGroupsock;
} sessionState;

UsageEnvironment* env;

int packet_counter;
int gpio;
int speaker_counter;
int exit_thread;
int debug;

void *speaker(void *ptr)
{
    while (!exit_thread) {
        usleep(100 * 1000);
        if (speaker_counter == 0) {
            speaker(0);
            speaker_counter = -1;
        } else if (speaker_counter > 0) {
            speaker_counter = speaker_counter - 100;
        }
    }

    return NULL;
}

void print_usage(char *progname)
{
    fprintf(stderr, "\nUsage: %s [options]\n\n", progname);
    fprintf(stderr, "\t-s,   --sample_rate\n");
    fprintf(stderr, "\t\tsample rate of incoming stream, default 16 KHz\n");
    fprintf(stderr, "\t-c,   --channels\n");
    fprintf(stderr, "\t\tnumber of channels of incoming stream (1 or 2 supported), default 1\n");
    fprintf(stderr, "\t-x TYPE, --xcast TYPE\n");
    fprintf(stderr, "\t\tset unicast, multicast or ssm (source-specific multicast)\n");
    fprintf(stderr, "\t-u ADDRESS, --source ADDRESS\n");
    fprintf(stderr, "\t\tsource address when ssm is selected\n");
    fprintf(stderr, "\t-i,   --ipv6\n");
    fprintf(stderr, "\t\tuse ipv6 instead of ipv4\n");
    fprintf(stderr, "\t-g,   --gpio\n");
    fprintf(stderr, "\t\tenable and disable gpio to activate the speaker (only Allwinner-v2)\n");
    fprintf(stderr, "\t-d,   --debug\n");
    fprintf(stderr, "\t\tenable debug\n");
    fprintf(stderr, "\t-h,   --help\n");
    fprintf(stderr, "\t\tprint this help\n");
}

int main(int argc, char** argv) {

    int c;
    int sample_rate = SAMPLING_FREQ;
    int channels = NUM_CHANNELS;
    char cast[16];
    char source_address[16];
    int ipv6 = 0;
    char *endptr;

    int pth_ret;
    pthread_t speaker_thread;

    strcpy(cast, "unicast");
    source_address[0] = '\0';
    packet_counter = 0;
    gpio = 0;
    speaker_counter = 0;
    exit_thread = 0;
    debug = 0;

    while (1) {
        static struct option long_options[] =
        {
            {"sample_rate",  required_argument, 0, 's'},
            {"channels",  required_argument, 0, 'c'},
            {"xcast",  required_argument, 0, 'x'},
            {"source",  required_argument, 0, 'u'},
            {"ipv6",  no_argument, 0, 'i'},
            {"pc",  no_argument, 0, 'p'},
            {"gpio",  no_argument, 0, 'g'},
            {"debug",  no_argument, 0, 'd'},
            {"help",  no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "s:c:x:u:ipgdh",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 's':
            errno = 0;    /* To distinguish success/failure after call */
            sample_rate = strtol(optarg, &endptr, 10);

            /* Check for various possible errors */
            if ((errno == ERANGE && (sample_rate == LONG_MAX || sample_rate == LONG_MIN)) || (errno != 0 && sample_rate == 0)) {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            if (endptr == optarg) {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;

        case 'c':
            errno = 0;    /* To distinguish success/failure after call */
            channels = strtol(optarg, &endptr, 10);

            /* Check for various possible errors */
            if ((errno == ERANGE && (channels == LONG_MAX || channels == LONG_MIN)) || (errno != 0 && channels == 0)) {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            if (endptr == optarg) {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            if ((channels != 1) && (channels != 2)) {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
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
            break;

        case 'u':
            if (strlen(optarg) < sizeof(source_address)) {
                strcpy(source_address, optarg);
            } else {
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;

        case 'i':
            ipv6 = 1;
            break;

        case 'p':
            packet_counter = 1;
            break;

        case 'g':
            gpio = 1;
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

    if ((strcasecmp("ssm", cast) == 0) && (source_address[0] == '\0')) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (gpio) {
        // Start thread to handle the gpio of the speaker (only Allwinner-v2)
        pth_ret = pthread_create(&speaker_thread, NULL, speaker, (void*) NULL);
        if (pth_ret != 0) {
            fprintf(stderr, "Failed to create speaker thread\n");
            exit(EXIT_FAILURE);
        }
        pthread_detach(speaker_thread);
    }

    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);

    // Create the data sink for 'stdout':
    sessionState.sink = ADTS2PCMFileSink::createNew(*env, "stdout", sample_rate, channels);
    // Note: The string "stdout" is handled as a special case.
    // A real file name could have been used instead.

    // Create 'groupsocks' for RTP and RTCP:
    char sessionAddressStr[16];
    if (ipv6) {
        if (strcasecmp("ssm", cast) == 0) {
            strcpy(sessionAddressStr, "FF3E::FFFF:2A2A");
        } else if (strcasecmp("multicast", cast) == 0) {
            strcpy(sessionAddressStr, "FF1E::FFFF:2A2A");
        } else if (strcasecmp("unicast", cast) == 0) {
            strcpy(sessionAddressStr, "::");
        }
    } else {
        if (strcasecmp("ssm", cast) == 0) {
            strcpy(sessionAddressStr, "232.255.42.42");
        } else if (strcasecmp("multicast", cast) == 0) {
            strcpy(sessionAddressStr, "239.255.42.42");
        } else if (strcasecmp("unicast", cast) == 0) {
            strcpy(sessionAddressStr, "0.0.0.0");
        }
    }

    const unsigned short rtpPortNum = 6666;
    const unsigned short rtcpPortNum = rtpPortNum+1;
    // If you are using SSM
    const unsigned char ttl = 1; // low, in case routers don't admin scope

    NetAddressList sessionAddresses(sessionAddressStr);
    struct sockaddr_storage sessionAddress;
    copyAddress(sessionAddress, sessionAddresses.firstAddress());

    const Port rtpPort(rtpPortNum);
    const Port rtcpPort(rtcpPortNum);

    if (strcasecmp("ssm", cast) == 0) {
        NetAddressList sourceFilterAddresses(source_address);
        struct sockaddr_storage sourceFilterAddress;
        copyAddress(sourceFilterAddress, sourceFilterAddresses.firstAddress());

        sessionState.rtpGroupsock = new Groupsock(*env, sessionAddress, sourceFilterAddress, rtpPort);
        sessionState.rtcpGroupsock = new Groupsock(*env, sessionAddress, sourceFilterAddress, rtcpPort);
        sessionState.rtcpGroupsock->changeDestinationParameters(sourceFilterAddress,0,~0);
        // our RTCP "RR"s are sent back using unicast
    } else {
        sessionState.rtpGroupsock = new Groupsock(*env, sessionAddress, rtpPort, ttl);
        sessionState.rtcpGroupsock = new Groupsock(*env, sessionAddress, rtcpPort, ttl);
    }

    RTPSource* rtpSource;

    // Create the data source: a "MPEG4 Generic RTP source"
    unsigned char rtpPayloadFormat = 97; // a dynamic payload type
    rtpSource
        = MPEG4GenericRTPSource::createNew(*env, sessionState.rtpGroupsock,
            rtpPayloadFormat,
            0, //SAMPLING_FREQ,
            "audio", "aac-hbr",
            13,   // unsigned sizeLength
            3,    // unsigned indexLength,
            3);   // unsigned indexDeltaLength

    // Create (and start) a 'RTCP instance' for the RTP source:
    const unsigned estimatedSessionBandwidth = 50; // in kbps; for RTCP b/w share
    const unsigned maxCNAMElen = 100;
    unsigned char CNAME[maxCNAMElen+1];
    gethostname((char*)CNAME, maxCNAMElen);
    CNAME[maxCNAMElen] = '\0'; // just in case
    sessionState.rtcpInstance
        = RTCPInstance::createNew(*env, sessionState.rtcpGroupsock,
                                  estimatedSessionBandwidth, CNAME,
                                  NULL /* we're a client */, rtpSource);
    // Note: This starts RTCP running automatically

    sessionState.source = rtpSource;

    // Finally, start receiving the stream:
    fprintf(stderr, "Beginning receiving stream...\n");
    sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);

    env->taskScheduler().doEventLoop(); // does not return

    delete sessionState.rtcpGroupsock;
    delete sessionState.rtpGroupsock;

    return 0; // only to prevent compiler warning
}

void afterPlaying(void* /*clientData*/) {
    fprintf(stderr, "...done receiving\n");

    // End by closing the media:
    Medium::close(sessionState.rtcpInstance); // Note: Sends a RTCP BYE
    Medium::close(sessionState.sink);
    Medium::close(sessionState.source);
}
