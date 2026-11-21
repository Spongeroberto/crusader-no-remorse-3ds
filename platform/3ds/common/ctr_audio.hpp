#pragma once
// Plays the Asylum mixer through the 3DS DSP (NDSP), as SdlAudio does on the
// PC: one mono 16-bit channel at the mixer's rate, which the DSP resamples to
// its own output rate. A thread refills each wave buffer the DSP has played,
// on the second core when the system lends it, so mixing stays off the
// game's frame.
//
// NDSP needs the console's DSP firmware at sdmc:/3ds/dspfirm.cdc (dumped once
// with the DSP1 homebrew, a step of the usual Luma3DS setup); without it ok()
// is false and the game runs silent.

#include "engine/audio/asylum_mixer.hpp"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace crusader::ctr {

class Audio {
public:
    explicit Audio(AsylumMixer& mixer) : mixer_(mixer) {
        if (R_FAILED(ndspInit())) return;
        ndsp_ = true;
        samples_ = static_cast<std::int16_t*>(linearAlloc(kBuffers * kSamples * sizeof(std::int16_t)));
        if (!samples_) return;
        std::memset(samples_, 0, kBuffers * kSamples * sizeof(std::int16_t));

        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnReset(kChannel);
        ndspChnSetInterp(kChannel, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(kChannel, static_cast<float>(AsylumMixer::kMixRate));
        ndspChnSetFormat(kChannel, NDSP_FORMAT_MONO_PCM16);
        float mix[12] = {1.0f, 1.0f};  // front left and right
        ndspChnSetMix(kChannel, mix);

        for (std::size_t i = 0; i < kBuffers; ++i) {
            buffers_[i] = {};
            buffers_[i].data_pcm16 = samples_ + i * kSamples;
            buffers_[i].nsamples = kSamples;
            buffers_[i].status = NDSP_WBUF_DONE;  // the thread fills it first
        }

        LightEvent_Init(&wake_, RESET_ONESHOT);
        ndspSetCallback(&Audio::on_dsp_frame, this);

        // Above the game's thread, so a refill is never late behind a frame.
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        priority = std::max<s32>(0x18, priority - 1);
        // The second core needs a share of its time lent to the application;
        // without it the thread shares the game's core.
        if (R_SUCCEEDED(APT_SetAppCpuTimeLimit(kCore1Percent))) {
            thread_ = threadCreate(&Audio::run, this, kStackSize, priority, 1, false);
            on_core1_ = thread_ != nullptr;
        }
        if (!thread_) thread_ = threadCreate(&Audio::run, this, kStackSize, priority, 0, false);
    }

    ~Audio() {
        if (thread_) {
            quit_ = true;
            LightEvent_Signal(&wake_);
            threadJoin(thread_, U64_MAX);
            threadFree(thread_);
        }
        if (ndsp_) {
            ndspSetCallback(nullptr, nullptr);
            ndspChnReset(kChannel);
            ndspExit();
        }
        if (samples_) linearFree(samples_);
    }

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool ok() const { return thread_ != nullptr; }
    // False without the DSP firmware (sdmc:/3ds/dspfirm.cdc).
    bool dsp_ok() const { return ndsp_; }
    bool on_core1() const { return on_core1_; }

private:
    static constexpr int kChannel = 0;
    static constexpr std::size_t kBuffers = 4;
    // About 23 ms each, as SdlAudio's device buffer: four of them ride out a
    // frame of about 60 ms on the classic 3DS even if the thread waits behind it.
    static constexpr std::size_t kSamples = 512;
    static constexpr std::size_t kStackSize = 16 * 1024;
    static constexpr u32 kCore1Percent = 30;

    // Called by NDSP once per DSP frame (about 5 ms).
    static void on_dsp_frame(void* self) { LightEvent_Signal(&static_cast<Audio*>(self)->wake_); }

    static void run(void* self) {
        auto* audio = static_cast<Audio*>(self);
        while (!audio->quit_) {
            for (ndspWaveBuf& buffer : audio->buffers_) {
                if (buffer.status != NDSP_WBUF_DONE) continue;
                audio->mixer_.mix(buffer.data_pcm16, kSamples);
                DSP_FlushDataCache(buffer.data_pcm16, kSamples * sizeof(std::int16_t));
                ndspChnWaveBufAdd(kChannel, &buffer);
            }
            LightEvent_Wait(&audio->wake_);
        }
    }

    AsylumMixer& mixer_;
    bool ndsp_ = false;
    bool on_core1_ = false;
    std::int16_t* samples_ = nullptr;  // linear memory, which the DSP reads
    std::array<ndspWaveBuf, kBuffers> buffers_{};
    LightEvent wake_{};
    Thread thread_ = nullptr;
    std::atomic<bool> quit_{false};
};

}  // namespace crusader::ctr
