// Bounded Java shaderpack member reader and GLSL #include expander.
// This is a source preprocessor for *one* selected pass, not an Iris renderer.

static constexpr size_t kOptJavaExpandedLimit = 512 * 1024;

static bool optReadJavaMember(OptPackInfo const& pack,
                              std::string const& member,
                              std::string& result) {
    if (member.rfind("shaders/", 0) != 0 ||
        member.find("..") != std::string::npos ||
        member.find('\\') != std::string::npos) return false;
    if (pack.folder) {
        // Refuse symlink escapes in loose shader folders.
        std::filesystem::path walk=pack.path;
        std::error_code ec;
        for (auto const& component : std::filesystem::path(member)) {
            walk/=component;
            if (std::filesystem::is_symlink(walk,ec) || ec) return false;
        }
        return optReadBounded(walk,result,kMaxShaderBytes);
    }
    auto extension=optLower(pack.path.filename().string());
    if (!optEnds(extension,".zip") && !optEnds(extension,".mcpack")) return false;
    std::string zip;
    if (!optReadBounded(pack.path,zip,kMaxZipBytes) || zip.size()<22) return false;
    auto const* p=reinterpret_cast<unsigned char const*>(zip.data());
    size_t const size=zip.size();
    size_t eocd=size;
    for(size_t i=size-22;;--i) {
        if(optLE32(p+i)==0x06054b50u){eocd=i;break;}
        if(i==0||size-i>65557)break;
    }
    if(eocd==size)return false;
    size_t cd=optLE32(p+eocd+16);
    size_t const count=optLE16(p+eocd+10);
    if (cd>size || count>4096 || count==65535) return false;
    std::string const expected=optLower(member);
    for(size_t i=0;i<count;i++) {
        if(cd>size || size-cd<46 || optLE32(p+cd)!=0x02014b50u) return false;
        size_t const nameSize=optLE16(p+cd+28);
        size_t const extra=optLE16(p+cd+30);
        size_t const comment=optLE16(p+cd+32);
        if(nameSize>1024 || 46+nameSize+extra+comment>size-cd) return false;
        std::string actual=optLower(std::string(zip.data()+cd+46,nameSize));
        for(char& ch:actual)if(ch=='\\')ch='/';
        // Allow an ordinary outer directory such as BSL/shaders/world0/final.fsh.
        bool match=actual==expected;
        if(!match) {
            std::string suffix="/"+expected;
            match=actual.size()>suffix.size() &&
                  actual.compare(actual.size()-suffix.size(),suffix.size(),suffix)==0;
        }
        if(match) {
            size_t const unpacked=optLE32(p+cd+24);
            size_t const packed=optLE32(p+cd+20);
            size_t const local=optLE32(p+cd+42);
            unsigned const method=optLE16(p+cd+10);
            unsigned const flags=optLE16(p+cd+8);
            std::uint32_t const expectedCrc=optLE32(p+cd+16);
            if(unpacked==0 || unpacked>kMaxShaderBytes ||
               packed>kMaxShaderBytes || (flags&1) ||
               (method!=0 && method!=8) ||
               local>size || size-local<30 ||
               optLE32(p+local)!=0x04034b50u)return false;
            size_t const data=local+30+optLE16(p+local+26)+optLE16(p+local+28);
            if(data>size || packed>size-data)return false;
            if(method==0) {
                if(packed!=unpacked)return false;
                result.assign(zip.data()+data,packed);
            }else{
                result.resize(unpacked);
                z_stream zs{};
                if(inflateInit2(&zs,-MAX_WBITS)!=Z_OK)return false;
                zs.next_in=(Bytef*)(p+data);
                zs.avail_in=static_cast<uInt>(packed);
                zs.next_out=(Bytef*)result.data();
                zs.avail_out=static_cast<uInt>(unpacked);
                int const status=inflate(&zs,Z_FINISH);
                bool const ok=status==Z_STREAM_END && zs.total_out==unpacked;
                inflateEnd(&zs);
                if(!ok){result.clear();return false;}
            }
            uLong crc=crc32(0L,Z_NULL,0);
            crc=crc32(crc,reinterpret_cast<Bytef const*>(result.data()),
                      static_cast<uInt>(result.size()));
            return crc==expectedCrc;
        }
        cd+=46+nameSize+extra+comment;
    }
    return false;
}

static std::string gOptJavaSourceError;

static bool optExpandJavaMember(OptPackInfo const& pack,
                                 std::string const& member,
                                 std::string& expanded,
                                 std::set<std::string>& stack,
                                 size_t& total,
                                 unsigned depth) {
    if(depth>12 || !stack.insert(member).second) {
        gOptJavaSourceError="Java shader #include depth/cycle at "+member;
        return false;
    }
    std::string source;
    if(!optReadJavaMember(pack,member,source)) {
        gOptJavaSourceError="Cannot read Java shader source: "+member;
        stack.erase(member);
        return false;
    }
    std::istringstream lines(source);
    std::string line;
    while(std::getline(lines,line)){
        auto first=line.find_first_not_of(" \t\r");
        bool include=first!=std::string::npos &&
                     line.compare(first,8,"#include")==0 &&
                     (line.size()==first+8 ||
                      line[first+8]==' ' || line[first+8]=='\t');
        if(include) {
            size_t begin=line.find_first_of("\"<",first+8);
            size_t end=begin==std::string::npos?std::string::npos:
                line.find(line[begin]=='\"'?'\"':'>',begin+1);
            if(end==std::string::npos || begin+1==end) {
                gOptJavaSourceError="Malformed GLSL #include in "+member;
                stack.erase(member);
                return false;
            }
            std::string path=line.substr(begin+1,end-begin-1);
            std::filesystem::path target;
            if(!path.empty() && path[0]=='/')
                target=std::filesystem::path("shaders")/path.substr(1);
            else target=std::filesystem::path(member).parent_path()/path;
            std::string normalized=target.lexically_normal().generic_string();
            if(normalized.rfind("shaders/",0)!=0 ||
               normalized.find("..")!=std::string::npos ||
               normalized.find('\\')!=std::string::npos) {
                gOptJavaSourceError="Unsafe GLSL #include path in "+member;
                stack.erase(member);
                return false;
            }
            if(!optExpandJavaMember(pack,normalized,expanded,stack,total,depth+1)){
                stack.erase(member);
                return false;
            }
        } else {
            if(total+line.size()+1>kOptJavaExpandedLimit) {
                gOptJavaSourceError="Expanded Java GLSL exceeds 512 KiB";
                stack.erase(member);
                return false;
            }
            expanded+=line;
            expanded+='\n';
            total+=line.size()+1;
        }
    }
    stack.erase(member);
    return true;
}

static bool optReadJavaFinalPass(OptPackInfo const& pack,
                                  std::string& source,
                                  std::string& origin) {
    gOptJavaSourceError.clear();
    // Prefer the Overworld program; don't accidentally compile Nether first
    // just because it occurs earlier in a ZIP central directory.
    for(char const* candidate : {
        "shaders/world0/final.fsh", "shaders/final.fsh",
        "shaders/world0/composite.fsh", "shaders/composite.fsh",
        "shaders/world0/composite1.fsh", "shaders/composite1.fsh",
        "shaders/world-1/final.fsh", "shaders/world1/final.fsh"
    }) {
        std::string entry;
        if(!optReadJavaMember(pack,candidate,entry))continue;
        origin=candidate;
        source.clear();
        size_t total=0;
        std::set<std::string> stack;
        return optExpandJavaMember(pack,candidate,source,stack,total,0);
    }
    gOptJavaSourceError="No supported Java final/composite fragment found in shader ZIP";
    return false;
}
