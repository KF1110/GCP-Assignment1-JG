SDL3 Audio can play background music, while allowing sounds effects to play at
the same time. The volumes of each audio stream can also be set and changed.

SDL3 Mixer adds only decoding of compressed formats (OGG, MP3, FLAC); channel
management abstractions; convenience helpers (fade in/out, looping); and legacy
SDL2 compatibility. So, it's not needed for simple games.

choral.wav (30 secs.) from [here](https://commons.wikimedia.org/wiki/File:30_second_sample_of_Agnus_Dei_by_Ernesto_Herrera,_performed_by_BYU_Singers.ogg)

```
ffmpeg -i 30_second_sample_of_Agnus_Dei_by_Ernesto_Herrera,_performed_by_BYU_Singers.ogg -ar 48000 choral.wav

```

Note that I use 48 kHz above. 44.1KHz is a legacy choice from the 70s, though
used for CDs. 48 kHz is the modern default used internally by almost all audio
pipelines (Windows, Linux, Consoles...). If a WAV was 44.1 kHz, the OS would
likely resample it to 48 kHz anyway.

Ok, so we want everything at 48 kHz, though this code is using a basic 22.1 kHz
wav as input:

```
ffmpeg -i buttonfx.wav -ar 48000 buttonfx2.wav
mv buttonfx2.wav buttonfx.wav
ffprobe -show_format buttonfx.wav
```

buttonfx.wav is from [here](https://github.com/raysan5/raylib/blob/master/examples/textures/resources/buttonfx.wav).

-------------------------------

For multi-stream mixing, use `SDL_OpenAudioDevice` directly rather than the
convenience wrapper `SDL_OpenAudioDeviceStream`. The wrapper opens a logical
device dedicated to a single stream, so trying to share it by calling
`SDL_GetAudioStreamDevice` and then binding a second stream via
`SDL_BindAudioStream` does not work: the second stream's data is ignored. The
correct pattern is to open the device once with `SDL_OpenAudioDevice`, create
each stream independently with `SDL_CreateAudioStream` (passing `nullptr` as
the destination spec so SDL resolves the device format at bind time), and then
call `SDL_BindAudioStream` for each stream. SDL automatically mixes all streams
bound to the same device before sending audio to the hardware.
