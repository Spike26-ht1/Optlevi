#pragma once
#include <chrono>
#include <deque>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace modmenu::android::shaderdiagnostics {
class Journal {
    struct Entry { std::string text; };
    std::mutex mutex_;
    std::deque<Entry> entries_;
    std::size_t bytes_=0;
    std::uint64_t revision_=0;
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
        bytes_+=line.size();entries_.push_back({std::move(line)});++revision_;
        while(bytes_>maxBytes||entries_.size()>48){
            bytes_-=entries_.front().text.size();entries_.pop_front();
        }
    }
    std::pair<std::uint64_t,std::string> versionedSnapshot(){
        std::lock_guard lock(mutex_);
        std::string out="OptLevi renderer diagnostics v1\nTimestamps: Unix seconds UTC\n";
        for(auto const& entry:entries_)out+=entry.text;
        if(!lastWorldCandidate_.empty())out+="\nLast observed world candidate (retained across menus):\n"+lastWorldCandidate_+"\n";
        return {revision_,std::move(out)};
    }
    std::string snapshot(){return versionedSnapshot().second;}
};
// Portable file sink, tested without JNI. Called by the Android lifecycle
// and throttled EGL frame path. A failed save never truncates latest.log.
class MediaLogWriter {
    std::filesystem::path directory_;
    bool rotated_=false;
    std::uint64_t savedRevision_=0;
    bool saved_=false;
public:
    bool save(std::filesystem::path const& directory,Journal& journal,std::string& error){
        namespace fs=std::filesystem;
        if(directory.empty()){error="No app media directory";return false;}
        if(directory_!=directory){
            directory_=directory;rotated_=false;saved_=false;
        }
        auto [revision,contents]=journal.versionedSnapshot();
        if(saved_&&savedRevision_==revision)return true;
        std::error_code ec;
        fs::create_directories(directory_,ec);
        if(ec){error="Create log directory: "+ec.message();return false;}
        auto latest=directory_/"latest.log",previous=directory_/"previous.log";
        auto temporary=directory_/"latest.log.tmp";
        if(!rotated_){
            bool exists=fs::exists(latest,ec);
            if(ec){error="Inspect previous session: "+ec.message();return false;}
            if(exists){
                fs::copy_file(latest,previous,fs::copy_options::overwrite_existing,ec);
                if(ec){error="Preserve previous log: "+ec.message();return false;}
            }
            rotated_=true;
        }
        {
            std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
            if(!stream){error="Open temporary renderer log failed";return false;}
            stream.write(contents.data(),static_cast<std::streamsize>(contents.size()));
            stream.close();
            if(!stream){
                fs::remove(temporary,ec);error="Write renderer log failed";return false;
            }
        }
        // Android/Linux rename replaces the old file atomically on the
        // same filesystem. Readers get a complete old or complete new log.
        fs::rename(temporary,latest,ec);
        if(ec){error="Publish renderer log: "+ec.message();std::error_code ignored;fs::remove(temporary,ignored);return false;}
        savedRevision_=revision;saved_=true;error.clear();return true;
    }
};
inline Journal& journal(){static Journal instance;return instance;}
inline void record(std::string const& category,std::string value,bool observation=false){
    journal().add(category,std::move(value),observation);
}
inline std::string snapshot(){return journal().snapshot();}
}
