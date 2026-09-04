#ifndef DRONE_COMMANDER_DSP_H
#define DRONE_COMMANDER_DSP_H

#include <stddef.h>

#define SYNTH_OSCILLATOR_COUNT 3
#define SYNTH_LFO_COUNT 3

typedef enum {
    WAVE_SINE,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_COUNT
} Waveform;

typedef struct {
    float phase;
    float frequency;
    float amplitude;
    Waveform waveform;
} Oscillator;

typedef struct {
    float integrator_1;
    float integrator_2;
} StateVariableFilter;

typedef struct {
    float frequency;
    float amplitude;
    Waveform waveform;
} OscillatorParameters;

typedef struct {
    float frequency;
    float amplitude;
    int sync_to_previous;
} LfoParameters;

typedef struct {
    OscillatorParameters oscillators[SYNTH_OSCILLATOR_COUNT];
    float cutoff;
    float cutoff_mod_depth;
    float resonance;
    float vca_amplitude;
    float vca_mod_depth;
    LfoParameters lfos[SYNTH_LFO_COUNT];
    float drive;
    float fm_depth;
    float blep_enabled;
} SynthParameters;

typedef struct {
    float sample_rate;
    Oscillator oscillators[SYNTH_OSCILLATOR_COUNT];
    Oscillator lfos[SYNTH_LFO_COUNT];
    StateVariableFilter filter;
    SynthParameters parameters;
    float smoothed_cutoff;
    float smoothed_resonance;
    float smoothed_vca_amp;
    float smoothed_drive;
    float smoothed_fm_depth;
    float smoothed_lfo_amplitudes[SYNTH_LFO_COUNT];
} Synth;

SynthParameters synth_default_parameters(void);
void synth_init(Synth *synth, float sample_rate, const SynthParameters *parameters);
void synth_set_parameters(Synth *synth, const SynthParameters *parameters);
float oscillator_process(Oscillator *oscillator, float sample_rate);
float oscillator_process_ex(Oscillator *oscillator, float sample_rate, float fm_offset, int blep);
float synth_process_sample(Synth *synth);
void synth_process(Synth *synth, float *output, size_t frame_count);
const char *waveform_name(Waveform waveform);

#endif