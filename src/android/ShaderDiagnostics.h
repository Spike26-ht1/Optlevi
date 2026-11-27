#pragma once
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace modmenu::android::shaderdiagnostics {
class Journal {
    struct Entry { std::string text; };
    std::mutex mutex_;
    std::deque<Entry> entries_;
    std::size_t bytes_=0;
    std::string previous_;
    std::chrono::steady_clock::time_point lastObservation_[2]{};
    std::string lastWorldCandidate_;
public:
    static constexpr std::size_t maxBytes=96*1024, maxEntry=12*1024;
    void add(std::string const& category,std::string value,bool observation=false){
        if(value.size()>maxEntry){value.resize(maxEntry);value+=" [truncated]";}
        std::lock_guard lock(mutex_);
        auto now=std::chrono::steady_clock::now();
        // Keep bounded samples of actual draw observations; never log each draw.
        unsigned index=category=="World candidate observation"?1:0;
        if(observation&&lastObservation_[index]!=std::chrono::steady_clock::time_point{}&&
           now-lastObservation_[index]<std::chrono::seconds(2))return;
        auto key=category+": "+value;
        if(key==previous_)return;
        previous_=key;
        if(observation){
            lastObservation_[index]=now;
            if(index==1)lastWorldCandidate_=key;
        }
        auto seconds=std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto line=std::to_string(seconds)+" "+key+"\n";
        bytes_+=line.size();entries_.push_back({std::move(line)});
        while(bytes_>maxBytes||entries_.size()>48){
            bytes_-=entries_.front().text.size();entries_.pop_front();
        }
    }
    std::string snapshot(){
        std::lock_guard lock(mutex_);
        std::string out="OptLevi renderer diagnostics v1\nTimestamps: Unix seconds UTC\n";
        for(auto const& entry:entries_)out+=entry.text;
        if(!lastWorldCandidate_.empty())out+="\nLast observed world candidate (retained across menus):\n"+lastWorldCandidate_+"\n";
        return out;
    }
};
inline Journal& journal(){static Journal instance;return instance;}
inline void record(std::string const& category,std::string value,bool observation=false){
    journal().add(category,std::move(value),observation);
}
inline std::string snapshot(){return journal().snapshot();}
}
