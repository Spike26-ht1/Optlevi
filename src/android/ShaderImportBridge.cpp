#include "ShaderImportBridge.h"
#include "platform/Jni.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <jni.h>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace modmenu::android::shaderimport {
namespace {
namespace fs = std::filesystem;
std::atomic<bool> enabled{false};
std::atomic<bool> registered{false};
std::atomic<bool> pending{false};
std::mutex resultMutex;
Result lastResult;
bool hasResult=false;
constexpr std::uintmax_t kMaxPackSize=64ULL*1024*1024;

std::string str(JNIEnv* env,jstring value) {
    if(!value) return {};
    const char* ptr=env->GetStringUTFChars(value,nullptr);
    if(!ptr)return {};
    std::string s(ptr);
    env->ReleaseStringUTFChars(value,ptr);
    return s;
}

void report(bool ok,std::string message) {
    std::lock_guard lock(resultMutex);
    lastResult={ok,std::move(message)};
    hasResult=true;
}

bool ends(std::string const& s,std::string const& ext) {
    if(s.size()<ext.size())return false;
    auto begin=s.end()-static_cast<std::ptrdiff_t>(ext.size());
    return std::equal(begin,s.end(),ext.begin(),ext.end(),
                      [](char a,char b){return std::tolower((unsigned char)a)==b;});
}

std::string safeName(std::string const& display) {
    std::string name=fs::path(display).filename().string();
    if(name.size()>120) name=name.substr(name.size()-120);
    for(char& c:name)if(!(std::isalnum((unsigned char)c)||c=='.'||c=='-'||c=='_'))
        c='_';
    if(name.empty()||name.front()=='.'||name==".."||name.find("..")!=std::string::npos)
        return {};
    return name;
}

bool isZip(fs::path const& file) {
    std::ifstream in(file,std::ios::binary);
    char sig[4]{};
    in.read(sig,4);
    return in.gcount()==4 && sig[0]=='P' && sig[1]=='K' &&
           ((sig[2]==3 && sig[3]==4)||(sig[2]==5 && sig[3]==6));
}

void JNICALL received(JNIEnv* env,jclass,jboolean success,jstring path,
                      jstring displayName,jstring error) {
    // The Activity deletes its temporary file as soon as we return.
    if(!enabled.load()||!pending.exchange(false))return;
    if(success!=JNI_TRUE) {
        report(false,str(env,error).empty()?"Import cancelled":str(env,error));
        return;
    }
    fs::path input(str(env,path));
    std::string name=safeName(str(env,displayName));
    if(name.empty()||!(ends(name,".zip")||ends(name,".mcpack")||
                       ends(name,".material.bin"))) {
        report(false,"Choose a .zip, .mcpack or .material.bin shader pack");
        return;
    }
    std::error_code ec;
    if(!fs::is_regular_file(input,ec)||ec) {
        report(false,"Android picker did not provide a readable file");
        return;
    }
    auto size=fs::file_size(input,ec);
    if(ec||size==0||size>kMaxPackSize) {
        report(false,"File is empty or exceeds the 64 MB import limit");
        return;
    }
    if((ends(name,".zip")||ends(name,".mcpack"))&&!isZip(input)) {
        report(false,"Selected file is not a ZIP archive");
        return;
    }
    fs::path destination=paths::selfDir()/"shaderpacks";
    fs::create_directories(destination,ec);
    if(ec) {report(false,"Could not create shaderpacks directory: "+ec.message());return;}
    fs::path candidate=destination/name;
    for(int i=1;fs::exists(candidate,ec)&&i<=999;i++) {
        auto dot=name.rfind('.');
        std::string stem=dot==std::string::npos?name:name.substr(0,dot);
        std::string ext=dot==std::string::npos?"":name.substr(dot);
        candidate=destination/(stem+"-"+std::to_string(i)+ext);
    }
    if(ec||fs::exists(candidate,ec)) {
        report(false,"Could not choose a safe destination name");return;
    }
    fs::copy_file(input,candidate,fs::copy_options::none,ec);
    if(ec||fs::file_size(candidate,ec)!=size) {
        fs::remove(candidate,ec);
        report(false,"Failed to import the selected shader file");
        return;
    }
    report(true,"Imported "+candidate.filename().string()+
          " ("+std::to_string(size)+" bytes)");
}

} // namespace

bool install() {
    JNIEnv* e=jni::env();
    if(!e) return false;
    if(e->PushLocalFrame(8)!=0)return false;
    jclass cls=jni::appClass(e,"org.levimc.launcher.preloader.PreloaderInput");
    if(!cls){jni::takeException(e);e->PopLocalFrame(nullptr);return false;}
    JNINativeMethod method{
        const_cast<char*>("nativeOnDocumentResult"),
        const_cast<char*>("(ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
        reinterpret_cast<void*>(received)
    };
    bool ok=e->RegisterNatives(cls,&method,1)==JNI_OK;
    if(auto err=jni::takeException(e);!err.empty()) {
        modmenu::log::warn("OptLevi Android picker bridge: {}",err);
        ok=false;
    }
    enabled.store(ok);
    registered.store(ok);
    e->PopLocalFrame(nullptr);
    modmenu::log::info("OptLevi Android ACTION_OPEN_DOCUMENT import bridge {}",ok?"ready":"unavailable");
    return ok;
}

void uninstall() {
    // The launcher holds a callback into this module after RegisterNatives.
    // Do not unregister the whole PreloaderInput class: that would break
    // the launcher touch/keyboard JNI functions.
    enabled.store(false);
    pending.store(false);
}

bool available(){return enabled.load()&&registered.load();}

bool openPicker() {
    if(!available()||pending.exchange(true))return false;
    JNIEnv* e=jni::env();
    if(!e||e->PushLocalFrame(8)!=0){pending=false;return false;}
    jobject activity=jni::currentActivity(e);
    jclass cls=activity?e->GetObjectClass(activity):nullptr;
    jmethodID open=cls?e->GetMethodID(cls,"openPreloaderDocumentPicker",
                                    "(Ljava/lang/String;)V"):nullptr;
    bool ok=activity&&open;
    if(ok){
        jstring mime=e->NewStringUTF("*/*");
        e->CallVoidMethod(activity,open,mime);
        if(!jni::takeException(e).empty())ok=false;
    }else jni::takeException(e);
    e->PopLocalFrame(nullptr);
    if(!ok){pending=false;report(false,"LeviLauncher document picker not available");}
    return ok;
}

bool consumeResult(Result& result) {
    std::lock_guard lock(resultMutex);
    if(!hasResult)return false;
    result=std::move(lastResult);
    hasResult=false;
    return true;
}
} // namespace modmenu::android::shaderimport
