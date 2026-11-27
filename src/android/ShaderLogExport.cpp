#include "ShaderLogExport.h"
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
