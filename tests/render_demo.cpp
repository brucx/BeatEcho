#include "beat_echo/engine.hpp"
#include "beat_echo/synth.hpp"
#include <cstdio>
#include <cstdint>
using namespace beat_echo;
static void u16(FILE* f, uint16_t n) { std::fputc(n&255,f); std::fputc(n>>8,f); }
static void u32(FILE* f, uint32_t n) { u16(f,n&65535); u16(f,n>>16); }
int main(int argc,char** argv) {
    const char* path=argc>1?argv[1]:"beat-echo-demo.wav";
    FILE* f=std::fopen(path,"wb"); if(!f) { std::perror(path); return 1; }
    Engine g; g.reset({120,0,0},42); g.select(0);
    const uint32_t frames=static_cast<uint32_t>((g.end_at()+500000)*kSampleRate/1000000);
    std::fwrite("RIFF",1,4,f); u32(f,36+frames*2); std::fwrite("WAVEfmt ",1,8,f);
    u32(f,16); u16(f,1); u16(f,1); u32(f,kSampleRate); u32(f,kSampleRate*2); u16(f,2); u16(f,16);
    std::fwrite("data",1,4,f); u32(f,frames*2);
    Synth synth; synth.set_volume(55); unsigned cue=0,note=0; bool result=false;
    for(uint32_t sample=0;sample<frames;++sample) {
        Time t=static_cast<Time>(sample)*1000000/kSampleRate;
        while(cue<g.schedule().count && g.schedule().cues[cue].at<=t) {
            auto c=g.schedule().cues[cue++]; synth.trigger(c.voice,c.velocity);
        }
        while(note<g.note_count() && g.repeat_start()+g.notes()[note].at<=t) {
            auto n=g.notes()[note++]; g.press(n.key,g.repeat_start()+n.at); synth.trigger(n.key);
        }
        g.tick(t);
        if(!result && g.phase()==Phase::Result) { synth.trigger(9); result=true; }
        u16(f,static_cast<uint16_t>(synth.next()));
    }
    const bool okay=!std::ferror(f); std::fclose(f);
    std::printf("Wrote %s (demonstration + scripted perfect response; not a hardware recording)\n",path);
    return okay?0:1;
}
