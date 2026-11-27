#include "ShaderDiagnostics.h"
#include <cassert>
#include <iostream>
#include <thread>
int main(){
    using namespace modmenu::android::shaderdiagnostics;
    Journal j;
    j.add("world","candidate A",true);
    j.add("world","candidate B",true);
    j.add("status","missing provider");
    j.add("status","missing provider");
    auto s=j.snapshot();
    assert(s.find("candidate A")!=std::string::npos);
    assert(s.find("candidate B")==std::string::npos);
    assert(s.find("missing provider")==s.rfind("missing provider"));
    std::thread a([&]{for(int i=0;i<100;++i)j.add("A",std::to_string(i)+std::string(4000,'a'));});
    std::thread b([&]{for(int i=0;i<100;++i)j.add("B",std::to_string(i)+std::string(4000,'b'));});
    a.join();b.join();
    s=j.snapshot();assert(s.size()<Journal::maxBytes+128);
    assert(s.find("A: 99")!=std::string::npos||s.find("B: 99")!=std::string::npos);
    std::cout<<"Bounded concurrent diagnostic journal: OK\n";
}
