#include "daisy_petal.h"
#include "daisysp.h"
//from looper
#define MAX_SIZE (48000 * 60 * 5) // 5 minutes of floats at 48 khz
//from looper

using namespace daisy;
using namespace daisysp;

Flanger    flanger;
DaisyPetal hw;
Tone       tone;

//>>from looper
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
//<<from looper

//>>from reverb
Parameter vtime, vfreq, vsend;
bool      bypass;
ReverbSc  verb;
//<<from reverb

//>>from flanger
bool  effectOn = true;
float wet;
float deltarget, del; 
float lfotarget, lfo;
//<<from flanger

void AudioCallback(float *in, float *out, size_t size)
{
    float output = 0; //from looper
    float dryl, dryr, wetl, wetr, sendl, sendr; //from reverb
   

    hw.ProcessAllControls();

    float tone_freq = 200.0f; 
    tone.SetFreq(tone_freq); //set freq
    float tone_out;

    //>>from reverb
    //verb.SetFeedback(vtime.Process());
    //verb.SetLpFreq(vfreq.Process());
    verb.SetFeedback(0.95f); //guess the reverb time you need between 0.6f, 0.999f//0.9f isn't enough 0.95f probably too much
    verb.SetLpFreq(5000.0f); //dsmping between 500.0f, 20000.0f
    vsend.Process(); 
    if(hw.switches[DaisyPetal::SW_2].RisingEdge())
        bypass = !bypass;
    //<<from reverb

    //>>from flanger
    deltarget = hw.knob[2].Process();
    //flanger.SetFeedback(hw.knob[3].Process());
    flanger.SetFeedback(0.5f);
    float val = hw.knob[4].Process();
    flanger.SetLfoFreq(val * val * 10.f);
    lfotarget = hw.knob[5].Process();

    effectOn ^= hw.switches[0].RisingEdge();

    //encoder
    wet += hw.encoder.Increment() * .05f;
    wet = fclamp(wet, 0.f, 1.f);

    wet = hw.encoder.RisingEdge() ? .8f : wet; //set up default value after pressing encoder
    //>>from flanger

    drywet = hw.knob[0].Process(); //for looper
    float tone_ratio = hw.knob[3].Process(); //for tone

    UpdateButtons();

    for(size_t i = 0; i < size; i++)
    {
        fonepole(del, deltarget, .0001f); //smooth at audio rate LOW PASS FILTER
        flanger.SetDelay(del);

        fonepole(lfo, lfotarget, .0001f); //smooth at audio rate
        flanger.SetLfoDepth(lfo);
       
        tone_out = in[i] + tone.Process(in[i])*tone_ratio; //find proper coefficient
        out[i] = out[i + 1] = tone_out;

        //then if effect is on out is taken after flanger
        if(effectOn)
        {
            float sig = flanger.Process(tone_out);
            out[i] = out[i + 1] = sig * wet + tone_out * (1.f - wet);
        }
        //then we send it to reverb module
        dryl  = out[i];
        dryr  = out[i + 1];
        sendl = dryl * vsend.Value();
        sendr = dryr * vsend.Value();
        verb.Process(sendl, sendr, &wetl, &wetr);
        if(bypass)
        {
            out[i]     = dryl;     // left
            out[i + 1] = dryr; // right
        }
        else
        {
            out[i]     = dryl + wetl;
            out[i + 1] = dryr + wetr;
        }


        // and then we send resulting OUT into looper
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
    wet = .8f;//default value for flanger after power off/on

    tone.Init(sample_rate);

    //>>from reverb
    //vtime.Init(hw.knob[hw.KNOB_1], 0.6f, 0.999f, Parameter::LOGARITHMIC);//reverb time ---- fixed value see above
    //vfreq.Init(hw.knob[hw.KNOB_2], 500.0f, 20000.0f, Parameter::LOGARITHMIC);//damping ----- fixed value see above
    vsend.Init(hw.knob[hw.KNOB_2], 0.0f, 0.1f, Parameter::LINEAR);//send amount (was LINEAR)
    verb.Init(sample_rate);
    //<<from reverb

    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1)
    {
        hw.DelayMs(6);

        hw.ClearLeds();
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)0, (float)effectOn);
        hw.SetFootswitchLed(hw.FOOTSWITCH_LED_2, bypass ? 0.0f : 1.0f); //from reverb

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
        //>>from looper
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)3, play);
        hw.SetFootswitchLed((DaisyPetal::FootswitchLed)2, rec);
        //hw.SetFootswitchLed((DaisyPetal::FootswitchLed)1, true);//test
        //<<from looper
        hw.UpdateLeds();
    }
}

//>>from looper
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
    buf[pos] = buf[pos] * drywet + in[i];
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
        output = output * drywet + in[i];
    }
}
//<<from looper