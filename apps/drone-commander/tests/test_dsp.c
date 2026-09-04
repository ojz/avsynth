#include "dsp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_SAMPLE_RATE 48000.0f
#define TEST_FRAME_COUNT 48000

static int failures = 0;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void test_phase_wraps(void)
{
    Oscillator oscillator = {.phase = 0.99f, .frequency = 1000.0f,
                             .amplitude = 1.0f, .waveform = WAVE_SINE};
    oscillator_process(&oscillator, TEST_SAMPLE_RATE);
    expect_true(oscillator.phase >= 0.0f && oscillator.phase < 1.0f,
                "oscillator phase stays in [0, 1)");
}

static void test_output_is_bounded_and_finite(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth synth;
    int frame_index;

    parameters.drive = 8.0f;
    parameters.resonance = 1.0f;
    parameters.vca_amplitude = 1.0f;
    synth_init(&synth, TEST_SAMPLE_RATE, &parameters);

    for (frame_index = 0; frame_index < TEST_FRAME_COUNT; ++frame_index) {
        float sample = synth_process_sample(&synth);
        expect_true(isfinite(sample), "synth output is finite");
        expect_true(sample >= -1.0f && sample <= 1.0f, "synth output is bounded");
        if (failures != 0) {
            return;
        }
    }
}

static void test_render_is_deterministic(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth first;
    Synth second;
    float first_output[512];
    float second_output[512];

    synth_init(&first, TEST_SAMPLE_RATE, &parameters);
    synth_init(&second, TEST_SAMPLE_RATE, &parameters);
    synth_process(&first, first_output, 512);
    synth_process(&second, second_output, 512);
    expect_true(memcmp(first_output, second_output, sizeof(first_output)) == 0,
                "identical synth state renders identical samples");
}

static void test_silent_oscillators_are_silent(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth synth;
    int oscillator_index;
    int frame_index;

    for (oscillator_index = 0; oscillator_index < SYNTH_OSCILLATOR_COUNT; ++oscillator_index) {
        parameters.oscillators[oscillator_index].amplitude = 0.0f;
    }
    synth_init(&synth, TEST_SAMPLE_RATE, &parameters);
    for (frame_index = 0; frame_index < 1024; ++frame_index) {
        expect_true(synth_process_sample(&synth) == 0.0f, "zero oscillator gain produces silence");
    }
}

static void test_polyblep_and_fm_stability(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth synth;
    int frame_index;

    parameters.fm_depth = 1000.0f;
    parameters.blep_enabled = 1.0f;
    parameters.drive = 4.0f;
    parameters.oscillators[0].waveform = WAVE_SAW;
    parameters.oscillators[1].waveform = WAVE_SQUARE;
    parameters.oscillators[2].waveform = WAVE_TRIANGLE;

    synth_init(&synth, TEST_SAMPLE_RATE, &parameters);
    for (frame_index = 0; frame_index < TEST_FRAME_COUNT; ++frame_index) {
        float sample = synth_process_sample(&synth);
        expect_true(isfinite(sample), "PolyBLEP + FM output is finite");
        expect_true(sample >= -1.0f && sample <= 1.0f, "PolyBLEP + FM output is bounded");
        if (failures != 0) {
            return;
        }
    }
}

static void test_parameter_smoothing(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth synth;
    float initial_cutoff = parameters.cutoff;

    synth_init(&synth, TEST_SAMPLE_RATE, &parameters);
    expect_true(synth.smoothed_cutoff == initial_cutoff, "smoothed cutoff initialized");

    parameters.cutoff = 8000.0f;
    synth_set_parameters(&synth, &parameters);
    synth_process_sample(&synth);

    expect_true(synth.smoothed_cutoff > initial_cutoff, "smoothed cutoff advances toward target");
    expect_true(synth.smoothed_cutoff < 8000.0f, "smoothed cutoff ramps smoothly without jumping");
}

static void test_lfo_sync_to_previous(void)
{
    SynthParameters parameters = synth_default_parameters();
    Synth synced;
    Synth free_running;
    int frame_index;

    parameters.lfos[0].frequency = 30.0f;
    parameters.lfos[1].frequency = 1.0f;
    parameters.lfos[1].sync_to_previous = 1;
    parameters.lfos[2].amplitude = 0.0f;
    synth_init(&synced, TEST_SAMPLE_RATE, &parameters);

    parameters.lfos[1].sync_to_previous = 0;
    synth_init(&free_running, TEST_SAMPLE_RATE, &parameters);

    for (frame_index = 0; frame_index < 1601; ++frame_index) {
        synth_process_sample(&synced);
        synth_process_sample(&free_running);
    }

    expect_true(synced.lfos[1].phase < 0.001f,
                "synced LFO phase resets when the preceding LFO wraps");
    expect_true(free_running.lfos[1].phase > 0.03f,
                "free-running LFO does not reset with the preceding LFO");
}

/* The fraction of a cycle a naive square spends high, which is what pulse
 * width means. Anti-aliasing is off here so the edges land where they should
 * rather than being rounded by the correction. */
static float duty_of(float pulse_width)
{
    Oscillator oscillator = {.frequency = 100.0f, .amplitude = 1.0f,
                             .waveform = WAVE_SQUARE, .pulse_width = pulse_width};
    int high = 0;
    for (int i = 0; i < TEST_FRAME_COUNT; ++i)
        if (oscillator_process_ex(&oscillator, TEST_SAMPLE_RATE, 0.0f, 0) > 0.0f) ++high;
    return (float)high / (float)TEST_FRAME_COUNT;
}

static void test_pulse_width(void)
{
    expect_true(fabsf(duty_of(0.5f) - 0.5f) < 0.01f, "a 0.5 duty square is high half the time");
    expect_true(fabsf(duty_of(0.25f) - 0.25f) < 0.01f, "a 0.25 duty square is high a quarter of the time");
    expect_true(fabsf(duty_of(0.75f) - 0.75f) < 0.01f, "a 0.75 duty square is high three quarters of the time");

    /* An oscillator built without naming pulse_width must still be a square,
     * not silence, so old code and test fixtures keep working. */
    expect_true(fabsf(duty_of(0.0f) - 0.5f) < 0.01f, "an unset pulse width behaves as 0.5");

    /* Pulse width must not break the anti-aliased path or the bounds. */
    SynthParameters parameters = synth_default_parameters();
    Synth synth;
    parameters.oscillators[0].waveform = WAVE_SQUARE;
    parameters.oscillators[0].pulse_width = 0.08f;
    parameters.oscillators[1].waveform = WAVE_SQUARE;
    parameters.oscillators[1].pulse_width = 0.92f;
    parameters.vca_amplitude = 1.0f;
    synth_init(&synth, TEST_SAMPLE_RATE, &parameters);
    for (int i = 0; i < TEST_FRAME_COUNT; ++i) {
        float sample = synth_process_sample(&synth);
        expect_true(sample >= -1.0f && sample <= 1.0f && sample == sample,
                    "extreme pulse widths stay bounded and finite");
        if (failures) break;
    }
}

int main(void)
{
    test_phase_wraps();
    test_output_is_bounded_and_finite();
    test_render_is_deterministic();
    test_silent_oscillators_are_silent();
    test_polyblep_and_fm_stability();
    test_parameter_smoothing();
    test_lfo_sync_to_previous();
    test_pulse_width();

    if (failures == 0) {
        puts("All DSP tests passed.");
        return 0;
    }
    fprintf(stderr, "%d DSP test(s) failed.\n", failures);
    return 1;
}