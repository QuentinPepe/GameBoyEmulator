#include <apu.hpp>
#include <ostream>
#include <istream>
#include <state.hpp>

namespace {
    // Duty cycle patterns for square waves
    constexpr std::array<std::array<U8, 8>, 4> DutyPatterns = {{
        {0, 0, 0, 0, 0, 0, 0, 1},  // 12.5%
        {1, 0, 0, 0, 0, 0, 0, 1},  // 25%
        {1, 0, 0, 0, 0, 1, 1, 1},  // 50%
        {0, 1, 1, 1, 1, 1, 1, 0}   // 75%
    }};

    constexpr std::array<S32, 8> NoiseDivisors = {8, 16, 32, 48, 64, 80, 96, 112};
}

// ============================================================================
// Square Channel
// ============================================================================

void SquareChannel::Trigger(bool hasSweep) {
    enabled = true;

    if (lengthCounter == 0)
        lengthCounter = 64;

    frequencyTimer = (2048 - GetFrequency()) * 4;

    periodTimer = envelope & 0x07;
    currentVolume = (envelope >> 4) & 0x0F;
    envelopeRunning = true;

    dacEnabled = (envelope & 0xF8) != 0;
    if (!dacEnabled)
        enabled = false;

    if (hasSweep) {
        sweepFrequency = GetFrequency();
        S32 sweepPeriod = (sweep >> 4) & 0x07;
        S32 sweepShift = sweep & 0x07;
        sweepTimer = sweepPeriod == 0 ? 8 : sweepPeriod;
        sweepEnabled = sweepPeriod != 0 || sweepShift != 0;
        sweepNegate = false;

        // If shift != 0, calculate new frequency to check for overflow
        if (sweepShift != 0) {
            S32 newFreq = sweepFrequency >> sweepShift;
            if (sweep & 0x08)
                newFreq = sweepFrequency - newFreq;
            else
                newFreq = sweepFrequency + newFreq;

            if (newFreq > 2047)
                enabled = false;
        }
    }
}

void SquareChannel::ClockLength() {
    if ((freqHigh & 0x40) && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0)
            enabled = false;
    }
}

void SquareChannel::ClockEnvelope() {
    if (!envelopeRunning)
        return;

    S32 period = envelope & 0x07;
    if (period == 0)
        return;

    if (periodTimer > 0)
        periodTimer--;

    if (periodTimer == 0) {
        periodTimer = period;

        if ((envelope & 0x08) && currentVolume < 15) {
            currentVolume++;
        } else if (!(envelope & 0x08) && currentVolume > 0) {
            currentVolume--;
        } else {
            envelopeRunning = false;
        }
    }
}

void SquareChannel::ClockSweep() {
    if (sweepTimer > 0)
        sweepTimer--;

    if (sweepTimer == 0) {
        S32 sweepPeriod = (sweep >> 4) & 0x07;
        sweepTimer = sweepPeriod == 0 ? 8 : sweepPeriod;

        if (sweepEnabled && sweepPeriod != 0) {
            S32 sweepShift = sweep & 0x07;
            S32 delta = sweepFrequency >> sweepShift;
            S32 newFreq;

            if (sweep & 0x08) {
                newFreq = sweepFrequency - delta;
                sweepNegate = true;
            } else {
                newFreq = sweepFrequency + delta;
            }

            if (newFreq > 2047) {
                enabled = false;
            } else if (sweepShift != 0) {
                sweepFrequency = newFreq;
                freqLow = newFreq & 0xFF;
                freqHigh = (freqHigh & 0xF8) | ((newFreq >> 8) & 0x07);

                // Check again for overflow
                delta = newFreq >> sweepShift;
                if (sweep & 0x08)
                    newFreq = sweepFrequency - delta;
                else
                    newFreq = sweepFrequency + delta;

                if (newFreq > 2047)
                    enabled = false;
            }
        }
    }
}

S32 SquareChannel::GetFrequency() const {
    return freqLow | ((freqHigh & 0x07) << 8);
}

U8 SquareChannel::GetOutput() const {
    if (!enabled || !dacEnabled)
        return 0;

    U8 duty = (lengthDuty >> 6) & 0x03;
    return static_cast<U8>(DutyPatterns[duty][dutyPosition] * currentVolume);
}

// ============================================================================
// Wave Channel
// ============================================================================

void WaveChannel::Trigger() {
    enabled = true;

    if (lengthCounter == 0)
        lengthCounter = 256;

    frequencyTimer = (2048 - GetFrequency()) * 2;
    positionCounter = 0;

    if (!(dacEnable & 0x80))
        enabled = false;
}

void WaveChannel::ClockLength() {
    if ((freqHigh & 0x40) && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0)
            enabled = false;
    }
}

S32 WaveChannel::GetFrequency() const {
    return freqLow | ((freqHigh & 0x07) << 8);
}

U8 WaveChannel::GetOutput() const {
    if (!enabled || !(dacEnable & 0x80))
        return 0;

    U8 sampleByte = waveRAM[positionCounter / 2];
    U8 sample;
    if (positionCounter % 2 == 0)
        sample = (sampleByte >> 4) & 0x0F;  // High nibble
    else
        sample = sampleByte & 0x0F;          // Low nibble

    U8 volumeCode = (volume >> 5) & 0x03;
    switch (volumeCode) {
        case 0: sample = 0; break;           // Mute
        case 1: break;                        // 100%
        case 2: sample >>= 1; break;         // 50%
        case 3: sample >>= 2; break;         // 25%
    }

    return sample;
}

// ============================================================================
// Noise Channel
// ============================================================================

void NoiseChannel::Trigger() {
    enabled = true;

    if (lengthCounter == 0)
        lengthCounter = 64;

    frequencyTimer = GetDivisor() << ((polynomial >> 4) & 0x0F);

    periodTimer = envelope & 0x07;
    currentVolume = (envelope >> 4) & 0x0F;
    envelopeRunning = true;

    lfsr = 0x7FFF;

    dacEnabled = (envelope & 0xF8) != 0;
    if (!dacEnabled)
        enabled = false;
}

void NoiseChannel::ClockLength() {
    if ((control & 0x40) && lengthCounter > 0) {
        lengthCounter--;
        if (lengthCounter == 0)
            enabled = false;
    }
}

void NoiseChannel::ClockEnvelope() {
    if (!envelopeRunning)
        return;

    S32 period = envelope & 0x07;
    if (period == 0)
        return;

    if (periodTimer > 0)
        periodTimer--;

    if (periodTimer == 0) {
        periodTimer = period;

        if ((envelope & 0x08) && currentVolume < 15) {
            currentVolume++;
        } else if (!(envelope & 0x08) && currentVolume > 0) {
            currentVolume--;
        } else {
            envelopeRunning = false;
        }
    }
}

S32 NoiseChannel::GetDivisor() const {
    return NoiseDivisors[polynomial & 0x07];
}

U8 NoiseChannel::GetOutput() const {
    if (!enabled || !dacEnabled)
        return 0;

    // Output is inverted bit 0 of LFSR
    return static_cast<U8>((~lfsr & 1) * currentVolume);
}

// ============================================================================
// APU
// ============================================================================

APU::APU() {
    m_NR52 = 0xF1;  // Power on with sound enabled
}

void APU::Tick(U8 cycles) {
    if (!(m_NR52 & 0x80))
        return;

    for (U8 i = 0; i < cycles; i++) {
        TickChannels();

        m_FrameSequencerTimer++;
        if (m_FrameSequencerTimer >= CyclesPerFrameSequencer) {
            m_FrameSequencerTimer -= CyclesPerFrameSequencer;
            TickFrameSequencer();
        }

        m_SampleTimer++;
        if (m_SampleTimer >= CyclesPerSample) {
            m_SampleTimer -= CyclesPerSample;
            GenerateSample();
        }
    }
}

void APU::TickChannels() {
    // Channel 1 (Square with sweep)
    if (m_Channel1.frequencyTimer > 0)
        m_Channel1.frequencyTimer--;
    if (m_Channel1.frequencyTimer <= 0) {
        m_Channel1.frequencyTimer = (2048 - m_Channel1.GetFrequency()) * 4;
        m_Channel1.dutyPosition = (m_Channel1.dutyPosition + 1) & 7;
    }

    // Channel 2 (Square)
    if (m_Channel2.frequencyTimer > 0)
        m_Channel2.frequencyTimer--;
    if (m_Channel2.frequencyTimer <= 0) {
        m_Channel2.frequencyTimer = (2048 - m_Channel2.GetFrequency()) * 4;
        m_Channel2.dutyPosition = (m_Channel2.dutyPosition + 1) & 7;
    }

    // Channel 3 (Wave)
    if (m_Channel3.frequencyTimer > 0)
        m_Channel3.frequencyTimer--;
    if (m_Channel3.frequencyTimer <= 0) {
        m_Channel3.frequencyTimer = (2048 - m_Channel3.GetFrequency()) * 2;
        m_Channel3.positionCounter = (m_Channel3.positionCounter + 1) & 31;
    }

    // Channel 4 (Noise)
    if (m_Channel4.frequencyTimer > 0)
        m_Channel4.frequencyTimer--;
    if (m_Channel4.frequencyTimer <= 0) {
        m_Channel4.frequencyTimer = m_Channel4.GetDivisor() << ((m_Channel4.polynomial >> 4) & 0x0F);

        // Clock LFSR
        U8 xorResult = (m_Channel4.lfsr & 1) ^ ((m_Channel4.lfsr >> 1) & 1);
        m_Channel4.lfsr = (m_Channel4.lfsr >> 1) | (xorResult << 14);

        // 7-bit mode
        if (m_Channel4.polynomial & 0x08) {
            m_Channel4.lfsr &= ~(1 << 6);
            m_Channel4.lfsr |= xorResult << 6;
        }
    }
}

void APU::TickFrameSequencer() {
    // Step 0: Length          Step 4: Length
    // Step 2: Length, Sweep   Step 6: Length, Sweep
    // Step 7: Envelope

    switch (m_FrameSequencerStep) {
        case 0:
        case 4:
            m_Channel1.ClockLength();
            m_Channel2.ClockLength();
            m_Channel3.ClockLength();
            m_Channel4.ClockLength();
            break;
        case 2:
        case 6:
            m_Channel1.ClockLength();
            m_Channel2.ClockLength();
            m_Channel3.ClockLength();
            m_Channel4.ClockLength();
            m_Channel1.ClockSweep();
            break;
        case 7:
            m_Channel1.ClockEnvelope();
            m_Channel2.ClockEnvelope();
            m_Channel4.ClockEnvelope();
            break;
    }

    m_FrameSequencerStep = (m_FrameSequencerStep + 1) & 7;
}

void APU::GenerateSample() {
    if (m_SampleIndex >= AudioBufferSize)
        return;

    m_AudioBuffer[m_SampleIndex++] = MixChannels();
}

float APU::MixChannels() const {
    if (!(m_NR52 & 0x80))
        return 0.0f;

    S32 ch1 = m_Channel1.GetOutput();
    S32 ch2 = m_Channel2.GetOutput();
    S32 ch3 = m_Channel3.GetOutput();
    S32 ch4 = m_Channel4.GetOutput();

    S32 left = 0, right = 0;

    if (m_NR51 & 0x10) left += ch1;
    if (m_NR51 & 0x20) left += ch2;
    if (m_NR51 & 0x40) left += ch3;
    if (m_NR51 & 0x80) left += ch4;

    if (m_NR51 & 0x01) right += ch1;
    if (m_NR51 & 0x02) right += ch2;
    if (m_NR51 & 0x04) right += ch3;
    if (m_NR51 & 0x08) right += ch4;

    // Master volume (0-7 per channel)
    S32 leftVol = ((m_NR50 >> 4) & 0x07) + 1;
    S32 rightVol = (m_NR50 & 0x07) + 1;

    left = (left * leftVol) / 8;
    right = (right * rightVol) / 8;

    // Mix to mono, normalize to -1.0..1.0
    // Max per channel = 15, max total = 60, with volume = 60
    float sample = static_cast<float>(left + right) / 120.0f;

    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;

    return sample;
}

std::optional<U8> APU::Read(U16 address) const {
    switch (address) {
        // Channel 1 (Square with sweep)
        case 0xFF10: return m_Channel1.sweep | 0x80;
        case 0xFF11: return m_Channel1.lengthDuty | 0x3F;
        case 0xFF12: return m_Channel1.envelope;
        case 0xFF13: return 0xFF;  // Write-only
        case 0xFF14: return m_Channel1.freqHigh | 0xBF;

        // Channel 2 (Square)
        case 0xFF15: return 0xFF;  // Not used
        case 0xFF16: return m_Channel2.lengthDuty | 0x3F;
        case 0xFF17: return m_Channel2.envelope;
        case 0xFF18: return 0xFF;  // Write-only
        case 0xFF19: return m_Channel2.freqHigh | 0xBF;

        // Channel 3 (Wave)
        case 0xFF1A: return m_Channel3.dacEnable | 0x7F;
        case 0xFF1B: return 0xFF;  // Write-only
        case 0xFF1C: return m_Channel3.volume | 0x9F;
        case 0xFF1D: return 0xFF;  // Write-only
        case 0xFF1E: return m_Channel3.freqHigh | 0xBF;

        // Channel 4 (Noise)
        case 0xFF1F: return 0xFF;  // Not used
        case 0xFF20: return 0xFF;  // Write-only
        case 0xFF21: return m_Channel4.envelope;
        case 0xFF22: return m_Channel4.polynomial;
        case 0xFF23: return m_Channel4.control | 0xBF;

        // Master control
        case 0xFF24: return m_NR50;
        case 0xFF25: return m_NR51;
        case 0xFF26: {
            U8 result = m_NR52 | 0x70;
            if (m_Channel1.enabled) result |= 0x01;
            if (m_Channel2.enabled) result |= 0x02;
            if (m_Channel3.enabled) result |= 0x04;
            if (m_Channel4.enabled) result |= 0x08;
            return result;
        }

        // Wave RAM
        default:
            if (address >= 0xFF30 && address <= 0xFF3F) {
                return m_Channel3.waveRAM[address - 0xFF30];
            }
            return std::nullopt;
    }
}

namespace {

void SaveSquareChannel(std::ostream& out, const SquareChannel& ch) {
    state::Write(out, ch.sweep);
    state::Write(out, ch.lengthDuty);
    state::Write(out, ch.envelope);
    state::Write(out, ch.freqLow);
    state::Write(out, ch.freqHigh);
    state::Write(out, ch.enabled);
    state::Write(out, ch.dacEnabled);
    state::Write(out, ch.frequencyTimer);
    state::Write(out, ch.dutyPosition);
    state::Write(out, ch.lengthCounter);
    state::Write(out, ch.periodTimer);
    state::Write(out, ch.currentVolume);
    state::Write(out, ch.envelopeRunning);
    state::Write(out, ch.sweepEnabled);
    state::Write(out, ch.sweepFrequency);
    state::Write(out, ch.sweepTimer);
    state::Write(out, ch.sweepNegate);
}

void LoadSquareChannel(std::istream& in, SquareChannel& ch) {
    state::Read(in, ch.sweep);
    state::Read(in, ch.lengthDuty);
    state::Read(in, ch.envelope);
    state::Read(in, ch.freqLow);
    state::Read(in, ch.freqHigh);
    state::Read(in, ch.enabled);
    state::Read(in, ch.dacEnabled);
    state::Read(in, ch.frequencyTimer);
    state::Read(in, ch.dutyPosition);
    state::Read(in, ch.lengthCounter);
    state::Read(in, ch.periodTimer);
    state::Read(in, ch.currentVolume);
    state::Read(in, ch.envelopeRunning);
    state::Read(in, ch.sweepEnabled);
    state::Read(in, ch.sweepFrequency);
    state::Read(in, ch.sweepTimer);
    state::Read(in, ch.sweepNegate);
}

} // anonymous namespace

bool APU::Write(U16 address, U8 value) {
    // If APU is off, only NR52 and wave RAM can be written
    if (!(m_NR52 & 0x80) && address != 0xFF26 && (address < 0xFF30 || address > 0xFF3F)) {
        return address >= 0xFF10 && address <= 0xFF3F;
    }

    switch (address) {
        // Channel 1 (Square with sweep)
        case 0xFF10:
            m_Channel1.sweep = value;
            return true;
        case 0xFF11:
            m_Channel1.lengthDuty = value;
            m_Channel1.lengthCounter = 64 - (value & 0x3F);
            return true;
        case 0xFF12:
            m_Channel1.envelope = value;
            m_Channel1.dacEnabled = (value & 0xF8) != 0;
            if (!m_Channel1.dacEnabled)
                m_Channel1.enabled = false;
            return true;
        case 0xFF13:
            m_Channel1.freqLow = value;
            return true;
        case 0xFF14:
            m_Channel1.freqHigh = value;
            if (value & 0x80)
                m_Channel1.Trigger(true);
            return true;

        // Channel 2 (Square)
        case 0xFF15: return true;  // Not used
        case 0xFF16:
            m_Channel2.lengthDuty = value;
            m_Channel2.lengthCounter = 64 - (value & 0x3F);
            return true;
        case 0xFF17:
            m_Channel2.envelope = value;
            m_Channel2.dacEnabled = (value & 0xF8) != 0;
            if (!m_Channel2.dacEnabled)
                m_Channel2.enabled = false;
            return true;
        case 0xFF18:
            m_Channel2.freqLow = value;
            return true;
        case 0xFF19:
            m_Channel2.freqHigh = value;
            if (value & 0x80)
                m_Channel2.Trigger(false);
            return true;

        // Channel 3 (Wave)
        case 0xFF1A:
            m_Channel3.dacEnable = value;
            if (!(value & 0x80))
                m_Channel3.enabled = false;
            return true;
        case 0xFF1B:
            m_Channel3.length = value;
            m_Channel3.lengthCounter = 256 - value;
            return true;
        case 0xFF1C:
            m_Channel3.volume = value;
            return true;
        case 0xFF1D:
            m_Channel3.freqLow = value;
            return true;
        case 0xFF1E:
            m_Channel3.freqHigh = value;
            if (value & 0x80)
                m_Channel3.Trigger();
            return true;

        // Channel 4 (Noise)
        case 0xFF1F: return true;  // Not used
        case 0xFF20:
            m_Channel4.length = value;
            m_Channel4.lengthCounter = 64 - (value & 0x3F);
            return true;
        case 0xFF21:
            m_Channel4.envelope = value;
            m_Channel4.dacEnabled = (value & 0xF8) != 0;
            if (!m_Channel4.dacEnabled)
                m_Channel4.enabled = false;
            return true;
        case 0xFF22:
            m_Channel4.polynomial = value;
            return true;
        case 0xFF23:
            m_Channel4.control = value;
            if (value & 0x80)
                m_Channel4.Trigger();
            return true;

        // Master control
        case 0xFF24:
            m_NR50 = value;
            return true;
        case 0xFF25:
            m_NR51 = value;
            return true;
        case 0xFF26:
            // Only bit 7 is writable
            if (!(value & 0x80) && (m_NR52 & 0x80)) {
                // Turning APU off - reset all registers
                m_Channel1 = SquareChannel{};
                m_Channel2 = SquareChannel{};
                m_Channel3.dacEnable = 0;
                m_Channel3.length = 0;
                m_Channel3.volume = 0;
                m_Channel3.freqLow = 0;
                m_Channel3.freqHigh = 0;
                m_Channel3.enabled = false;
                m_Channel4 = NoiseChannel{};
                m_NR50 = 0;
                m_NR51 = 0;
            }
            m_NR52 = (m_NR52 & 0x0F) | (value & 0x80);
            return true;

        // Wave RAM
        default:
            if (address >= 0xFF30 && address <= 0xFF3F) {
                m_Channel3.waveRAM[address - 0xFF30] = value;
                return true;
            }
            return false;
    }
}

void APU::SaveState(std::ostream& out) const
{
    SaveSquareChannel(out, m_Channel1);
    SaveSquareChannel(out, m_Channel2);

    // Wave channel
    state::Write(out, m_Channel3.dacEnable);
    state::Write(out, m_Channel3.length);
    state::Write(out, m_Channel3.volume);
    state::Write(out, m_Channel3.freqLow);
    state::Write(out, m_Channel3.freqHigh);
    state::Write(out, m_Channel3.waveRAM);
    state::Write(out, m_Channel3.enabled);
    state::Write(out, m_Channel3.frequencyTimer);
    state::Write(out, m_Channel3.positionCounter);
    state::Write(out, m_Channel3.lengthCounter);
    state::Write(out, m_Channel3.sampleBuffer);

    // Noise channel
    state::Write(out, m_Channel4.length);
    state::Write(out, m_Channel4.envelope);
    state::Write(out, m_Channel4.polynomial);
    state::Write(out, m_Channel4.control);
    state::Write(out, m_Channel4.enabled);
    state::Write(out, m_Channel4.dacEnabled);
    state::Write(out, m_Channel4.frequencyTimer);
    state::Write(out, m_Channel4.lengthCounter);
    state::Write(out, m_Channel4.periodTimer);
    state::Write(out, m_Channel4.currentVolume);
    state::Write(out, m_Channel4.envelopeRunning);
    state::Write(out, m_Channel4.lfsr);

    // Master control
    state::Write(out, m_NR50);
    state::Write(out, m_NR51);
    state::Write(out, m_NR52);
    state::Write(out, m_FrameSequencerTimer);
    state::Write(out, m_FrameSequencerStep);
    state::Write(out, m_SampleTimer);
}

void APU::LoadState(std::istream& in)
{
    LoadSquareChannel(in, m_Channel1);
    LoadSquareChannel(in, m_Channel2);

    // Wave channel
    state::Read(in, m_Channel3.dacEnable);
    state::Read(in, m_Channel3.length);
    state::Read(in, m_Channel3.volume);
    state::Read(in, m_Channel3.freqLow);
    state::Read(in, m_Channel3.freqHigh);
    state::Read(in, m_Channel3.waveRAM);
    state::Read(in, m_Channel3.enabled);
    state::Read(in, m_Channel3.frequencyTimer);
    state::Read(in, m_Channel3.positionCounter);
    state::Read(in, m_Channel3.lengthCounter);
    state::Read(in, m_Channel3.sampleBuffer);

    // Noise channel
    state::Read(in, m_Channel4.length);
    state::Read(in, m_Channel4.envelope);
    state::Read(in, m_Channel4.polynomial);
    state::Read(in, m_Channel4.control);
    state::Read(in, m_Channel4.enabled);
    state::Read(in, m_Channel4.dacEnabled);
    state::Read(in, m_Channel4.frequencyTimer);
    state::Read(in, m_Channel4.lengthCounter);
    state::Read(in, m_Channel4.periodTimer);
    state::Read(in, m_Channel4.currentVolume);
    state::Read(in, m_Channel4.envelopeRunning);
    state::Read(in, m_Channel4.lfsr);

    // Master control
    state::Read(in, m_NR50);
    state::Read(in, m_NR51);
    state::Read(in, m_NR52);
    state::Read(in, m_FrameSequencerTimer);
    state::Read(in, m_FrameSequencerStep);
    state::Read(in, m_SampleTimer);

    m_SampleIndex = 0;
}
