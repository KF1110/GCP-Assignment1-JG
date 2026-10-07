#ifndef _SDL3_AUDIO_HPP_
#define _SDL3_AUDIO_HPP_

#include "sdl3-context.hpp"  // SDLContext
#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include <SDL3/SDL.h>        // SDL_LoadWAV, SDL_OpenAudioDevice etc.
#include <filesystem>        // std::filesystem::path
#include <span>              // std::span

namespace zod
{

// The samples of a WAV file, decoded into memory, and the format they are in.
// Loading needs no audio device, so nothing is asked for here but the path.
class Wav
{
  struct Key {};   // private tag: only Wav can name it

public:
  static Result<Wav> create(const std::filesystem::path& path)
  {
    SDL_AudioSpec spec{};
    Uint8*        buffer = nullptr;
    Uint32        length = 0;
    if (!SDL_LoadWAV(path.string().c_str(), &spec, &buffer, &length))
    {
      return sdl_fail("SDL_LoadWAV");
    }

    return Result<Wav>(ns::in_place, Key{}, spec, buffer, length);
  }

  Wav(Key, const SDL_AudioSpec& spec, Uint8* buffer, Uint32 length)
    : spec_{spec}, buffer_{buffer}, length_{length}
  {
  }

  Wav(const Wav&)            = delete;
  Wav& operator=(const Wav&) = delete;

  ~Wav() { SDL_free(buffer_); }

  // The format: sample rate, channel count, and sample type
  const SDL_AudioSpec& spec() const { return spec_; }

  // A view of the samples themselves, which this Wav still owns
  std::span<const Uint8> bytes() const { return {buffer_, length_}; }

private:
  SDL_AudioSpec spec_;
  Uint8*        buffer_;
  Uint32        length_;
};

// The default playback device: the speakers, headphones, or TV.  Every stream
// bound to it is mixed together by SDL before it reaches the hardware.
class AudioDevice
{
  struct Key {};   // private tag: only AudioDevice can name it

public:
  // An SDLContext proves that SDL_Init was called, but not with which flags,
  // so the audio subsystem is checked for here.
  static Result<AudioDevice> create(const SDLContext&)
  {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
    {
      return fail("SDL_INIT_AUDIO was not passed to SDLContext::create");
    }

    SDL_AudioDeviceID device =
      SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device)
    {
      return sdl_fail("SDL_OpenAudioDevice");
    }

    return Result<AudioDevice>(ns::in_place, Key{}, device);
  }

  AudioDevice(Key, SDL_AudioDeviceID device) : device_{device} { }
  AudioDevice(const AudioDevice&)            = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  ~AudioDevice() { SDL_CloseAudioDevice(device_); }

  // The raw handle, for the SDL calls that want one
  SDL_AudioDeviceID sdl() const { return device_; }

private:
  SDL_AudioDeviceID device_;
};

// A queue of samples, bound to a device, with a volume ("gain") of its own.
// Each stream plays one Wav: its samples are put in, in the Wav's format,
// and converted to whatever the device uses.  Needing an AudioDevice and a
// Wav means this is declared after both, and so destroyed before both.
class AudioStream
{
  struct Key {};   // private tag: only AudioStream can name it

public:
  // The output format is left null: SDL fills it in from the device when the
  // stream is bound to it.
  static Result<AudioStream> create(const AudioDevice& device, const Wav& wav)
  {
    SDL_AudioStream* stream = SDL_CreateAudioStream(&wav.spec(), nullptr);
    if (!stream)
    {
      return sdl_fail("SDL_CreateAudioStream");
    }

    if (!SDL_BindAudioStream(device.sdl(), stream))
    {
      SDL_DestroyAudioStream(stream);
      return sdl_fail("SDL_BindAudioStream");
    }

    return Result<AudioStream>(ns::in_place, Key{}, stream, wav.bytes());
  }

  AudioStream(Key, SDL_AudioStream* stream, std::span<const Uint8> samples)
    : stream_{stream}, samples_{samples}
  {
  }

  AudioStream(const AudioStream&)            = delete;
  AudioStream& operator=(const AudioStream&) = delete;

  ~AudioStream() { SDL_DestroyAudioStream(stream_); } // also unbinds it

  // The raw handle, for the SDL calls that want one
  SDL_AudioStream* sdl() const { return stream_; }

  // The Wav's samples, which it still owns, ready to be put into the stream
  std::span<const Uint8> samples() const { return samples_; }

private:
  SDL_AudioStream*       stream_;
  std::span<const Uint8> samples_;
};

} // namespace zod

#endif // _SDL3_AUDIO_HPP_
