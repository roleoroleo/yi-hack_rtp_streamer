# RTP streamer/receiver for Yi camera based on MStar and Allwinner platforms


## Streamer
This process reads the stream produced by the microphone from the frame buffer in shared memory (/dev/fshare_frame_buf or /dev/shm/fshare_frame_buf) and sends it to another host using RTP protocol.

- The stream is in AAC format
- The RTP port used is 6666
- The RTCP port used is 6667

```
root@yi-hack:/tmp/sd/yi-hack/bin# ./rAudioStreamer -h

Usage: ./rAudioStreamer [options]

        -m MODEL, --model MODEL
                set model: y203c, y23, y25, y30, h201c, h305r, h307
                           y20ga, y25ga, y30qa, y501gc
                           y21ga, y211ga, y213ga, y291ga, h30ga, r30gb, r35gb, r40ga, h51ga, h52ga, h60ga, y28ga, y29ga, y623, q321br_lsx, qg311r or b091qp (default y21ga)
        -x TYPE, --xcast TYPE
                set unicast, multicast or ssm (source-specific multicast)
        -a ADDRESS,  --address ADDRESS
                set unicast destination address
        -i,   --ipv6
                use ipv6 instead of ipv4
        -d,   --debug
                enable debug
        -h,   --help
                print this help
```

Command line example to stream using unicast address:

`./rAudioStreamer -m y20ga -a 192.168.100.100`


## Receiver
This process waits for incoming packets on port 6666, converts the stream to PCM and sends the resulting stream to stdout.

You can use the internal speaker of the cam redirecting the stream to /tmp/audio_in_fifo

```
root@yi-hack:/tmp/sd/yi-hack/bin# ./rAudioReceiver -h

Usage: ./rAudioReceiver [options]

        -s,   --sample_rate
                sample rate of incoming stream, default 16 KHz
        -c,   --channels
                number of channels of incoming stream (1 or 2 supported), default 1
        -x TYPE, --xcast TYPE
                set unicast, multicast or ssm (source-specific multicast)
        -u ADDRESS, --source ADDRESS
                source address when ssm is selected
        -i,   --ipv6
                use ipv6 instead of ipv4
        -g,   --gpio
                enable and disable gpio to activate the speaker (only Allwinner-v2)
        -d,   --debug
                enable debug
        -h,   --help
                print this help

```

Command line example to receive using unicast address:

`./rAudioReceiver`

Command line example to receive using unicast address and activate the speaker (Allwinner-v2 cam):

`./rAudioReceiver -g > /tmp/audio_in_fifo`


## Stream using ffmpeg
You can use ffmpeg to stream audio to the receiver.
AAC must be 16 KHz, mono, VBR.

Command line example to stream with ffmpeg:

`ffmpeg -re -i audio.aac -c:a aac -ar 16000 -f rtp rtp://192.168.100.100:6666`

----

## Acknowledgments
This work is based on:
- live555 library: http://www.live555.com/
  RTP streaming
- FDK AAC library: https://github.com/mstorsjo/fdk-aac
  AAC to PCM conversion

## License
[GPLv3](https://choosealicense.com/licenses/gpl-3.0/)

## DISCLAIMER
**NOBODY BUT YOU IS RESPONSIBLE FOR ANY USE OR DAMAGE THIS SOFTWARE MAY CAUSE. THIS IS INTENDED FOR EDUCATIONAL PURPOSES ONLY. USE AT YOUR OWN RISK.**
