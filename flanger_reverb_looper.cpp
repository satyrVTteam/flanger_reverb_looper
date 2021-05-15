#include "daisy_petal.h"
#include "daisysp.h"
//from looper
#define MAX_SIZE (48000 * 60 * 5) // 5 minutes of floats at 48 khz
//from looper

using namespace daisy;
using namespace daisysp;

Flanger    flanger;
DaisyPetal hw;

//from looper
bool first = true;  //first loop (sets length)
bool rec   = false; //currently recording
bool play  = false; //currently playing

int                 pos   = 0;
float DSY_SDRAM_BSS buf[MAX_SIZE];
int                 mod    = MAX_SIZE;
int                 len    = 0;
float               drywet = 0;
bool                res    = false;


void ResetBuffer();
void Controls();
void UpdateButtons();

void NextSamples(float &output, float *in, size_t i);
//from looper

bool  effectOn = true;
float wet;
float deltarget, del; 
float lfotarget, lfo;

void AudioCallback(float *in, float *out, size_t size)
{
    float output = 0; //from looper
    
    hw.ProcessAllControls();

    deltarget = hw.knob[2].Process();
    flanger.SetFeedback(hw.knob[3].Process());
    float val = hw.knob[4].Process();
    flanger.SetLfoFreq(val * val * 10.f);
    lfotarget = hw.knob[5].Process();

    effectOn ^= hw.switches[0].RisingEdge();

    //encoder
    wet += hw.encoder.Increment() * .05f;
    wet = fclamp(wet, 0.f, 1.f);

    wet = hw.encoder.RisingEdge() ? .9f : wet;

    //looper
    drywet = hw.knob[0].Process();
    UpdateButtons();

    for(size_t i = 0; i < size; i++)
    {
        fonepole(del, deltarget, .0001f); //smooth at audio rate LOW PASS FILTER
        flanger.SetDelay(del);

        fonepole(lfo, lfotarget, .0001f); //smooth at audio rate
        flanger.SetLfoDepth(lfo);

        out[i] = out[i + 1] = in[i];

        if(effectOn)
        {
            float sig = flanger.Process(in[i]);
            out[i] = out[i + 1] = sig * wet + in[i] * (1.f - wet);
        }

        NextSamples(output, out, i);
        out[i] = out[i + 1] = output;
    }
}

int main(void)
{
    hw.Init();
    ResetBuffer();//from looper
    float sample_rate = hw.AudioSampleRate();

    deltarget = del = 0.f;
    lfotarget = lfo = 0.f;
    flanger.Init(sample_rate);

    wet = .9f;

    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1)
    {
        hw.DelayMs(6);

        hw.ClearLeds();
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)0, (float)effectOn);

        int   wet_int  = (int)(wet * 8.f);
        float wet_frac = wet - wet_int;
        for(int i = 0; i < wet_int; i++)
        {
            hw.SetRingLed((DaisyPetal::RingLed)i, 1.f, 0.f, 0.f);
        }

        if(wet_int < 8)
        {
            hw.SetRingLed((DaisyPetal::RingLed)wet_int, wet_frac, 0.f, 0.f);
        }
        //from looper
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)3, play);
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)2, rec);
        //hw.SetFootswitchLed((DaisyPetal::FootswitchLed)1, true);//test
        //from looper
        hw.UpdateLeds();
    }
}

//from looper
//Resets the buffer
void ResetBuffer()
{
    play  = false;
    rec   = false;
    first = true;
    pos   = 0;
    len   = 0;
    for(int i = 0; i < mod; i++)
    {
        buf[i] = 0;
    }
    mod = MAX_SIZE;
}

void UpdateButtons()
{
    //switch1 pressed
    if(hw.switches[2].RisingEdge())
    {
        if(first && rec)
        {
            first = false;
            mod   = len;
            len   = 0;
        }

        res  = true;
        play = true;
        rec  = !rec;
    }

    //switch1 held
    if(hw.switches[2].TimeHeldMs() >= 1000 && res)
    {
        ResetBuffer();
        res = false;
    }

    //switch2 pressed and not empty buffer
    if(hw.switches[3].RisingEdge() && !(!rec && first))
    {
        play = !play;
        rec  = false;
    }
}

//Deals with analog controls

void WriteBuffer(float *in, size_t i)
{
    buf[pos] = buf[pos] * 0.5 + in[i] * 0.5;
    if(first)
    {
        len++;
    }
}

void NextSamples(float &output, float *in, size_t i)
{
    if(rec)
    {
        WriteBuffer(in, i);
    }

    output = buf[pos];

    //automatic looptime
    if(len >= MAX_SIZE)
    {
        first = false;
        mod   = MAX_SIZE;
        len   = 0;
    }

    if(play)
    {
        pos++;
        pos %= mod;
    }

    if(!rec)
    {
        output = output * drywet + in[i] * (1 - drywet);
    }
}