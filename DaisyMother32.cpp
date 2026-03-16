// DaisyMother32.cpp
// Moog Mother-32 emulator for Daisy Pod
//
// Signal chain: VCO1+VCO2 + Noise → Mixer → VCF (Moog Ladder) → VCA → Output
// Modulation:   ADSR → VCF (amount) + VCA
//               LFO  → VCO pitch / VCF cutoff
//
// -----------------------------------------------------------------------
// INTERFACE
// -----------------------------------------------------------------------
//  Encoder press:  cycle target → VCO1 → VCO2 → LFO → VCO1 …
//  Encoder rotate: cycle waveforms for the selected target
//                  VCO1/VCO2: Saw → Square
//                  LFO:       Sine → Triangle → Saw → Square
//
//  LED 1: shows selected target + waveform
//    VCO1 Saw    = Cyan      VCO1 Square = Blue
//    VCO2 Saw    = Yellow    VCO2 Square = Red
//    LFO Sine    = Green     LFO Tri     = Purple
//    LFO Saw     = Orange    LFO Square  = White
//
//  Knob 1: VCO1 pitch ±1 octave (centre = no offset)
//  Knob 2: VCO2 pitch ±1 octave (centre = unison with VCO1)
//
//  Button 1: Gate (hold = note on, release = note off; plays C4 when no MIDI)
//  Button 2: Hold mode toggle (latches gate; LED2 turns blue)
//
//  LED 2: shows envelope level (green), blue when hold mode is active
//
//  MIDI: NoteOn / NoteOff (with note-priority stack)
//        CC1=cutoff  CC21=res    CC22=glide  CC23=VCO1/VCO2 blend  CC24=LFO rate
//        CC25=VCO mod amt  CC26=VCF mod amt  CC27=attack  CC28=decay
//        CC29=VCO1 pitch ±1oct  CC30=VCO2 pitch ±1oct
//        CC31=VCO1 wave  CC32=VCO2 wave  CC33=LFO wave
//        CC44=VCO mod source (0=EG, 127=LFO)
//        CC45=VCO mod dest (0=pulse width, 127=frequency)
//        CC46=VCF mod source (0=LFO, 127=EG)  CC64=sustain pedal
// -----------------------------------------------------------------------

#include "daisysp.h"
#include "daisy_pod.h"

using namespace daisysp;
using namespace daisy;

// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
static DaisyPod pod;

// ---------------------------------------------------------------------------
// DSP objects
// ---------------------------------------------------------------------------
static Oscillator vco;     // Oscillator 1
static Oscillator vco2;    // Oscillator 2
static Oscillator lfo;     // Low-frequency oscillator
static MoogLadder vcf;     // Moog Ladder filter
static Adsr       env;     // ADSR envelope (controls VCF + VCA)
static WhiteNoise nse;     // Noise source

// ---------------------------------------------------------------------------
// Global parameter state
// ---------------------------------------------------------------------------
static float sample_rate;

// VCO1
static float vco_freq        = 261.63f;  // current (glided) frequency (C4)
static float vco_target_freq = 261.63f;  // target frequency
static float vco_pitch_mult  = 1.0f;     // ±1 octave from knob 1
static float vco_pw          = 0.5f;     // pulse width (0-1)
static float glide_time      = 0.0f;     // seconds (0 = off, CC22)

// VCO2
static float vco2_pitch_mult = 1.0f;     // ±1 octave from knob 2 (centre = unison)
static float vco_blend       = 0.5f;     // 0=VCO1 only, 1=VCO2 only (CC23)

// VCF
static float vcf_cutoff      = 8000.0f;
static float vcf_res         = 0.3f;
static float vcf_eg_amt      = 0.5f;     // how much mod opens filter (0-1)
static float vcf_kb_track    = 0.3f;     // keyboard tracking (0-1)

// ADSR
static float env_attack      = 0.01f;
static float env_decay       = 0.3f;
static float env_sustain     = 0.7f;
static float env_release     = 0.5f;

// LFO
static float lfo_rate        = 1.0f;     // Hz
static float lfo_amount      = 0.0f;     // 0-1

// VCO mod source / destination
static int   vco_mod_source  = 0;        // 0=LFO, 1=EG
static int   vco_mod_dest    = 1;        // 0=pulse width, 1=frequency

// VCF mod source
static int   vcf_mod_source  = 1;        // 0=LFO, 1=EG

// VCA
static float vca_level       = 0.8f;
static float velocity_scale  = 1.0f;     // last NoteOn velocity (sqrt curve)
static float noise_level     = 0.0f;

// Gate / hold
static bool  gate            = false;
static bool  hold_mode       = false;

// ---------------------------------------------------------------------------
// Encoder waveform selection
// ---------------------------------------------------------------------------
static int enc_target        = 0;        // 0=VCO1, 1=VCO2, 2=LFO
static const int NUM_TARGETS = 3;

static const int VCO_WAVES[] = {
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
};
static const int NUM_VCO_WAVES = 2;
static int vco_wave_idx      = 0;        // VCO1 waveform
static int vco2_wave_idx     = 0;        // VCO2 waveform

static const int LFO_WAVES[] = {
    Oscillator::WAVE_SIN,
    Oscillator::WAVE_TRI,
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
};
static const int NUM_LFO_WAVES = 4;
static int lfo_wave_idx      = 0;        // default: Sine

static const float VCO1_WAVE_COLORS[2][3] = {
    {0.0f, 1.0f, 1.0f},   // Saw    – Cyan
    {0.0f, 0.2f, 1.0f},   // Square – Blue
};
static const float VCO2_WAVE_COLORS[2][3] = {
    {1.0f, 1.0f, 0.0f},   // Saw    – Yellow
    {1.0f, 0.0f, 0.0f},   // Square – Red
};
static const float LFO_WAVE_COLORS[4][3] = {
    {0.0f, 1.0f, 0.0f},   // Sine   – Green
    {0.5f, 0.0f, 1.0f},   // Tri    – Purple
    {1.0f, 0.4f, 0.0f},   // Saw    – Orange
    {1.0f, 1.0f, 1.0f},   // Square – White
};

// Previous knob values for change detection
static float old_k1 = -1.0f;
static float old_k2 = -1.0f;
static const float KNOB_THRESHOLD = 0.003f;

// ---------------------------------------------------------------------------
// MIDI note stack (last-note priority with note-off tracking)
// ---------------------------------------------------------------------------
static int midi_note         = -1;
static int note_stack[16];
static int note_stack_len    = 0;

static void HandleNoteOn(int note, int vel)
{
    bool found = false;
    for(int i = 0; i < note_stack_len; i++)
        if(note_stack[i] == note) { found = true; break; }
    if(!found && note_stack_len < 16)
        note_stack[note_stack_len++] = note;

    midi_note       = note;
    vco_target_freq = mtof((float)note);
    if(glide_time < 0.001f)
        vco_freq = vco_target_freq;

    velocity_scale = sqrtf(vel / 127.0f);
    gate = true;
}

static void HandleNoteOff(int note)
{
    for(int i = 0; i < note_stack_len; i++)
    {
        if(note_stack[i] == note)
        {
            for(int j = i; j < note_stack_len - 1; j++)
                note_stack[j] = note_stack[j + 1];
            note_stack_len--;
            break;
        }
    }

    if(note_stack_len > 0)
    {
        midi_note       = note_stack[note_stack_len - 1];
        vco_target_freq = mtof((float)midi_note);
        if(glide_time < 0.001f)
            vco_freq = vco_target_freq;
    }
    else
    {
        midi_note = -1;
        if(!hold_mode)
            gate = false;
    }
}

// ---------------------------------------------------------------------------
// MIDI handler (called from main loop)
// ---------------------------------------------------------------------------
static void HandleMidi(MidiEvent m)
{
    switch(m.type)
    {
        case daisy::NoteOn:
        {
            NoteOnEvent p = m.AsNoteOn();
            if(p.velocity == 0)
                HandleNoteOff(p.note);
            else
                HandleNoteOn(p.note, p.velocity);
        }
        break;

        case daisy::NoteOff:
        {
            NoteOffEvent p = m.AsNoteOff();
            HandleNoteOff(p.note);
        }
        break;

        case ControlChange:
        {
            ControlChangeEvent p = m.AsControlChange();
            float val = p.value / 127.0f;
            switch(p.control_number)
            {
                case 1:  // Mod wheel → VCF cutoff
                    vcf_cutoff = fmap(val, 20.0f, 20000.0f, Mapping::LOG);
                    break;
                case 21: // VCF resonance
                    vcf_res = val * 0.95f;
                    break;
                case 22: // Glide time (0-2 s, quadratic)
                    glide_time = val * val * 2.0f;
                    break;
                case 23: // VCO1/VCO2 blend (0=VCO1 only, 127=VCO2 only)
                    vco_blend = val;
                    break;
                case 24: // LFO rate
                    lfo_rate = fmap(val, 0.2f, 350.0f, Mapping::LOG);
                    lfo.SetFreq(lfo_rate);
                    break;
                case 25: // VCO mod amount
                    lfo_amount = val;
                    break;
                case 26: // VCF mod amount
                    vcf_eg_amt = val;
                    break;
                case 27: // Envelope attack
                    env_attack = val * val * 5.0f;
                    env.SetAttackTime(env_attack);
                    break;
                case 28: // Envelope decay
                    env_decay = val * val * 5.0f;
                    env.SetDecayTime(env_decay);
                    break;
                case 29: // VCO1 pitch ±1 octave
                    vco_pitch_mult = powf(2.0f, (val - 0.5f) * 2.0f);
                    break;
                case 30: // VCO2 pitch ±1 octave
                    vco2_pitch_mult = powf(2.0f, (val - 0.5f) * 2.0f);
                    break;
                case 31: // VCO1 waveform (0-63=Saw, 64-127=Square)
                    vco_wave_idx = p.value * NUM_VCO_WAVES / 128;
                    break;
                case 32: // VCO2 waveform (0-63=Saw, 64-127=Square)
                    vco2_wave_idx = p.value * NUM_VCO_WAVES / 128;
                    break;
                case 33: // LFO waveform (0-31=Sine, 32-63=Tri, 64-95=Saw, 96-127=Square)
                    lfo_wave_idx = p.value * NUM_LFO_WAVES / 128;
                    lfo.SetWaveform(LFO_WAVES[lfo_wave_idx]);
                    break;
                case 44: // VCO mod source: 0=EG, 127=LFO
                    vco_mod_source = (p.value >= 64) ? 0 : 1;
                    break;
                case 45: // VCO mod destination: 0=pulse width, 127=frequency
                    vco_mod_dest = (p.value >= 64) ? 1 : 0;
                    break;
                case 46: // VCF mod source: 0=LFO, 127=EG
                    vcf_mod_source = (p.value >= 64) ? 1 : 0;
                    break;
                case 64: // Sustain pedal
                    hold_mode = (p.value >= 64);
                    if(!hold_mode && midi_note < 0)
                        gate = false;
                    break;
                default: break;
            }
        }
        break;

        default: break;
    }
}

// ---------------------------------------------------------------------------
// Audio callback
// ---------------------------------------------------------------------------
static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    pod.ProcessAllControls();

    // Encoder press: cycle VCO1 → VCO2 → LFO → VCO1
    if(pod.encoder.RisingEdge())
        enc_target = (enc_target + 1) % NUM_TARGETS;

    // Encoder rotate: cycle waveform for selected target
    int enc_inc = pod.encoder.Increment();
    if(enc_inc != 0)
    {
        if(enc_target == 0) // VCO1
        {
            vco_wave_idx = (vco_wave_idx + enc_inc + NUM_VCO_WAVES) % NUM_VCO_WAVES;
        }
        else if(enc_target == 1) // VCO2
        {
            vco2_wave_idx = (vco2_wave_idx + enc_inc + NUM_VCO_WAVES) % NUM_VCO_WAVES;
        }
        else // LFO
        {
            lfo_wave_idx = (lfo_wave_idx + enc_inc + NUM_LFO_WAVES) % NUM_LFO_WAVES;
            lfo.SetWaveform(LFO_WAVES[lfo_wave_idx]);
        }
    }

    // Button 1: gate (plays C4 when no MIDI active)
    if(pod.button1.RisingEdge())
    {
        if(midi_note < 0)
        {
            vco_target_freq = mtof(60.0f);
            vco_freq        = vco_target_freq;
        }
        gate = true;
    }
    if(pod.button1.FallingEdge())
    {
        if(midi_note < 0 && !hold_mode)
            gate = false;
    }

    // Button 2: toggle hold mode
    if(pod.button2.RisingEdge())
    {
        hold_mode = !hold_mode;
        if(!hold_mode && midi_note < 0)
            gate = false;
    }

    // Knob 1: VCO1 pitch ±1 octave
    // Knob 2: VCO2 pitch ±1 octave
    float k1 = pod.knob1.Process();
    float k2 = pod.knob2.Process();

    if(fabsf(k1 - old_k1) > KNOB_THRESHOLD)
    {
        vco_pitch_mult = powf(2.0f, (k1 - 0.5f) * 2.0f);
        old_k1 = k1;
    }
    if(fabsf(k2 - old_k2) > KNOB_THRESHOLD)
    {
        vco2_pitch_mult = powf(2.0f, (k2 - 0.5f) * 2.0f);
        old_k2 = k2;
    }

    // LED 1: colour reflects selected target + waveform
    if(enc_target == 0)
        pod.led1.Set(VCO1_WAVE_COLORS[vco_wave_idx][0],
                     VCO1_WAVE_COLORS[vco_wave_idx][1],
                     VCO1_WAVE_COLORS[vco_wave_idx][2]);
    else if(enc_target == 1)
        pod.led1.Set(VCO2_WAVE_COLORS[vco2_wave_idx][0],
                     VCO2_WAVE_COLORS[vco2_wave_idx][1],
                     VCO2_WAVE_COLORS[vco2_wave_idx][2]);
    else
        pod.led1.Set(LFO_WAVE_COLORS[lfo_wave_idx][0],
                     LFO_WAVE_COLORS[lfo_wave_idx][1],
                     LFO_WAVE_COLORS[lfo_wave_idx][2]);

    // --- Precompute glide coefficient (once per block) ---
    float glide_coeff = (glide_time > 0.001f)
        ? (1.0f - expf(-1.0f / (glide_time * sample_rate)))
        : 1.0f;

    // --- Sample loop ---
    float last_env = 0.0f;

    for(size_t i = 0; i < size; i += 2)
    {
        fonepole(vco_freq, vco_target_freq, glide_coeff);

        float env_val = env.Process(gate);
        last_env = env_val;

        float lfo_val = lfo.Process(); // -1 to +1

        float vco_mod_sig  = (vco_mod_source == 0) ? lfo_val : env_val;
        float vco_freq_mod = (vco_mod_dest == 1) ? vco_mod_sig * lfo_amount * vco_freq * 0.05f : 0.0f;
        float vco_pw_mod   = (vco_mod_dest == 0) ? vco_mod_sig * lfo_amount * 0.45f : 0.0f;

        // VCF mod: source selected by CC46, depth by CC26
        float vcf_mod_hz;
        if(vcf_mod_source == 0) // LFO
            vcf_mod_hz = lfo_val * vcf_eg_amt * (20000.0f - 20.0f) * 0.5f;
        else                    // EG: opens filter from cutoff up toward 20 kHz
            vcf_mod_hz = env_val * vcf_eg_amt * (20000.0f - vcf_cutoff);

        // --- VCO1 ---
        float modded_freq = (vco_freq + vco_freq_mod) * vco_pitch_mult;
        float modded_pw   = fclamp(vco_pw + vco_pw_mod, 0.05f, 0.95f);
        vco.SetWaveform(VCO_WAVES[vco_wave_idx]);
        vco.SetFreq(modded_freq);
        vco.SetPw(modded_pw);
        float vco1_sig = vco.Process();

        // --- VCO2 (independent waveform + pitch) ---
        vco2.SetWaveform(VCO_WAVES[vco2_wave_idx]);
        vco2.SetFreq((vco_freq + vco_freq_mod) * vco2_pitch_mult);
        vco2.SetPw(modded_pw);
        float vco2_sig = vco2.Process();

        // --- Mixer ---
        float osc_mix   = vco1_sig * (1.0f - vco_blend) + vco2_sig * vco_blend;
        float noise_sig = nse.Process() * noise_level;
        float mix       = osc_mix + noise_sig;

        // --- VCF ---
        float kb_offset = 0.0f;
        if(midi_note >= 0 && vcf_kb_track > 0.001f)
            kb_offset = vcf_kb_track * vcf_cutoff
                        * (powf(2.0f, (midi_note - 60) * (1.0f / 12.0f)) - 1.0f);

        float cutoff_mod = fclamp(vcf_cutoff + vcf_mod_hz + kb_offset, 20.0f, 20000.0f);
        vcf.SetFreq(cutoff_mod);
        vcf.SetRes(vcf_res);
        float filtered = vcf.Process(mix);

        // --- VCA ---
        float sig = filtered * fclamp(env_val, 0.0f, 1.0f) * vca_level * velocity_scale;

        out[i]     = sig;
        out[i + 1] = sig;
    }

    // LED 2: envelope brightness or hold mode
    if(hold_mode)
        pod.led2.Set(0.0f, 0.0f, 0.8f);
    else
        pod.led2.Set(0.0f, last_env, 0.0f);

    pod.UpdateLeds();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(4);
    sample_rate = pod.AudioSampleRate();

    vco.Init(sample_rate);
    vco.SetWaveform(VCO_WAVES[vco_wave_idx]);
    vco.SetFreq(261.63f);
    vco.SetAmp(1.0f);

    vco2.Init(sample_rate);
    vco2.SetWaveform(VCO_WAVES[vco2_wave_idx]);
    vco2.SetFreq(261.63f);
    vco2.SetAmp(1.0f);

    lfo.Init(sample_rate);
    lfo.SetWaveform(LFO_WAVES[lfo_wave_idx]);
    lfo.SetFreq(lfo_rate);
    lfo.SetAmp(1.0f);

    vcf.Init(sample_rate);
    vcf.SetFreq(vcf_cutoff);
    vcf.SetRes(vcf_res);

    env.Init(sample_rate);
    env.SetAttackTime(env_attack);
    env.SetDecayTime(env_decay);
    env.SetSustainLevel(env_sustain);
    env.SetReleaseTime(env_release);

    nse.Init();
    nse.SetAmp(1.0f);

    pod.midi.StartReceive();
    pod.StartAdc();
    pod.StartAudio(AudioCallback);

    while(1)
    {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidi(pod.midi.PopEvent());
    }
}
