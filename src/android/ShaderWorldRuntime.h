#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>
#include <array>
#include "ShaderMeshMapping.h"
#include "ShaderPackUniforms.h"
#include "ShaderPackProfiles.h"

namespace modmenu::android::shaderworld {
// Ordered fullscreen pass graph. Only buffers actually produced by a pass
// can be read; the initial real frame provides colortex0, not a fake G-buffer.
struct PackTexture {
    std::string sampler;
    int width=0,height=0;
    bool nearest=false;
    std::vector<std::uint8_t> rgba;
};
struct GeometryProgram {
    std::string name,vertex,fragment;
    bool shadow=false;
    std::string enabledProbe;
};
struct Pass {
    std::string name;
    std::string fragment;
    std::vector<unsigned> targets;
    bool finalPass=false;
    // Translated original fullscreen vertex stage; empty for diagnostic passes.
    std::string vertex;
    // Resolve the active conditional DRAWBUFFERS mapping from linked outputs.
    bool routedOutputs=false;
    // Shared pack-level composite textures are attached to the first pass only.
    std::vector<PackTexture> packTextures;
    // Optional GLSL preprocessor-only program.enabled probe. Disabled passes
    // must not compile, allocate targets, or satisfy later dependencies.
    std::string enabledProbe;
    // Pack-wide attachment formats, present only on the first graph pass.
    std::vector<std::pair<unsigned,std::string>> colorFormats;
    // Deferred programs run at the opaque/translucent boundary, not at swap.
    bool deferred=false;
    // Selected pack geometry/shadow source pairs, shared on first pass.
    std::vector<GeometryProgram> geometryPrograms;
    std::vector<mesh::BlockRule> blockRules;
    std::vector<custom::Rule> customUniforms;
    // Clear=false history is reflected via enabledProbe; no history buffer
    // is usable without an actual writer in the graph.
};
struct Status {
    bool active=false;
    bool failed=false;
    std::string message;
};
void requestPipeline(std::vector<Pass> passes,std::string label);
void requestEnable(std::string translatedFragment,std::string sourceLabel);
void requestDisable();
Status status();
// Remains true for a selected pack awaiting native resources, even if its
// first compilation was rejected. False when Off or Disable is selected.
bool captureRequested();
// Called on the game render thread by a phase-aware renderer adapter, with
// a completed, single-sample depth attachment. Never infer phases at swap.
enum class DepthPhase : unsigned { Complete=0, BeforeTranslucent=1, BeforeHand=2 };
bool captureSceneDepth(DepthPhase phase,unsigned framebuffer,unsigned internalFormat,
                       int width,int height,std::string& error);
// Capture a native geometry color attachment with explicitly verified Java
// semantics. The adapter must select the correct phase and attachment;
// an arbitrary native color attachment is not automatically a G-buffer.
bool captureSceneColor(unsigned colortex,unsigned framebuffer,unsigned attachment,
                       std::string const& format,int width,int height,std::string& error);
// Independent light-space resources; the adapter must render actual shadows.
bool captureShadowDepth(unsigned index,unsigned framebuffer,unsigned internalFormat,
                        int width,int height,std::string& error);
bool captureShadowColor(unsigned index,unsigned framebuffer,unsigned attachment,
                        std::string const& format,int width,int height,std::string& error);
// Typed data from a verified world-rendering phase. Matrices are column
// major and must already use the Java shader's coordinate convention.
struct WorldUniform {
    std::string name;
    unsigned glType=0;
    std::array<float,16> floats{};
    std::array<int,4> ints{};
};
struct UniformMapping {
    std::string nativeName;
    std::string javaName;
    unsigned glType=0;
};
// Either submit native world-state values, or read a verified linked native
// GLES program's actual uniform values using explicit semantic mappings.
// All values expire at the next swap; missing data is never defaulted.
bool publishWorldUniforms(std::vector<WorldUniform> const& values,std::string& error);
bool captureProgramUniforms(unsigned program,std::vector<UniformMapping> const& mappings,
                            std::string& error);
// Called after all opaque snapshots and uniforms have been captured, before
// translucent draws. Outputs are borrowed until the next swap; the native
// adapter must consume them using the pack's actual G-buffer semantics.
bool renderDeferred(int width,int height,std::string& error);
unsigned deferredTexture(unsigned colortex);
// A geometry program is not a fullscreen pass. Its attribute ABI and
// output locations are reflected for a native adapter with real meshes.
struct GeometryHandle {
    unsigned program=0;
    std::vector<unsigned> targets; // colortex indices, or shadowcolor indices
    std::vector<std::pair<std::string,unsigned>> attributes;
    bool shadow=false;
};
// Pack textures remain available even while the composite graph waits for world data.
bool acquireGeometryProgram(std::string const& name,GeometryHandle& result,std::string& error);
// Explicit, mapped native mesh and dedicated G-buffer/light-space FBO.
// Attachment slots must match the reflected GeometryHandle target list.
struct GeometryDraw {
    std::string program;
    unsigned vao=0,framebuffer=0;
    int width=0,height=0,first=0,count=0,instances=1;
    unsigned indexType=0; // 0 for arrays; otherwise GL_UNSIGNED_SHORT/INT
    std::uintptr_t indexOffset=0;
    std::vector<WorldUniform> uniforms;
    std::vector<std::pair<std::string,unsigned>> textures2D;
};
// Actual world viewport (not shadow-map dimensions). Call before shadow
// draws if their programs need viewport built-ins.
bool beginJavaFrame(int width,int height,std::string& error);
bool drawGeometry(GeometryDraw const& draw,std::string& error);
// Actual block state must already be normalized to Java names/properties.
bool translateTerrainQuad(std::string const& canonicalName,mesh::State const& states,
    std::array<mesh::NativeVertex,4> const& vertices,mesh::V3 center,
    std::array<mesh::JavaVertex,4>& output,std::string& error);
struct TerrainQuad {
    std::string canonicalName;
    mesh::State states;
    std::array<mesh::NativeVertex,4> vertices;
    mesh::V3 center;
};
// The native adapter supplies verified faces and per-draw inputs. Uses
// the selected geometry/shadow program with a generated mapped VAO.
bool drawTerrainQuads(GeometryDraw const& draw,std::vector<TerrainQuad> const& faces,std::string& error);
void beforeSwap(int width,int height);
void stop();
} // namespace modmenu::android::shaderworld
