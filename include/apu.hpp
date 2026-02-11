#pragma once

#include <array>
#include <optional>
#include <types.hpp>

// Channel 1 has sweep, Channel 2 doesn't
struct SquareChannel {
    U8 sweep{};         // NR10 (Channel 1 only)
    U8 lengthDuty{};    // NRx1
    U8 envelope{};      // NRx2
    U8 freqLow{};       // NRx3
    U8 freqHigh{};      // NRx4

    bool enabled{false};
    bool dacEnabled{false};
    S32 frequencyTimer{};
    S32 dutyPosition{};
    S32 lengthCounter{};
    S32 periodTimer{};
    S32 currentVolume{};
    bool envelopeRunning{};

    // Sweep (Channel 1 only)
    bool sweepEnabled{};
    S32 sweepFrequency{};
    S32 sweepTimer{};
    bool sweepNegate{};

    void Trigger(bool hasSweep);
    void ClockLength();
    void ClockEnvelope();
    void ClockSweep();
    S32 GetFrequency() const;
    U8 GetOutput() const;
};

struct WaveChannel {
    U8 dacEnable{};     // NR30
    U8 length{};        // NR31
    U8 volume{};        // NR32
    U8 freqLow{};       // NR33
    U8 freqHigh{};      // NR34

    // 32 4-bit samples stored in 16 bytes
    std::array<U8, 16> waveRAM{};

    bool enabled{false};
    S32 frequencyTimer{};
    S32 positionCounter{};
    S32 lengthCounter{};
    U8 sampleBuffer{};

    void Trigger();
    void ClockLength();
    S32 GetFrequency() const;
    U8 GetOutput() const;
};

struct NoiseChannel {
    U8 length{};        // NR41
    U8 envelope{};      // NR42
    U8 polynomial{};    // NR43
    U8 control{};       // NR44

    bool enabled{false};
    bool dacEnabled{false};
    S32 frequencyTimer{};
    S32 lengthCounter{};
    S32 periodTimer{};
    S32 currentVolume{};
    bool envelopeRunning{};
    U16 lfsr{0x7FFF};   // Linear feedback shift register

    void Trigger();
    void ClockLength();
    void ClockEnvelope();
    S32 GetDivisor() const;
    U8 GetOutput() const;
};

class APU {
public:
    static constexpr S32 SampleRate = 44100;
    static constexpr S32 CPUFrequency = 4194304;
    static constexpr S32 FrameSequencerRate = 512;
    static constexpr S32 CyclesPerSample = CPUFrequency / SampleRate;
    static constexpr S32 CyclesPerFrameSequencer = CPUFrequency / FrameSequencerRate;
    static constexpr Size AudioBufferSize = 2048;

    APU();

    void Tick(U8 cycles);

    [[nodiscard]] std::optional<U8> Read(U16 address) const;
    bool Write(U16 address, U8 value);

    [[nodiscard]] const std::array<float, AudioBufferSize>& GetAudioBuffer() const { return m_AudioBuffer; }
    [[nodiscard]] Size GetSampleCount() const { return m_SampleIndex; }
    void ClearBuffer() { m_SampleIndex = 0; }
    [[nodiscard]] bool BufferFull() const { return m_SampleIndex >= AudioBufferSize; }

private:
    void TickChannels();
    void TickFrameSequencer();
    void GenerateSample();
    float MixChannels() const;

    SquareChannel m_Channel1;  // Square with sweep
    SquareChannel m_Channel2;  // Square
    WaveChannel m_Channel3;    // Wave
    NoiseChannel m_Channel4;   // Noise

    U8 m_NR50{};  // 0xFF24: Master volume & VIN panning
    U8 m_NR51{};  // 0xFF25: Sound panning
    U8 m_NR52{};  // 0xFF26: Sound on/off

    S32 m_FrameSequencerTimer{};
    S32 m_FrameSequencerStep{};
    S32 m_SampleTimer{};

    std::array<float, AudioBufferSize> m_AudioBuffer{};
    Size m_SampleIndex{};
};
