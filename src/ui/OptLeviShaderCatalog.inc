// OptLevi 0.8 — non-executing shader-pack catalog.
// ZIP central-directory filenames and loose files are inspected only.
// No source code is compiled, extracted, loaded into the GPU, or executed.
enum class OptPackType { Unknown, Java, Bedrock, Mixed };
struct OptPackInfo {
    std::filesystem::path path;
    OptPackType type = OptPackType::Unknown;
    unsigned javaFiles = 0;
    unsigned materialFiles = 0;
    bool folder = false;
};
static std::vector<OptPackInfo> gOptShaderFiles;
static std::string gOptExportStatus;

static std::string optLower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c=char(c-'A'+'a');
    return s;
}
static bool optEnds(std::string const& s, char const* suffix) {
    size_t n=std::strlen(suffix);
    return s.size()>=n && s.compare(s.size()-n,n,suffix)==0;
}
static void optClassify(OptPackInfo& pack,std::string name) {
    name=optLower(std::move(name));
    for (char& c:name) if(c=='\\') c='/';
    if (optEnds(name, ".material.bin")) ++pack.materialFiles;
    if (name.find("shaders/")!=std::string::npos &&
        (optEnds(name,".fsh") || optEnds(name,".vsh") ||
         optEnds(name,".glsl") || optEnds(name,"shaders.properties")))
        ++pack.javaFiles;
    if (optEnds(name,"shaders.properties")) ++pack.javaFiles;
}
static OptPackType optPackType(OptPackInfo const& p) {
    if(p.javaFiles && p.materialFiles) return OptPackType::Mixed;
    if(p.javaFiles) return OptPackType::Java;
    if(p.materialFiles) return OptPackType::Bedrock;
    return OptPackType::Unknown;
}
static char const* optPackLabel(OptPackType t) {
    switch(t) {
    case OptPackType::Java: return "Java (Iris / OptiFine)";
    case OptPackType::Bedrock: return "Bedrock RenderDragon (.material.bin)";
    case OptPackType::Mixed: return "Hybrid / mixed shader resources";
    default: return "Unknown / no recognized shader files";
    }
}
static unsigned optLE16(unsigned char const* p) {
    return unsigned(p[0]) | (unsigned(p[1])<<8);
}
static unsigned optLE32(unsigned char const* p) {
    return optLE16(p) | (optLE16(p+2)<<16);
}
static void optInspectZip(OptPackInfo& pack) {
    std::ifstream in(pack.path,std::ios::binary|std::ios::ate);
    if(!in) return;
    auto end=in.tellg();
    if(end < std::streamoff(22)) return;
    auto size=static_cast<std::uint64_t>(end);
    // EOCD search is limited by the ZIP specification (64 KiB comments).
    size_t tailSize=static_cast<size_t>(std::min<std::uint64_t>(size,65557));
    std::vector<unsigned char> tail(tailSize);
    in.seekg(std::streamoff(size-tailSize),std::ios::beg);
    if(!in.read(reinterpret_cast<char*>(tail.data()),tailSize))return;
    size_t found=tailSize;
    for(size_t pos=tailSize-22;;--pos) {
        if(optLE32(tail.data()+pos)==0x06054b50u) {found=pos;break;}
        if(pos==0)break;
    }
    if(found==tailSize)return;
    auto const* footer=tail.data()+found;
    std::uint64_t count=optLE16(footer+10);
    std::uint64_t bytes=optLE32(footer+12);
    std::uint64_t off=optLE32(footer+16);
    // ZIP64 archives are not parsed in this lightweight catalog.
    if(count==65535 || bytes==0xffffffffu || off==0xffffffffu ||
       count>10000 || bytes>(8u<<20) || off+bytes>size)return;
    std::vector<unsigned char> directory(static_cast<size_t>(bytes));
    in.clear();
    in.seekg(std::streamoff(off),std::ios::beg);
    if(!in.read(reinterpret_cast<char*>(directory.data()),directory.size()))return;
    size_t pos=0;
    for(std::uint64_t i=0;i<count && pos+46<=directory.size();++i) {
        auto const* header=directory.data()+pos;
        if(optLE32(header)!=0x02014b50u)break;
        size_t name=optLE16(header+28),extra=optLE16(header+30),comment=optLE16(header+32);
        if(name>4096 || pos+46+name+extra+comment>directory.size())break;
        optClassify(pack,std::string(reinterpret_cast<char const*>(header+46),name));
        pos+=46+name+extra+comment;
    }
}
static OptPackInfo optReadPack(std::filesystem::path p) {
    OptPackInfo out;
    out.path=std::move(p);
    std::error_code ec;
    out.folder=std::filesystem::is_directory(out.path,ec);
    if(out.folder) {
        std::filesystem::recursive_directory_iterator it(
            out.path,std::filesystem::directory_options::skip_permission_denied,ec),end;
        size_t seen=0;
        for(;!ec && it!=end && seen<6000;it.increment(ec),++seen) {
            if(it.depth()>7){it.disable_recursion_pending();continue;}
            std::error_code typeError;
            if(it->is_symlink(typeError)) {it.disable_recursion_pending();continue;}
            if(it->is_regular_file(typeError))
                optClassify(out,it->path().lexically_relative(out.path).generic_string());
        }
    } else {
        auto filename=optLower(out.path.filename().string());
        if(optEnds(filename,".material.bin")) ++out.materialFiles;
        if(optEnds(filename,".zip")||optEnds(filename,".mcpack"))optInspectZip(out);
    }
    out.type=optPackType(out);
    return out;
}
static bool optCandidate(std::filesystem::directory_entry const& f) {
    std::error_code ec;
    if(f.is_symlink(ec))return false;
    if(f.is_directory(ec))return true;
    if(!f.is_regular_file(ec))return false;
    auto name=optLower(f.path().filename().string());
    return optEnds(name,".zip") || optEnds(name,".mcpack") ||
           optEnds(name,".material.bin");
}
static std::filesystem::path optExportPath(std::filesystem::path const& source) {
    auto dir=paths::selfDir()/"exports";
    std::error_code ec;
    std::filesystem::create_directories(dir,ec);
    if(ec) {gOptExportStatus="Export failed: "+ec.message();return {};}
    auto name=source.filename().string();
    if(name.empty()||name=="."||name=="..") {
        gOptExportStatus="Export failed: invalid file name";return {};
    }
    auto dest=dir/name;
    for(int n=2;n<1000 && std::filesystem::exists(dest,ec);++n) {
        dest=dir/(source.stem().string()+"-"+std::to_string(n)+source.extension().string());
    }
    if(ec || std::filesystem::exists(dest,ec)) {
        gOptExportStatus="Export failed: destination unavailable";return {};
    }
    return dest;
}
static void optExportPack(OptPackInfo const& pack) {
    auto dest=optExportPath(pack.path);
    if(dest.empty())return;
    std::error_code ec;
    if(pack.folder) {
        // Do not follow symbolic links from user-created folders.
        std::filesystem::copy(pack.path,dest,
            std::filesystem::copy_options::recursive|
            std::filesystem::copy_options::skip_symlinks,ec);
    } else std::filesystem::copy_file(pack.path,dest,
                   std::filesystem::copy_options::none,ec);
    if(ec)gOptExportStatus="Export failed: "+ec.message();
    else gOptExportStatus="Exported to OptLevi/exports: "+dest.filename().string();
}
