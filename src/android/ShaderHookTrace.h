#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace modmenu::android::shaderbridge {
// A fixed-size, allocation-free sequence for adjacent native render events.
// The sequence is evidence of GL ordering, NOT a Minecraft phase classifier.
enum class TraceKind : unsigned { Draw, Bind, Clear, Blit };
struct TraceEvent {
    TraceKind kind{};
    std::uint32_t a=0,b=0,c=0,d=0;
    bool operator==(TraceEvent const& other) const {
        return kind==other.kind&&a==other.a&&b==other.b&&c==other.c&&d==other.d;
    }
};
class HookTrace {
    struct Slot { TraceEvent event; unsigned repeats=0; };
    std::array<Slot,48> slots_{};
    unsigned size_=0,omitted_=0;
public:
    void add(TraceEvent event){
        if(size_&&slots_[size_-1].event==event){
            if(slots_[size_-1].repeats<1000000)++slots_[size_-1].repeats;
        }else if(size_<slots_.size())slots_[size_++]={event,1};
        else if(omitted_<1000000)++omitted_;
    }
    void reset(){size_=omitted_=0;}
    std::string report() const {
        std::string out;
        for(unsigned i=0;i<size_;++i){
            auto const& s=slots_[i];auto const& e=s.event;
            switch(e.kind){
            case TraceKind::Draw:out+="\n draw program="+std::to_string(e.a)+" FBO="+std::to_string(e.b)+" mode="+std::to_string(e.c)+" variant="+std::to_string(e.d);break;
            case TraceKind::Bind:out+="\n bind target="+std::to_string(e.a)+" from="+std::to_string(e.b)+" to="+std::to_string(e.c);break;
            case TraceKind::Clear:out+="\n clear FBO="+std::to_string(e.a)+" mask="+std::to_string(e.b);break;
            case TraceKind::Blit:out+="\n blit read="+std::to_string(e.a)+" draw="+std::to_string(e.b)+" mask="+std::to_string(e.c)+" filter="+std::to_string(e.d);break;
            }
            out+=" x"+std::to_string(s.repeats);
        }
        if(omitted_)out+="\n omitted events="+std::to_string(omitted_);
        return out;
    }
};
}
