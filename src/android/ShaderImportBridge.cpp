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
#include <chrono>
#include <thread>
#include <exception>

namespace modmenu::android::shaderimport {
namespace {
namespace fs = std::filesystem;
std::atomic<bool> enabled{false};
std::atomic<bool> registered{false};
std::atomic<bool> pending{false};
std::atomic<bool> working{false};
std::thread importWorker;
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

// The system Documents picker delivers a temporary file from
// <launcher cache>/preloader_documents, and deletes that file as soon as
// this JNI callback returns. A large synchronous copy here blocks the
// Android main thread; on lower-end devices it can cause ANR/process death.
//
// Move the temporary file to a uniquely named staging file on the SAME
// filesystem, then return immediately. A native worker copies it into
// OptLevi/shaderpacks off the Android UI and game's GL render threads.
static void importOnWorker(fs::path staging,std::string name) noexcept {
    try {
        std::error_code ec;
        auto size=fs::file_size(staging,ec);
        if(ec||size==0||size>kMaxPackSize) {
            report(false,"Staged shader pack is unreadable or exceeds 64 MB");
        } else {
            auto directory=paths::selfDir()/"shaderpacks";
            fs::create_directories(directory,ec);
            if(ec){
                report(false,"Cannot create shaderpacks: "+ec.message());
            }else{
                std::string stem=name;
                std::string extension;
                if(ends(name,".material.bin")) {
                    extension=name.substr(name.size()-13);
                    stem=name.substr(0,name.size()-13);
                }else {
                    auto dot=name.rfind('.');
                    if(dot!=std::string::npos){
                        stem=name.substr(0,dot);extension=name.substr(dot);
                    }
                }
                fs::path target=directory/name;
                for(unsigned n=1;!ec&&fs::exists(target,ec)&&n<1000;n++)
                    target=directory/(stem+"-"+std::to_string(n)+extension);
                if(ec||fs::exists(target,ec)){
                    report(false,"Cannot choose unique imported filename");
                } else {
                    fs::copy_file(staging,target,fs::copy_options::none,ec);
                    if(ec||fs::file_size(target,ec)!=size){
                        std::error_code ignored;
                        fs::remove(target,ignored);
                        report(false,"Failed to copy shader pack to OptLevi");
                    } else {
                        report(true,"Imported "+target.filename().string()+
                               " ("+std::to_string(size)+" bytes)");
                    }
                }
            }
        }
    } catch(std::exception const& ex) {
        report(false,std::string("Shader import exception: ")+ex.what());
    } catch(...) {
        report(false,"Unexpected shader import error");
    }
    std::error_code ignored;
    fs::remove(staging,ignored);
    working.store(false);
}

void JNICALL received(JNIEnv* env,jclass,jboolean success,jstring path,
                      jstring displayName,jstring error) {
    // JNI entrypoints may not propagate C++ exceptions into the JVM.
    // Consume the pending picker flag before handling duplicate deliveries.
    if(!enabled.load()||!pending.exchange(false))return;
    try {
        if(success!=JNI_TRUE){
            std::string message=str(env,error);
            report(false,message.empty()?"Import cancelled":message);
            return;
        }
        auto name=safeName(str(env,displayName));
        if(name.empty()||!(ends(name,".zip")||ends(name,".mcpack")||
                            ends(name,".material.bin"))){
            report(false,"Choose a .zip, .mcpack or .material.bin pack");
            return;
        }
        fs::path input(str(env,path));
        std::error_code ec;
        // Accept only the private cache file furnished by the launcher,
        // never an arbitrary JNI path or a user-selected symlink.
        if(input.parent_path().filename()!="preloader_documents"||
           fs::is_symlink(input,ec)||ec||!fs::is_regular_file(input,ec)||ec){
            report(false,"Picker did not return a safe temporary file");
            return;
        }
        auto size=fs::file_size(input,ec);
        if(ec||size==0||size>kMaxPackSize){
            report(false,"File empty or larger than 64 MB");
            return;
        }
        if((ends(name,".zip")||ends(name,".mcpack"))&&!isZip(input)){
            report(false,"Chosen file is not a valid ZIP archive");
            return;
        }
        if(working.load()){
            report(false,"Another shader import is still in progress");
            return;
        }
        // A completed worker must be joined before reassigning std::thread.
        if(importWorker.joinable())importWorker.join();
        auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path staging=input.parent_path()/
            ("optlevi_stage_"+std::to_string(nonce)+".tmp");
        fs::rename(input,staging,ec);
        if(ec){
            report(false,"Cannot stage Android picker file: "+ec.message());
            return;
        }
        working.store(true);
        try {
            importWorker=std::thread([staging,name](){
                importOnWorker(staging,name);
            });
        }catch(std::exception const& ex){
            working.store(false);
            std::error_code ignored;
            fs::remove(staging,ignored);
            report(false,std::string("Cannot start import worker: ")+ex.what());
        }
    } catch(std::exception const& ex) {
        report(false,std::string("Android import callback failed: ")+ex.what());
    } catch(...) {
        report(false,"Unexpected error during Android file import");
    }
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
    if(importWorker.joinable())importWorker.join();
}

bool available(){return enabled.load()&&registered.load();}

bool openPicker() {
    if(!available()||working.load()||pending.exchange(true))return false;
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
