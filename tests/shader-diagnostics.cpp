#include "ShaderDiagnostics.h"
#include "ShaderHookTrace.h"
#include <cassert>
#include <iostream>
#include <thread>
int main(){
    using namespace modmenu::android::shaderdiagnostics;
    Journal j;
    j.add("world","candidate A",true);
    j.add("world","candidate B",true);
    j.add("World candidate observation","terrain-layout",true);
    j.add("status","missing provider");
    j.add("status","missing provider");
    auto s=j.snapshot();
    assert(s.find("candidate A")!=std::string::npos);
    assert(s.find("candidate B")==std::string::npos);
    assert(s.find("missing provider")==s.rfind("missing provider"));
    std::thread a([&]{for(int i=0;i<100;++i)j.add("A",std::to_string(i)+std::string(4000,'a'));});
    std::thread b([&]{for(int i=0;i<100;++i)j.add("B",std::to_string(i)+std::string(4000,'b'));});
    a.join();b.join();
    s=j.snapshot();assert(s.size()<Journal::maxBytes+Journal::maxEntry+256);
    assert(s.find("terrain-layout")!=std::string::npos);
    assert(s.find("A: 99")!=std::string::npos||s.find("B: 99")!=std::string::npos);
    namespace fs=std::filesystem;
    auto dir=fs::temp_directory_path()/("optlevi-log-test-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    auto read=[](fs::path const& p){
        std::ifstream in(p,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),{});
    };
    std::string error;
    Journal first;first.add("Lifecycle","first session");
    MediaLogWriter writer;
    assert(writer.save(dir,first,error));
    assert(read(dir/"latest.log").find("first session")!=std::string::npos);
    assert(!fs::exists(dir/"latest.log.tmp"));
    first.add("status","world input missing");
    assert(writer.save(dir,first,error));
    assert(read(dir/"latest.log").find("world input missing")!=std::string::npos);
    assert(!fs::exists(dir/"previous.log"));
    auto before=read(dir/"latest.log");
    Journal second;second.add("Lifecycle","second session");
    MediaLogWriter next;
    assert(next.save(dir,second,error));
    assert(read(dir/"previous.log")==before);
    assert(read(dir/"latest.log").find("second session")!=std::string::npos);
    // A blocked temporary path must not destroy the last good log.
    auto good=read(dir/"latest.log");fs::create_directory(dir/"latest.log.tmp");
    second.add("status","new sample");
    assert(!next.save(dir,second,error));
    assert(read(dir/"latest.log")==good);
    fs::remove_all(dir);
    using namespace modmenu::android::shaderbridge;
    HookTrace trace;
    trace.add({TraceKind::Bind,1,2,3,0});
    trace.add({TraceKind::Draw,7,3,4,2});
    trace.add({TraceKind::Draw,7,3,4,2});
    trace.add({TraceKind::Blit,3,0,16384,9728});
    auto order=trace.report();
    assert(order.find("bind target=1")<order.find("draw program=7"));
    assert(order.find("variant=2 x2")!=std::string::npos);
    assert(order.find("draw program=7")<order.find("blit read=3"));
    for(unsigned i=0;i<100;++i)trace.add({TraceKind::Clear,i,256,0,0});
    assert(trace.full()&&trace.report().find("omitted events=")!=std::string::npos);
    trace.omit();
    trace.reset();assert(trace.report().empty());
    std::cout<<"Bounded concurrent diagnostic journal, log persistence and hook trace: OK\n";
}
