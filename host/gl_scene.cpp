#include "gl_scene.hpp"
#include "spatial_draw.hpp"
#include "spatial_cockpit.hpp"
#include "spatial_units.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <cstring>
#include <map>
#include <set>
#include <bitset>
#include <tuple>
#include <unordered_map>

namespace comanchecd::host {
namespace {
#define GL_PROCEDURES(X)                                                                                     \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                                                             \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                                                             \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)                                                       \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                                                                       \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                                                                       \
    X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                              \
        X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) X(                     \
            PFNGLCOMPILESHADERPROC, CompileShader) X(PFNGLGETSHADERIVPROC, GetShaderiv)                      \
            X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) X(         \
                PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader)                \
                X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation) X(PFNGLLINKPROGRAMPROC, LinkProgram) X(   \
                    PFNGLGETPROGRAMIVPROC, GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)    \
                    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) X(PFNGLUSEPROGRAMPROC, UseProgram)              \
                        X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(PFNGLUNIFORM1IPROC, Uniform1i)  \
                            X(PFNGLUNIFORM1FPROC, Uniform1f) X(PFNGLUNIFORM2FPROC, Uniform2f)                \
                                X(PFNGLUNIFORM2FVPROC, Uniform2fv) X(PFNGLDRAWELEMENTSINSTANCEDPROC,         \
                                                                     DrawElementsInstanced)                  \
                                    X(PFNGLUNIFORM3FPROC, Uniform3f) X(PFNGLUNIFORM4FPROC, Uniform4f)        \
                                        X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) X(      \
                                            PFNGLVERTEXATTRIBPOINTERPROC,                                    \
                                            VertexAttribPointer) X(PFNGLACTIVETEXTUREPROC, ActiveTexture)    \
                                            X(PFNGLGENFRAMEBUFFERSPROC,                                      \
                                              GenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)  \
                                                X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) X(    \
                                                    PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
                                                    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)       \
                                                        X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)       \
                                                            X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)   \
                                                                X(PFNGLRENDERBUFFERSTORAGEPROC,              \
                                                                  RenderbufferStorage)                       \
                                                                    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,      \
                                                                      FramebufferRenderbuffer)               \
                                                                        X(PFNGLDELETERENDERBUFFERSPROC,      \
                                                                          DeleteRenderbuffers)

struct Gl {
#define DECLARE(type, name) type name = nullptr;
    GL_PROCEDURES(DECLARE)
#undef DECLARE
    Gl() {
#define LOAD(type, name)                                                                                     \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));                                        \
    if (!name) {                                                                                             \
        throw std::runtime_error("Missing OpenGL procedure gl" #name);                                       \
    }
        GL_PROCEDURES(LOAD)
#undef LOAD
    }
};
#undef GL_PROCEDURES

const char* vertex_shader = R"GLSL(#version 150
in vec3 position;
in vec2 uv;
in vec4 color;
uniform vec4 frustum;
uniform vec2 clips;
uniform vec3 rightAxis, upAxis, backAxis, eyeOrigin;
uniform usampler2D heights;
uniform int terrainGrid;
uniform vec2 tileOffsets[128];
out vec2 vUv;
out vec3 vPoint, vRay;
out vec4 vColor;
void main() {
    vec3 p=position;
    vUv=uv;
    if(terrainGrid!=0) {
        vUv=position.xz+tileOffsets[gl_InstanceID];
        float height=float(texelFetch(heights,ivec2(vUv)&ivec2(1023),0).r)*0.375;
        vec3 delta=vec3(vUv.x,height,vUv.y)-eyeOrigin;
        p=vec3(dot(delta,rightAxis),dot(delta,upAxis),dot(delta,backAxis));
    }
    gl_Position = vec4((2.0*p.x+(frustum.y+frustum.x)*p.z)/(frustum.y-frustum.x),
        (2.0*p.y+(frustum.z+frustum.w)*p.z)/(frustum.z-frustum.w),
        -(clips.y+clips.x)/(clips.y-clips.x)*p.z-2.0*clips.y*clips.x/(clips.y-clips.x), -p.z);
    vPoint=p; vColor=color;
    vRay=rightAxis*p.x+upAxis*p.y+backAxis*p.z;
}
)GLSL";

const char* fragment_shader = R"GLSL(#version 150
uniform usampler2D indexed, heights, waterMap, weatherMap;
uniform sampler2D palette, reflected, backgroundImage, panel;
uniform int kind, mirrored, hasSky, haze, reflectionPass, waterHeight, weatherSolid, weatherType, solidIndex, linearOutput, heightClip;
uniform vec2 dimensions, spriteSize, fogDistance;
uniform float minimumHeight, spriteBottom, spriteScale;
uniform int reflectionPhase;
in vec2 vUv;
in vec3 vPoint, vRay;
in vec4 vColor;
out vec4 outputColor;
uint terrainHeight(ivec2 p) { return texelFetch(heights,p & ivec2(1023),0).r; }
float surfaceHeight(vec2 p) {
    ivec2 cell=ivec2(floor(p)); vec2 f=fract(p);
    return mix(mix(float(terrainHeight(cell)),float(terrainHeight(cell+ivec2(1,0))),f.x),
        mix(float(terrainHeight(cell+ivec2(0,1))),float(terrainHeight(cell+ivec2(1,1))),f.x),f.y)*0.375;
}
vec4 mapped(uint index, bool fog) {
    int bank=0;
    if (fog && haze!=0) { bank=-vPoint.z<fogDistance.x?0:-vPoint.z<fogDistance.y?1:2; }
    return texelFetch(palette,ivec2(int(index),bank),0);
}
uint nearestIndex(vec3 color) {
    float best=1e30; uint result=0u;
    for(int i=0;i<256;++i) {
        vec3 d=color-texelFetch(palette,ivec2(i,0),0).rgb; float distance=dot(d,d);
        if(distance<best) { best=distance; result=uint(i); }
    }
    return result;
}
vec3 decodeSrgb(vec3 c) { return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045))); }
void main() {
    vec4 c;
    if(kind==0) {
        ivec2 cell=ivec2(floor(vUv)) & ivec2(1023);
        if(heightClip!=0 && surfaceHeight(vUv)<=minimumHeight) { discard; }
        uint index=texelFetch(indexed,cell,0).r;
        if(waterHeight>=0) {
            if(index<251u || index>254u || int(terrainHeight(cell))!=waterHeight) { discard; }
            ivec2 ripple=ivec2(floor(vUv*32.0));
            uint phase=uint(reflectionPhase) ^ (uint(ripple.x)*1664525u+uint(ripple.y)*1013904223u);
            vec2 at=gl_FragCoord.xy/dimensions;
            at.y=1.0-at.y+(float(int(phase&3u)-1))/float(textureSize(reflected,0).y);
            uint sampleIndex=uint(round(texture(reflected,at).a*255.0));
            c=mapped(texelFetch(waterMap,ivec2(int(sampleIndex),int(index-251u)),0).r,false);
        } else { c=mapped(index,true); }
    } else if(kind==1) {
        if(heightClip!=0 && spriteBottom+vUv.y*spriteScale<=minimumHeight) { discard; }
        ivec2 p=clamp(ivec2(vUv),ivec2(0),ivec2(spriteSize)-1);
        if(mirrored!=0) { p.x=int(spriteSize.x)-1-p.x; }
        uint index=texelFetch(indexed,p,0).r; if(index==0u) { discard; }
        c=mapped(index,false);
    } else if(kind==2) { c=vColor; }
    else if(kind==3 || kind==4) {
        c=texelFetch(panel,clamp(ivec2(vUv),ivec2(0),ivec2(319,239)),0);
        if(c.a<0.5) { discard; }
    } else if(kind==5) {
        uint index=uint(solidIndex);
        if(weatherSolid==0) {
            uint behind=nearestIndex(texture(backgroundImage,gl_FragCoord.xy/dimensions).rgb);
            index=texelFetch(weatherMap,ivec2(int(behind),weatherType),0).r;
        }
        c=mapped(index,true);
    } else if(kind==6) {
        uint index=255u;
        if(hasSky!=0) {
            float azimuth=atan(vRay.x,-vRay.z);
            float elevation=atan(vRay.y,length(vRay.xz));
            int column=int(floor(504.0-azimuth*512.0/6.283185307179586)) & 511;
            int row=int(clamp(118.0-elevation*200.0,0.0,149.0));
            index=texelFetch(indexed,ivec2(column,row),0).r;
        }
        c=mapped(index,false);
    } else {
        c=texture(panel,gl_FragCoord.xy/dimensions);
        if(linearOutput!=0) { c.rgb=decodeSrgb(c.rgb); }
    }
    outputColor=vec4(c.rgb,reflectionPass!=0?c.a:1.0);
}
)GLSL";

struct Vertex {
    float point[3], uv[2], color[4];
};
enum class PixelFormat { indices, rgba };
enum class ScenePass { world, reflection };
enum class ClearTarget { clear, retain };
enum class ScreenPass { sky = 6, output = 7 };
struct Surface {
    GLuint framebuffer = 0, color = 0, depth = 0;
    unsigned width = 0, height = 0;
};
} // namespace

class GlScene::Impl final : public SpatialGeometry {
    static constexpr unsigned tile_cells = 32, tile_batch = 128, tiles_side = terrain_side / tile_cells;
    static constexpr int no_water_height = -1;
    struct Tile {
        float x, z;
        unsigned map_index;
    };

  public:
    Impl() {
        try {
            program_ = gl_.CreateProgram();
            for (const auto& item : {std::pair<GLenum, const char*>{GL_VERTEX_SHADER, vertex_shader},
                                     {GL_FRAGMENT_SHADER, fragment_shader}}) {
                const auto shader = compile(item.first, item.second);
                gl_.AttachShader(program_, shader);
                gl_.DeleteShader(shader);
            }
            gl_.BindAttribLocation(program_, 0, "position");
            gl_.BindAttribLocation(program_, 1, "uv");
            gl_.BindAttribLocation(program_, 2, "color");
            gl_.LinkProgram(program_);
            GLint linked = 0;
            gl_.GetProgramiv(program_, GL_LINK_STATUS, &linked);
            if (!linked) {
                char log[4096]{};
                gl_.GetProgramInfoLog(program_, sizeof(log), nullptr, log);
                throw std::runtime_error(log);
            }
            gl_.GenVertexArrays(1, &vao_);
            gl_.GenBuffers(1, &buffer_);
            gl_.BindVertexArray(vao_);
            gl_.BindBuffer(GL_ARRAY_BUFFER, buffer_);
            gl_.EnableVertexAttribArray(0);
            gl_.EnableVertexAttribArray(1);
            gl_.EnableVertexAttribArray(2);
            gl_.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
            gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, uv)));
            gl_.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                    reinterpret_cast<void*>(offsetof(Vertex, color)));
            create_terrain_tile();
            palette_ = texture();
            terrain_ = texture();
            heights_ = texture();
            sky_ = texture();
            water_map_ = texture();
            weather_map_ = texture();
            panel_ = texture();
            hud_ = texture();
            background_ = texture();
            gl_.GenFramebuffers(1, &target_framebuffer_);
            gl_.UseProgram(program_);
            for (const auto& binding : {std::pair<const char*, int>{"indexed", 0},
                                        {"palette", 1},
                                        {"heights", 2},
                                        {"reflected", 3},
                                        {"waterMap", 4},
                                        {"backgroundImage", 5},
                                        {"weatherMap", 6},
                                        {"panel", 7}}) {
                integer(binding.first, binding.second);
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Impl() override {
        cleanup();
    }
    void draw(const OpenXrTarget& target, const SpatialFrame& frame, const CameraView& supplied,
              const EntityPoses& poses, const CockpitImage& source, const std::optional<CameraView>& body) {
        if (!target.texture || !target.width || !target.height || !frame.resources ||
            frame.scene_mask.size() != source.pixels.size()) {
            throw std::invalid_argument("GPU scene requires complete eye and scene observations");
        }
        // An earlier failed frame may have left geometry that was never submitted.
        vertices_.clear();
        override_ = -1;
        prepare(frame, source);
        resize(scene_, target.width, target.height);
        resize(reflected_, std::max(1u, target.width / 2), std::max(1u, target.height / 2));
        auto view = supplied;
        view.pose.position.x = std::remainder(view.pose.position.x, double(terrain_side));
        view.pose.position.z = std::remainder(view.pose.position.z, double(terrain_side));
        const auto& tables = frame.resources->tables;
        if (tables.levels > 0 && tables.levels <= terrain_distance_capacity) {
            view.frustum.far_clip =
                std::min(view.frustum.far_clip, std::clamp(tables.distance[4][tables.levels - 1] /
                                                               spatial_units::distance_table_units_per_cell,
                                                           16.0, 512.0));
        }
        gl_.UseProgram(program_);
        gl_.BindVertexArray(vao_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, buffer_);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        bind(1, palette_);
        bind(2, heights_);
        bind(4, water_map_);
        bind(6, weather_map_);
        gl_.Uniform2f(location("fogDistance"), float(colors_.quarter_distance), float(colors_.half_distance));
        integer("haze", colors_.haze);
        integer("hasSky", colors_.panorama != nullptr);
        integer("linearOutput", 0);
        phase_ = frame.effects.reflection_phase;
        bind_surface(scene_, view, ScenePass::world);
        SpatialImage geometry{target.width, target.height, {}, {}, this};
        const auto world_tiles = visible_tiles(view);
        if (view.pose.position.y < spatial_draw::surface_height(frame.resources->assets.terrain,
                                                                view.pose.position.x, view.pose.position.z)) {
            const auto cell = std::size_t(int(std::floor(view.pose.position.z)) & 1023) * terrain_side +
                              (int(std::floor(view.pose.position.x)) & 1023);
            const auto color = colors_.base[frame.resources->assets.terrain.color[cell]];
            glClearColor(float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255,
                         float(color & 255) / 255, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            sky(view);
            draw_tiles(world_tiles, view);
            if (frame.effects.reflections) {
                std::bitset<256> water_heights;
                for (const auto& tile : world_tiles) {
                    water_heights |= water_tiles_[tile.map_index];
                }
                for (unsigned height = 0; height < water_heights.size(); ++height) {
                    if (!water_heights[height]) {
                        continue;
                    }
                    const double plane = height * spatial_units::terrain_height;
                    auto mirror = view;
                    mirror.pose.position.y = 2 * plane - view.pose.position.y;
                    mirror.pose.orientation.x = -mirror.pose.orientation.x;
                    mirror.pose.orientation.z = -mirror.pose.orientation.z;
                    mirror.frustum.up = -view.frustum.down;
                    mirror.frustum.down = -view.frustum.up;
                    bind_surface(reflected_, mirror, ScenePass::reflection);
                    sky(mirror);
                    SpatialImage reflected_geometry{reflected_.width, reflected_.height, {}, {}, this};
                    const CameraProjection mirrored(mirror, reflected_.width, reflected_.height);
                    draw_tiles(visible_tiles(mirror), mirror, plane);
                    spatial_draw::models(reflected_geometry, frame, mirror, mirrored, poses, colors_, plane);
                    flush();
                    bind_surface(scene_, view, ScenePass::world, ClearTarget::retain);
                    bind(3, reflected_.color);
                    draw_tiles(world_tiles, view, -std::numeric_limits<double>::infinity(), int(height));
                }
            }
            const CameraProjection projection(view, target.width, target.height);
            spatial_draw::weather(geometry, frame, view, projection, colors_,
                                  spatial_draw::WeatherLayer::background);
            spatial_draw::models(geometry, frame, view, projection, poses, colors_);
            spatial_draw::weather(geometry, frame, view, projection, colors_,
                                  spatial_draw::WeatherLayer::foreground);
            flush();
        }
        if (body) {
            glClear(GL_DEPTH_BUFFER_BIT);
            set_view(*body);
            cockpit_.draw(geometry, *body, source, frame);
            flush();
        }
        gl_.BindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
        gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        if (gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("XR eye framebuffer incomplete");
        }
        glViewport(0, 0, target.width, target.height);
        glDisable(GL_DEPTH_TEST);
        if (target.color_space == OpenXrColorSpace::srgb) {
            glEnable(GL_FRAMEBUFFER_SRGB);
        }
        integer("reflectionPass", 0);
        integer("linearOutput", 1);
        bind(7, scene_.color);
        screen(ScreenPass::output, CameraView{{}, {-1, 1, 1, -1, 0.1, 10}});
        glDisable(GL_FRAMEBUFFER_SRGB);
        gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl_.BindVertexArray(0);
        gl_.UseProgram(0);
        glFlush();
        if (glGetError() != GL_NO_ERROR) {
            throw std::runtime_error("OpenGL scene rendering failed");
        }
    }

  private:
    GLuint compile(GLenum type, const char* source) {
        const auto shader = gl_.CreateShader(type);
        gl_.ShaderSource(shader, 1, &source, nullptr);
        gl_.CompileShader(shader);
        GLint compiled = 0;
        gl_.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[4096]{};
            gl_.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
            gl_.DeleteShader(shader);
            throw std::runtime_error(log);
        }
        return shader;
    }
    void cleanup() {
        if (scene_.framebuffer) {
            gl_.DeleteFramebuffers(1, &scene_.framebuffer);
            gl_.DeleteRenderbuffers(1, &scene_.depth);
        }
        if (reflected_.framebuffer) {
            gl_.DeleteFramebuffers(1, &reflected_.framebuffer);
            gl_.DeleteRenderbuffers(1, &reflected_.depth);
        }
        if (target_framebuffer_) {
            gl_.DeleteFramebuffers(1, &target_framebuffer_);
        }
        if (!textures_.empty()) {
            glDeleteTextures(GLsizei(textures_.size()), textures_.data());
        }
        if (grid_indices_) {
            gl_.DeleteBuffers(1, &grid_indices_);
        }
        if (grid_buffer_) {
            gl_.DeleteBuffers(1, &grid_buffer_);
        }
        if (grid_vao_) {
            gl_.DeleteVertexArrays(1, &grid_vao_);
        }
        if (buffer_) {
            gl_.DeleteBuffers(1, &buffer_);
        }
        if (vao_) {
            gl_.DeleteVertexArrays(1, &vao_);
        }
        if (program_) {
            gl_.DeleteProgram(program_);
        }
    }
    GLint location(const char* name) {
        const auto found = uniforms_.find(name);
        if (found != uniforms_.end()) {
            return found->second;
        }
        return uniforms_.emplace(name, gl_.GetUniformLocation(program_, name)).first->second;
    }
    void integer(const char* name, int value) {
        gl_.Uniform1i(location(name), value);
    }
    void number(const char* name, double value) {
        gl_.Uniform1f(location(name), float(value));
    }
    void bind(unsigned unit, GLuint name) {
        gl_.ActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, name);
    }
    GLuint texture() {
        GLuint name = 0;
        glGenTextures(1, &name);
        textures_.push_back(name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return name;
    }
    void upload(GLuint name, unsigned width, unsigned height, const void* pixels, PixelFormat format) {
        gl_.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const auto size = std::make_pair(width, height);
        const auto found = texture_sizes_.find(name);
        const auto channels = format == PixelFormat::indices ? GL_RED_INTEGER : GL_RGBA;
        if (found == texture_sizes_.end() || found->second != size) {
            glTexImage2D(GL_TEXTURE_2D, 0, format == PixelFormat::indices ? GL_R8UI : GL_RGBA8, width, height,
                         0, channels, GL_UNSIGNED_BYTE, pixels);
            texture_sizes_[name] = size;
        } else if (pixels) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, channels, GL_UNSIGNED_BYTE, pixels);
        }
    }
    void resize(Surface& surface, unsigned width, unsigned height) {
        if (surface.width == width && surface.height == height) {
            return;
        }
        if (!surface.framebuffer) {
            gl_.GenFramebuffers(1, &surface.framebuffer);
            gl_.GenRenderbuffers(1, &surface.depth);
            surface.color = texture();
        }
        surface.width = width;
        surface.height = height;
        upload(surface.color, width, height, nullptr, PixelFormat::rgba);
        gl_.BindRenderbuffer(GL_RENDERBUFFER, surface.depth);
        gl_.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        gl_.BindFramebuffer(GL_FRAMEBUFFER, surface.framebuffer);
        gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface.color, 0);
        gl_.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, surface.depth);
        if (gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("GPU scene framebuffer incomplete");
        }
    }
    void set_view(const CameraView& view) {
        const CameraProjection transform(view, 1, 1);
        const auto axis = [&](const char* name, CameraVector p) {
            const auto v = transform.to_world(p) - view.pose.position;
            gl_.Uniform3f(location(name), float(v.x), float(v.y), float(v.z));
        };
        axis("rightAxis", {1, 0, 0});
        axis("upAxis", {0, 1, 0});
        axis("backAxis", {0, 0, 1});
        gl_.Uniform3f(location("eyeOrigin"), float(view.pose.position.x), float(view.pose.position.y),
                      float(view.pose.position.z));
    }
    void bind_surface(const Surface& surface, const CameraView& view, ScenePass pass,
                      ClearTarget clear = ClearTarget::clear) {
        flush();
        gl_.BindFramebuffer(GL_FRAMEBUFFER, surface.framebuffer);
        glViewport(0, 0, surface.width, surface.height);
        dimensions_ = {surface.width, surface.height};
        gl_.Uniform2f(location("dimensions"), float(surface.width), float(surface.height));
        integer("reflectionPass", pass == ScenePass::reflection);
        set_view(view);
        if (clear == ClearTarget::clear) {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    }
    void prepare(const SpatialFrame& frame, const CockpitImage& source) {
        colors_ = spatial_draw::colors(frame, source.palette);
        if (resources_ != frame.resources) {
            std::set<GLuint> retired;
            for (const auto& sprite : sprites_) {
                retired.insert(sprite.second);
            }
            for (const auto name : retired) {
                glDeleteTextures(1, &name);
                texture_sizes_.erase(name);
            }
            textures_.erase(std::remove_if(textures_.begin(), textures_.end(),
                                           [&](GLuint name) { return retired.count(name) != 0; }),
                            textures_.end());
            sprites_.clear();
            resources_ = frame.resources;
            for (auto& tile : water_tiles_) {
                tile.reset();
            }
            palette_valid_ = false;
            last_pixels_.clear();
            last_mask_.clear();
            last_water_.clear();
            last_weather_.clear();
            const auto& terrain = resources_->assets.terrain;
            if (terrain.height.size() != terrain_side * terrain_side ||
                terrain.color.size() != terrain.height.size()) {
                throw std::invalid_argument("GPU terrain maps incomplete");
            }
            upload(terrain_, terrain_side, terrain_side, terrain.color.data(), PixelFormat::indices);
            upload(heights_, terrain_side, terrain_side, terrain.height.data(), PixelFormat::indices);
            for (unsigned tile_z = 0; tile_z < terrain_side / tile_cells; ++tile_z) {
                for (unsigned tile_x = 0; tile_x < terrain_side / tile_cells; ++tile_x) {
                    const auto tile = tile_z * (terrain_side / tile_cells) + tile_x;
                    unsigned minimum = 255, maximum = 0;
                    for (unsigned z = 0; z <= tile_cells; ++z) {
                        for (unsigned x = 0; x <= tile_cells; ++x) {
                            const auto cell = std::size_t((tile_z * tile_cells + z) & 1023) * terrain_side +
                                              ((tile_x * tile_cells + x) & 1023);
                            const auto height = terrain.height[cell];
                            minimum = std::min(minimum, unsigned(height));
                            maximum = std::max(maximum, unsigned(height));
                            if (x < tile_cells && z < tile_cells && terrain.color[cell] >= 251 &&
                                terrain.color[cell] <= 254) {
                                water_tiles_[tile].set(height);
                            }
                        }
                    }
                    tile_heights_[tile] = {minimum * spatial_units::terrain_height,
                                           maximum * spatial_units::terrain_height};
                }
            }
            if (colors_.panorama) {
                std::vector<std::uint8_t> pixels(512 * 150);
                for (unsigned y = 0; y < 150; ++y) {
                    for (unsigned x = 0; x < 512; ++x) {
                        pixels[y * 512 + x] = (*colors_.panorama)[colors_.panorama_offset + x * 150 + y];
                    }
                }
                upload(sky_, 512, 150, pixels.data(), PixelFormat::indices);
            }
        }
        const bool palette_changed = !palette_valid_ || last_palette_ != source.palette;
        if (palette_changed) {
            std::array<std::uint8_t, 256 * 3 * 4> palette{};
            const std::array<const std::array<std::uint32_t, 256>*, 3> banks{&colors_.base, &colors_.quarter,
                                                                             &colors_.half};
            for (unsigned bank = 0; bank < banks.size(); ++bank) {
                for (unsigned index = 0; index < 256; ++index) {
                    const auto color = (*banks[bank])[index];
                    const auto at = (bank * 256 + index) * 4;
                    palette[at] = color >> 16;
                    palette[at + 1] = color >> 8;
                    palette[at + 2] = color;
                    unsigned closest = index;
                    if (bank) {
                        unsigned best = ~0u;
                        for (unsigned candidate = 0; candidate < 256; ++candidate) {
                            unsigned distance = 0;
                            for (unsigned shift : {0u, 8u, 16u}) {
                                const auto d = int((color >> shift) & 255) -
                                               int((colors_.base[candidate] >> shift) & 255);
                                distance += unsigned(d * d);
                            }
                            if (distance < best) {
                                best = distance;
                                closest = candidate;
                            }
                        }
                    }
                    palette[at + 3] = closest;
                }
            }
            upload(palette_, 256, 3, palette.data(), PixelFormat::rgba);
            last_palette_ = source.palette;
            palette_valid_ = true;
        }
        if (frame.effects.reflections) {
            if (frame.effects.reflection_colors.size() != 4) {
                throw std::invalid_argument("GPU water tables incomplete");
            }
            if (last_water_ != frame.effects.reflection_colors) {
                upload(water_map_, 256, 4, frame.effects.reflection_colors.data(), PixelFormat::indices);
                last_water_ = frame.effects.reflection_colors;
            }
        }
        if (!frame.effects.particles.empty()) {
            if (frame.effects.tint.size() != 2) {
                throw std::invalid_argument("GPU weather tables incomplete");
            }
            if (last_weather_ != frame.effects.tint) {
                upload(weather_map_, 256, 2, frame.effects.tint.data(), PixelFormat::indices);
                last_weather_ = frame.effects.tint;
            }
        }
        if (palette_changed || last_mask_ != frame.scene_mask ||
            last_pixels_.size() != source.pixels.size() ||
            !std::equal(last_pixels_.begin(), last_pixels_.end(), source.pixels.begin())) {
            std::vector<std::uint8_t> panel(source.pixels.size() * 4), hud(panel.size());
            for (std::size_t i = 0; i < source.pixels.size(); ++i) {
                const auto color = colors_.base[source.pixels[i]];
                const auto at = i * 4;
                panel[at] = color >> 16;
                panel[at + 1] = color >> 8;
                panel[at + 2] = color;
                panel[at + 3] = 255;
                if (!frame.scene_mask[i] && resources_->hud_colors[source.pixels[i]]) {
                    std::copy_n(panel.begin() + at, 4, hud.begin() + at);
                }
            }
            upload(panel_, cockpit_width, cockpit_height, panel.data(), PixelFormat::rgba);
            upload(hud_, cockpit_width, cockpit_height, hud.data(), PixelFormat::rgba);
            last_pixels_.assign(source.pixels.begin(), source.pixels.end());
            last_mask_ = frame.scene_mask;
        }
    }
    void create_terrain_tile() {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        for (unsigned z = 0; z <= tile_cells; ++z) {
            for (unsigned x = 0; x <= tile_cells; ++x) {
                vertices.push_back({{float(x), 0, float(z)}, {float(x), float(z)}, {1, 1, 1, 1}});
            }
        }
        for (unsigned z = 0; z < tile_cells; ++z) {
            for (unsigned x = 0; x < tile_cells; ++x) {
                const auto a = z * (tile_cells + 1) + x, b = a + 1, c = a + tile_cells + 2,
                           d = a + tile_cells + 1;
                indices.insert(indices.end(), {a, b, c, a, c, d});
            }
        }
        gl_.GenVertexArrays(1, &grid_vao_);
        gl_.GenBuffers(1, &grid_buffer_);
        gl_.GenBuffers(1, &grid_indices_);
        gl_.BindVertexArray(grid_vao_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, grid_buffer_);
        gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(),
                       GL_STATIC_DRAW);
        gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, grid_indices_);
        gl_.BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(std::uint32_t)),
                       indices.data(), GL_STATIC_DRAW);
        gl_.EnableVertexAttribArray(0);
        gl_.EnableVertexAttribArray(1);
        gl_.EnableVertexAttribArray(2);
        gl_.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                reinterpret_cast<void*>(offsetof(Vertex, uv)));
        gl_.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                reinterpret_cast<void*>(offsetof(Vertex, color)));
        gl_.BindVertexArray(vao_);
    }
    std::vector<Tile> visible_tiles(const CameraView& view) const {
        const CameraProjection transform(view, 1, 1);
        const auto right = transform.to_world({1, 0, 0}) - view.pose.position;
        const auto up = transform.to_world({0, 1, 0}) - view.pose.position;
        const auto back = transform.to_world({0, 0, 1}) - view.pose.position;
        const auto& f = view.frustum;
        const auto dot = [](CameraVector a, CameraVector b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        struct Plane {
            CameraVector normal;
            double bias;
        };
        const auto plane = [&](CameraVector normal, double distance) {
            return Plane{normal, distance - dot(normal, view.pose.position)};
        };
        const std::array<Plane, 6> planes{{plane(back * -1, -f.near_clip), plane(back, f.far_clip),
                                           plane(right + back * f.left, 0),
                                           plane(right * -1 - back * f.right, 0),
                                           plane(up * -1 - back * f.up, 0), plane(up + back * f.down, 0)}};
        const auto horizontal = std::max(std::abs(f.left), std::abs(f.right));
        const auto vertical = std::max(std::abs(f.up), std::abs(f.down));
        const auto radius = f.far_clip * std::sqrt(1 + horizontal * horizontal + vertical * vertical);
        if (!std::isfinite(radius) || radius > std::numeric_limits<int>::max() / 4.0) {
            throw std::invalid_argument("Unrepresentable terrain frustum");
        }
        const int x0 = int(std::floor((view.pose.position.x - radius) / tile_cells));
        const int x1 = int(std::ceil((view.pose.position.x + radius) / tile_cells));
        const int z0 = int(std::floor((view.pose.position.z - radius) / tile_cells));
        const int z1 = int(std::ceil((view.pose.position.z + radius) / tile_cells));
        std::vector<Tile> tiles;
        for (int z = z0; z < z1; ++z) {
            for (int x = x0; x < x1; ++x) {
                const auto index =
                    unsigned(z & (tiles_side - 1)) * tiles_side + unsigned(x & (tiles_side - 1));
                const double left = x * int(tile_cells), bottom = z * int(tile_cells);
                const auto& bounds = tile_heights_[index];
                bool visible = true;
                for (const auto& clip : planes) {
                    const CameraVector corner{clip.normal.x >= 0 ? left + tile_cells : left,
                                              clip.normal.y >= 0 ? bounds.second : bounds.first,
                                              clip.normal.z >= 0 ? bottom + tile_cells : bottom};
                    if (dot(clip.normal, corner) + clip.bias < -0.001) {
                        visible = false;
                        break;
                    }
                }
                if (visible) {
                    tiles.push_back({float(left), float(bottom), index});
                }
            }
        }
        return tiles;
    }
    void draw_tiles(const std::vector<Tile>& tiles, const CameraView& view,
                    double minimum_height = -std::numeric_limits<double>::infinity(), int water_height = -1) {
        flush();
        gl_.UseProgram(program_);
        gl_.BindVertexArray(grid_vao_);
        integer("kind", int(GeometryKind::terrain));
        integer("terrainGrid", 1);
        integer("waterHeight", water_height);
        integer("heightClip", std::isfinite(minimum_height));
        number("minimumHeight", std::isfinite(minimum_height) ? minimum_height : 0);
        integer("reflectionPhase", int(phase_ & 3));
        bind(0, terrain_);
        const auto& f = view.frustum;
        gl_.Uniform4f(location("frustum"), float(f.left), float(f.right), float(f.up), float(f.down));
        gl_.Uniform2f(location("clips"), float(f.near_clip), float(f.far_clip));
        std::array<float, tile_batch * 2> offsets{};
        unsigned count = 0;
        const auto draw = [&] {
            if (!count) {
                return;
            }
            gl_.Uniform2fv(location("tileOffsets"), count, offsets.data());
            gl_.DrawElementsInstanced(GL_TRIANGLES, tile_cells * tile_cells * 6, GL_UNSIGNED_INT, nullptr,
                                      count);
            count = 0;
        };
        for (const auto& tile : tiles) {
            if (tile_heights_[tile.map_index].second <= minimum_height) {
                continue;
            }
            if (water_height >= 0 && !water_tiles_[tile.map_index][water_height]) {
                continue;
            }
            offsets[count * 2] = tile.x;
            offsets[count * 2 + 1] = tile.z;
            if (++count == tile_batch) {
                draw();
            }
        }
        draw();
        integer("terrainGrid", 0);
        gl_.BindVertexArray(vao_);
    }
    void material(const GeometryMaterial& value) override {
        if (material_.kind != GeometryKind::solid || value.kind != GeometryKind::solid) {
            flush();
        }
        material_ = value;
    }
    void triangle(const CameraFrustum& frustum, const GeometryVertex& a, const GeometryVertex& b,
                  const GeometryVertex& c) override {
        if (std::memcmp(&frustum_, &frustum, sizeof(frustum)) != 0) {
            flush();
            frustum_ = frustum;
        }
        const auto color = material_.color;
        for (const auto* vertex : {&a, &b, &c}) {
            vertices_.push_back({{float(vertex->point.x), float(vertex->point.y), float(vertex->point.z)},
                                 {float(vertex->u), float(vertex->v)},
                                 {float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255,
                                  float(color & 255) / 255, 1}});
        }
    }
    void weather_background() override {
        flush();
        bind(5, background_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dimensions_.first, dimensions_.second, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, dimensions_.first, dimensions_.second);
    }
    void flush() {
        if (vertices_.empty()) {
            return;
        }
        gl_.UseProgram(program_);
        const int kind = override_ >= 0 ? override_ : int(material_.kind);
        integer("terrainGrid", 0);
        integer("kind", kind);
        integer("waterHeight", no_water_height);
        integer("mirrored", material_.mirror);
        integer("heightClip", std::isfinite(material_.minimum_height));
        number("minimumHeight", std::isfinite(material_.minimum_height) ? material_.minimum_height : 0);
        number("spriteBottom", material_.bottom);
        number("spriteScale", material_.vertical_scale);
        integer("weatherSolid", material_.weather_solid);
        integer("weatherType", material_.weather_type);
        integer("solidIndex", material_.color & 255);
        // Reflection noise is deterministic and independent of the canonical random generator.
        integer("reflectionPhase", int(phase_ & 3));
        gl_.Uniform4f(location("frustum"), float(frustum_.left), float(frustum_.right), float(frustum_.up),
                      float(frustum_.down));
        gl_.Uniform2f(location("clips"), float(frustum_.near_clip), float(frustum_.far_clip));
        gl_.Uniform2f(location("spriteSize"), float(material_.width), float(material_.height));
        if (kind == 0) {
            bind(0, terrain_);
        } else if (kind == 1) {
            const auto key =
                std::make_tuple(material_.bytes, material_.source, material_.width, material_.height);
            auto found = sprites_.find(key);
            if (found == sprites_.end()) {
                const auto name = texture();
                std::vector<std::uint8_t> pixels(material_.width * material_.height);
                for (unsigned y = 0; y < material_.height; ++y) {
                    for (unsigned x = 0; x < material_.width; ++x) {
                        pixels[y * material_.width + x] =
                            material_.bytes->at(material_.source + x * material_.height + y);
                    }
                }
                upload(name, material_.width, material_.height, pixels.data(), PixelFormat::indices);
                found = sprites_.emplace(key, name).first;
            }
            bind(0, found->second);
        } else if (kind == 3) {
            bind(7, panel_);
        } else if (kind == 4) {
            bind(7, hud_);
        } else if (kind == 6) {
            bind(0, sky_);
        }
        gl_.BindVertexArray(vao_);
        gl_.BindBuffer(GL_ARRAY_BUFFER, buffer_);
        gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data(),
                       GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
        vertices_.clear();
    }
    void screen(ScreenPass kind, const CameraView& view) {
        flush();
        material_ = GeometryMaterial{};
        material_.color = 0xffffffffu;
        override_ = int(kind);
        const auto& f = view.frustum;
        const GeometryVertex a{{f.left, f.up, -1}}, b{{f.right, f.up, -1}}, c{{f.right, f.down, -1}},
            d{{f.left, f.down, -1}};
        triangle(f, a, b, c);
        triangle(f, a, c, d);
        flush();
        override_ = -1;
    }
    void sky(const CameraView& view) {
        glDepthMask(GL_FALSE);
        screen(ScreenPass::sky, view);
        glDepthMask(GL_TRUE);
    }
    Gl gl_;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0, target_framebuffer_ = 0;
    GLuint palette_ = 0, terrain_ = 0, heights_ = 0, sky_ = 0, water_map_ = 0, weather_map_ = 0, panel_ = 0,
           hud_ = 0, background_ = 0;
    std::vector<GLuint> textures_;
    std::unordered_map<GLuint, std::pair<unsigned, unsigned>> texture_sizes_;
    Surface scene_, reflected_;
    std::shared_ptr<const SpatialResources> resources_;
    spatial_draw::Colors colors_;
    std::map<std::tuple<const std::vector<std::uint8_t>*, std::size_t, unsigned, unsigned>, GLuint> sprites_;
    std::unordered_map<std::string, GLint> uniforms_;
    std::array<std::pair<double, double>, (terrain_side / tile_cells) * (terrain_side / tile_cells)>
        tile_heights_{};
    std::array<std::bitset<256>, (terrain_side / tile_cells) * (terrain_side / tile_cells)> water_tiles_{};
    GLuint grid_vao_ = 0, grid_buffer_ = 0, grid_indices_ = 0;
    std::vector<Vertex> vertices_;
    GeometryMaterial material_;
    CameraFrustum frustum_;
    std::pair<unsigned, unsigned> dimensions_{};
    int override_ = -1;
    std::uint32_t phase_ = 0;
    SpatialCockpit cockpit_;
    Palette last_palette_{};
    bool palette_valid_ = false;
    std::vector<std::uint8_t> last_pixels_, last_mask_;
    std::vector<ColorMap> last_water_, last_weather_;
};
GlScene::GlScene() : impl_(std::make_unique<Impl>()) {}
GlScene::~GlScene() = default;
void GlScene::draw(const OpenXrTarget& target, const SpatialFrame& frame, const CameraView& view,
                   const EntityPoses& poses, const CockpitImage& source,
                   const std::optional<CameraView>& cockpit) {
    impl_->draw(target, frame, view, poses, source, cockpit);
}
} // namespace comanchecd::host
