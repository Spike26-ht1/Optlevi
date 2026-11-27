#include "ShaderLogExport.h"
#include "ShaderDiagnostics.h"
#include <android/log.h>
#include <filesystem>
#include "platform/Jni.h"
#include <jni.h>
#include <chrono>
#include <stdexcept>
#include <string>

namespace modmenu::android::shaderdiagnostics {
namespace {
void require(JNIEnv* env,bool good,char const* message){
    if(env->ExceptionCheck()){
        env->ExceptionClear();
        throw std::runtime_error(message);
    }
    if(!good)throw std::runtime_error(message);
}
}
namespace {
std::mutex mediaMutex;
MediaLogWriter mediaWriter;
std::filesystem::path mediaDirectory;
std::chrono::steady_clock::time_point nextAttempt{};
std::string lastMediaError;
// Query Android's own app-specific media directory rather than hardcoding
// a package name or assuming a storage volume.
std::filesystem::path discoverMediaDirectory(std::string& error){
    JNIEnv* e=jni::env();
    if(!e){error="JNI unavailable";return {};}
    if(e->PushLocalFrame(24)<0){
        if(e->ExceptionCheck())e->ExceptionClear();
        error="JNI local frame unavailable";return {};
    }
    std::filesystem::path path;
    try{
        jobject context=jni::currentActivity(e);
        if(!context){
            // The activity can be absent when the native module is enabled.
            // If the application lookup is unavailable, retry on game swaps.
            auto thread=e->FindClass("android/app/ActivityThread");
            require(e,thread!=nullptr,"Application context unavailable");
            auto app=e->GetStaticMethodID(thread,"currentApplication","()Landroid/app/Application;");
            require(e,app!=nullptr,"Application method unavailable");
            context=e->CallStaticObjectMethod(thread,app);
            require(e,context!=nullptr,"Application context not ready");
        }
        auto contextClass=e->FindClass("android/content/Context");
        require(e,contextClass!=nullptr,"Android Context unavailable");
        auto getDirs=e->GetMethodID(contextClass,"getExternalMediaDirs","()[Ljava/io/File;");
        require(e,getDirs!=nullptr,"External media directories unavailable");
        auto dirs=static_cast<jobjectArray>(e->CallObjectMethod(context,getDirs));
        require(e,dirs!=nullptr,"External media not ready");
        auto fileClass=e->FindClass("java/io/File");
        require(e,fileClass!=nullptr,"File class unavailable");
        auto getPath=e->GetMethodID(fileClass,"getAbsolutePath","()Ljava/lang/String;");
        require(e,getPath!=nullptr,"Media directory path unavailable");
        auto length=e->GetArrayLength(dirs);
        require(e,length>0,"No external media volume");
        for(jsize i=0;i<length;++i){
            auto file=e->GetObjectArrayElement(dirs,i);
            require(e,true,"Media directory lookup failed");
            if(!file)continue;
            auto value=static_cast<jstring>(e->CallObjectMethod(file,getPath));
            require(e,value!=nullptr,"Media path unavailable");
            auto chars=e->GetStringUTFChars(value,nullptr);
            require(e,chars!=nullptr,"Media path conversion failed");
            std::filesystem::path candidate(chars);
            e->ReleaseStringUTFChars(value,chars);
            e->DeleteLocalRef(value);e->DeleteLocalRef(file);
            if(candidate.is_absolute()){
                candidate/="OptLevi/logs";
                std::error_code ec;
                std::filesystem::create_directories(candidate,ec);
                if(!ec){path=std::move(candidate);break;}
            }
        }
        if(path.empty())error="No writable app-specific external media directory";
    }catch(std::exception const& ex){error=ex.what();}
    if(e->ExceptionCheck())e->ExceptionClear();
    e->PopLocalFrame(nullptr);
    return path;
}
}
void persistMediaLog(bool force){
    // No per-draw file I/O. Only lifecycle hooks and the outer EGL swap
    // call this function. Failed paths are retried, including after resume.
    std::lock_guard lock(mediaMutex);
    auto now=std::chrono::steady_clock::now();
    if(!force&&now<nextAttempt)return;
    nextAttempt=now+std::chrono::seconds(3);
    std::string error;
    if(mediaDirectory.empty()){
        mediaDirectory=discoverMediaDirectory(error);
        if(!mediaDirectory.empty())record("Logger","Automatic Android media log initialized");
    }
    if(!mediaDirectory.empty()&&mediaWriter.save(mediaDirectory,journal(),error)){
        if(!lastMediaError.empty())lastMediaError.clear();
        return;
    }
    mediaDirectory.clear();
    if(error!=lastMediaError){
        lastMediaError=error;
        __android_log_print(ANDROID_LOG_WARN,"OptLevi","Automatic renderer log: %s",error.c_str());
        record("Logger error",error);
    }
}
bool exportDownloads(std::string const& contents,std::string& result){
    if(contents.empty()||contents.size()>256*1024){result="Invalid log size";return false;}
    JNIEnv* e=jni::env();
    if(!e){result="Android JNI unavailable; use Copy renderer log";return false;}
    if(e->PushLocalFrame(48)<0){if(e->ExceptionCheck())e->ExceptionClear();result="JNI frame unavailable";return false;}
    jobject resolver=nullptr,uri=nullptr,stream=nullptr;
    jmethodID remove=nullptr,close=nullptr;
    bool closed=false,ok=false;
    try{
        auto cls=[&](char const* name){
            auto c=e->FindClass(name);require(e,c!=nullptr,name);return c;
        };
        auto method=[&](jclass c,char const* n,char const* sig){
            auto m=e->GetMethodID(c,n,sig);require(e,m!=nullptr,n);return m;
        };
        auto str=[&](char const* s){
            auto v=e->NewStringUTF(s);require(e,v!=nullptr,"Cannot allocate JNI string");return v;
        };
        auto version=cls("android/os/Build$VERSION");
        auto sdk=e->GetStaticFieldID(version,"SDK_INT","I");require(e,sdk!=nullptr,"SDK_INT unavailable");
        auto api=e->GetStaticIntField(version,sdk);require(e,api>=29,"Downloads export requires Android 10+; use Copy renderer log");
        auto activity=jni::currentActivity(e);require(e,activity!=nullptr,"No current Android activity");
        auto context=cls("android/content/Context");
        resolver=e->CallObjectMethod(activity,method(context,"getContentResolver","()Landroid/content/ContentResolver;"));
        require(e,resolver!=nullptr,"No content resolver");
        auto rc=cls("android/content/ContentResolver");
        remove=method(rc,"delete","(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I");
        auto downloads=cls("android/provider/MediaStore$Downloads");
        auto field=e->GetStaticFieldID(downloads,"EXTERNAL_CONTENT_URI","Landroid/net/Uri;");
        require(e,field!=nullptr,"Downloads collection unavailable");
        auto collection=e->GetStaticObjectField(downloads,field);require(e,collection!=nullptr,"No Downloads collection");
        auto cv=cls("android/content/ContentValues");
        auto ctor=method(cv,"<init>","()V");
        auto values=e->NewObject(cv,ctor);require(e,values!=nullptr,"ContentValues allocation failed");
        auto put=method(cv,"put","(Ljava/lang/String;Ljava/lang/String;)V");
        auto integer=cls("java/lang/Integer");
        auto valueOf=e->GetStaticMethodID(integer,"valueOf","(I)Ljava/lang/Integer;");
        require(e,valueOf!=nullptr,"Integer.valueOf unavailable");
        auto putInt=method(cv,"put","(Ljava/lang/String;Ljava/lang/Integer;)V");
        auto setInt=[&](jobject target,char const* key,int value){
            auto boxed=e->CallStaticObjectMethod(integer,valueOf,jint(value));require(e,boxed!=nullptr,"Integer allocation failed");
            auto jkey=str(key);e->CallVoidMethod(target,putInt,jkey,boxed);require(e,true,"Cannot set Downloads metadata");
        };
        auto time=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto name="OptLevi-renderer-"+std::to_string(time)+".txt";
        auto setString=[&](char const* key,char const* val){
            auto k=str(key);auto v=str(val);e->CallVoidMethod(values,put,k,v);require(e,true,"Cannot set Downloads metadata");
        };
        setString("_display_name",name.c_str());setString("mime_type","text/plain");
        setString("relative_path","Download/OptLevi");setInt(values,"is_pending",1);
        uri=e->CallObjectMethod(resolver,method(rc,"insert","(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;"),collection,values);
        require(e,uri!=nullptr,"Cannot create log in Downloads");
        auto os=cls("java/io/OutputStream");
        close=method(os,"close","()V");
        auto write=method(os,"write","([B)V");
        stream=e->CallObjectMethod(resolver,method(rc,"openOutputStream","(Landroid/net/Uri;Ljava/lang/String;)Ljava/io/OutputStream;"),uri,str("w"));
        require(e,stream!=nullptr,"Cannot open Downloads log");
        auto bytes=e->NewByteArray(jsize(contents.size()));require(e,bytes!=nullptr,"Cannot allocate log bytes");
        e->SetByteArrayRegion(bytes,0,jsize(contents.size()),reinterpret_cast<jbyte const*>(contents.data()));
        require(e,true,"Cannot copy log bytes");
        e->CallVoidMethod(stream,write,bytes);require(e,true,"Log write failed");
        e->CallVoidMethod(stream,close);require(e,true,"Log close failed");closed=true;
        auto complete=e->NewObject(cv,ctor);require(e,complete!=nullptr,"Cannot publish log");
        setInt(complete,"is_pending",0);
        auto update=method(rc,"update","(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I");
        auto updated=e->CallIntMethod(resolver,update,uri,complete,nullptr,nullptr);
        require(e,updated==1,"Cannot publish Downloads log");
        result="Saved: Downloads/OptLevi/"+name;ok=true;
    }catch(std::exception const& ex){
        if(e->ExceptionCheck())e->ExceptionClear();
        if(stream&&close&&!closed){e->CallVoidMethod(stream,close);if(e->ExceptionCheck())e->ExceptionClear();}
        if(uri&&resolver&&remove){e->CallIntMethod(resolver,remove,uri,nullptr,nullptr);if(e->ExceptionCheck())e->ExceptionClear();}
        result=std::string(ex.what())+"; use Copy renderer log";
    }
    e->PopLocalFrame(nullptr);
    return ok;
}
}
