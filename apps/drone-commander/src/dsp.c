#include "dsp.h"

#include <math.h>

#define PI_F 3.14159265358979323846f

static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float poly_blep(float t, float dt)
{
    if (dt <= 0.0f) {
        return 0.0f;
    }
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float waveform_sample(Waveform waveform, float phase, float dt, int blep)
{
    switch (waveform) {
    case WAVE_SAW: {
        float sample = 2.0f * phase - 1.0f;
        if (blep) {
            sample -= poly_blep(phase, dt);
        }
        return sample;
    }
    case WAVE_SQUARE: {
        float sample = phase < 0.5f ? 1.0f : -1.0f;
        if (blep) {
            float phase_half = phase + 0.5f;
            phase_half -= floorf(phase_half);
            sample += poly_blep(phase, dt);
            sample -= poly_blep(phase_half, dt);
        }
        return sample;
    }
    case WAVE_TRIANGLE:
        return 1.0f - 4.0f * fabsf(phase - 0.5f);
    case WAVE_SINE:
    default:
        return sinf(2.0f * PI_F * phase);
    }
}

SynthParameters synth_default_parameters(void)
{
    SynthParameters parameters = {
        .oscillators = {
            {110.0f, 0.70f, WAVE_SAW},
            {164.81f, 0.55f, WAVE_SAW},
            {220.0f, 0.40f, WAVE_SINE},
        },
        .cutoff = 1400.0f,
        .cutoff_mod_depth = 900.0f,
        .resonance = 0.25f,
        .vca_amplitude = 0.22f,
        .vca_mod_depth = 0.30f,
        .lfos = {
            {0.35f, 1.0f, 0},
            {0.20f, 0.0f, 0},
            {0.12f, 0.0f, 0},
        },
        .drive = 1.6f,
        .fm_depth = 0.0f,
        .blep_enabled = 1.0f,
    };
    return parameters;
}

void synth_init(Synth *synth, float sample_rate, const SynthParameters *parameters)
{
    int lfo_index;

    *synth = (Synth){0};
    synth->sample_rate = sample_rate;
    synth_set_parameters(synth, parameters);
    synth->smoothed_cutoff = synth->parameters.cutoff;
    synth->smoothed_resonance = synth->parameters.resonance;
    synth->smoothed_vca_amp = synth->parameters.vca_amplitude;
    synth->smoothed_drive = synth->parameters.drive;
    synth->smoothed_fm_depth = synth->parameters.fm_depth;
    for (lfo_index = 0; lfo_index < SYNTH_LFO_COUNT; ++lfo_index) {
        synth->smoothed_lfo_amplitudes[lfo_index] = synth->parameters.lfos[lfo_index].amplitude;
    }
}

void synth_set_parameters(Synth *synth, const SynthParameters *parameters)
{
    int oscillator_index;
    int lfo_index;

    synth->parameters = *parameters;
    for (oscillator_index = 0; oscillator_index < SYNTH_OSCILLATOR_COUNT; ++oscillator_index) {
        synth->oscillators[oscillator_index].frequency =
            clampf(parameters->oscillators[oscillator_index].frequency, 0.0f, synth->sample_rate * 0.45f);
        synth->oscillators[oscillator_index].amplitude =
            clampf(parameters->oscillators[oscillator_index].amplitude, 0.0f, 1.0f);
        synth->oscillators[oscillator_index].waveform = parameters->oscillators[oscillator_index].waveform;
    }

    for (lfo_index = 0; lfo_index < SYNTH_LFO_COUNT; ++lfo_index) {
        synth->lfos[lfo_index].frequency = clampf(parameters->lfos[lfo_index].frequency, 0.01f, 30.0f);
        synth->lfos[lfo_index].amplitude = 1.0f;
        synth->lfos[lfo_index].waveform = WAVE_SQUARE;
    }
}

float oscillator_process_ex(Oscillator *oscillator, float sample_rate, float fm_offset, int blep)
{
    float effective_freq = clampf(oscillator->frequency + fm_offset, 0.0f, sample_rate * 0.45f);
    float dt = effective_freq / sample_rate;
    float sample = oscillator->amplitude * waveform_sample(oscillator->waveform, oscillator->phase, dt, blep);
    oscillator->phase += dt;
    oscillator->phase -= floorf(oscillator->phase);
    return sample;
}

float oscillator_process(Oscillator *oscillator, float sample_rate)
{
    return oscillator_process_ex(oscillator, sample_rate, 0.0f, 0);
}

static float filter_process(StateVariableFilter *filter, float input, float cutoff,
                            float resonance, float sample_rate)
{
    float limited_cutoff = clampf(cutoff, 20.0f, sample_rate * 0.45f);
    float g = tanf(PI_F * limited_cutoff / sample_rate);
    float damping = 2.0f - 1.9f * clampf(resonance, 0.0f, 1.0f);
    float coefficient = 1.0f / (1.0f + g * (g + damping));
    float band = coefficient * (filter->integrator_1 + g * (input - filter->integrator_2));
    float low = filter->integrator_2 + g * band;

    filter->integrator_1 = 2.0f * band - filter->integrator_1;
    filter->integrator_2 = 2.0f * low - filter->integrator_2;
    return low;
}

float synth_process_sample(Synth *synth)
{
    float mixed = 0.0f;
    float modulation = 0.0f;
    float modulation_weight = 0.0f;
    float cutoff;
    float vca;
    float drive;
    int blep;
    float s1;
    float s2;
    float s3;
    int lfo_index;
    int previous_wrapped = 0;
    const float smooth_factor = 0.005f;

    synth->smoothed_cutoff += smooth_factor * (synth->parameters.cutoff - synth->smoothed_cutoff);
    synth->smoothed_resonance += smooth_factor * (synth->parameters.resonance - synth->smoothed_resonance);
    synth->smoothed_vca_amp += smooth_factor * (synth->parameters.vca_amplitude - synth->smoothed_vca_amp);
    synth->smoothed_drive += smooth_factor * (synth->parameters.drive - synth->smoothed_drive);
    synth->smoothed_fm_depth += smooth_factor * (synth->parameters.fm_depth - synth->smoothed_fm_depth);
    for (lfo_index = 0; lfo_index < SYNTH_LFO_COUNT; ++lfo_index) {
        synth->smoothed_lfo_amplitudes[lfo_index] += smooth_factor *
            (synth->parameters.lfos[lfo_index].amplitude - synth->smoothed_lfo_amplitudes[lfo_index]);
    }

    blep = synth->parameters.blep_enabled >= 0.5f;

    /* Cascaded Linear FM: Osc 1 -> Osc 2 -> Osc 3 */
    s1 = oscillator_process_ex(&synth->oscillators[0], synth->sample_rate, 0.0f, blep);
    s2 = oscillator_process_ex(&synth->oscillators[1], synth->sample_rate,
                               synth->smoothed_fm_depth * s1, blep);
    s3 = oscillator_process_ex(&synth->oscillators[2], synth->sample_rate,
                               synth->smoothed_fm_depth * s2, blep);

    mixed = (s1 + s2 + s3) / (float)SYNTH_OSCILLATOR_COUNT;

    drive = clampf(synth->smoothed_drive, 0.1f, 8.0f);
    mixed = tanhf(mixed * drive) / tanhf(drive);

    for (lfo_index = 0; lfo_index < SYNTH_LFO_COUNT; ++lfo_index) {
        float phase_before;
        float lfo_signal;

        if (lfo_index > 0 && synth->parameters.lfos[lfo_index].sync_to_previous && previous_wrapped) {
            synth->lfos[lfo_index].phase = 0.0f;
        }
        phase_before = synth->lfos[lfo_index].phase;
        lfo_signal = oscillator_process_ex(&synth->lfos[lfo_index], synth->sample_rate, 0.0f, 0);
        previous_wrapped = synth->lfos[lfo_index].phase < phase_before;
        modulation += lfo_signal * synth->smoothed_lfo_amplitudes[lfo_index];
        modulation_weight += synth->smoothed_lfo_amplitudes[lfo_index];
    }
    if (modulation_weight > 1.0f) {
        modulation /= modulation_weight;
    }

    cutoff = synth->smoothed_cutoff + modulation * synth->parameters.cutoff_mod_depth;
    mixed = filter_process(&synth->filter, mixed, cutoff, synth->smoothed_resonance,
                           synth->sample_rate);

    vca = synth->smoothed_vca_amp *
            (1.0f + modulation * clampf(synth->parameters.vca_mod_depth, 0.0f, 1.0f));
    return clampf(mixed * clampf(vca, 0.0f, 1.0f), -1.0f, 1.0f);
}

void synth_process(Synth *synth, float *output, size_t frame_count)
{
    size_t frame_index;
    for (frame_index = 0; frame_index < frame_count; ++frame_index) {
        output[frame_index] = synth_process_sample(synth);
    }
}

const char *waveform_name(Waveform waveform)
{
    static const char *names[WAVE_COUNT] = {"Sine", "Saw", "Square", "Triangle"};
    if (waveform < 0 || waveform >= WAVE_COUNT) {
        return "Unknown";
    }
    return names[waveform];
}