
# How to find url/codec for  radio streams, current song, and bitrate

## Identifying url

<https://fmstream.org> or ask gemini

## Identifying codec

```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 <url>
```

## Measuring bitrate

```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=bit_rate -of default=noprint_wrappers=1:nokey=1 <url>
```

## getting currently playing song

### kexp

```{bash}
curl -s "https://api.kexp.org/v2/plays/?limit=1" | jq '.results[0] | {artist, song, album}'
```

## Station Bitrate Reference

The following table shows the measured bitrates for the default radio stations (calculated using `ffprobe`).

| Call Sign | Measured Bitrate | Sampling Frequency | URI |
| :--- | :--- | :--- | :--- |
| KEXP | 160 kbps | 44100 Hz | `https://kexp.streamguys1.com/kexp160.aac` |
| KBUT | 64 kbps | 22050 Hz | `http://playerservices.streamtheworld.com/api/livestream-redirect/KBUTFM.mp3` |
| KSUT | 64 kbps | 48000 Hz | `https://ksut.streamguys1.com/kute` |
| KDUR | 128 kbps | 44100 Hz | `https://kdurradio.fortlewis.edu/stream` |
| KOTO | 128 kbps | 44100 Hz | `http://playerservices.streamtheworld.com/api/livestream-redirect/KOTOFM.mp3` |
| KHEN | 128 kbps | 44100 Hz | `https://stream.pacificaservice.org:9000/khen_128` |
| KWSB | 128 kbps | 44100 Hz | `https://kwsb.streamguys1.com/live` |
| KFFP | 256 kbps | 44100 Hz | `http://listen.freeformportland.org:8000/stream` |
| KBOO | 192 kbps | 48000 Hz | `https://live.kboo.fm:8443/high` |
| KXLU | 48 kbps | 44100 Hz | `http://kxlu.streamguys1.com:80/kxlu-lo` |
| WPRB | 96 kbps | 44100 Hz | `https://wprb.streamguys1.com/listen.mp3` |
| WMBR | 128 kbps | 44100 Hz | `https://wmbr.org:8002/hi` |
| KALX | 128 kbps | 48000 Hz | `https://stream.kalx.berkeley.edu:8443/kalx-128.mp3` |
| WFUV | 128 kbps | 44100 Hz | `https://onair.wfuv.org/onair-hi` |
| KUFM | 64 kbps | 22050 Hz | `https://playerservices.streamtheworld.com/api/livestream-redirect/KUFMFM.mp3` |
| KRCL | 256 kbps | 44100 Hz | `http://stream.xmission.com:8000/krcl-low` |
