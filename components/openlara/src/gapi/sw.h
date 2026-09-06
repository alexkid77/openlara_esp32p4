#ifndef H_GAPI_SW
#define H_GAPI_SW

#include "core.h"

#define PROFILE_MARKER(title)
#define PROFILE_LABEL(id, name, label)
#define PROFILE_TIMING(time)

//#define DITHER_FILTER

#if defined(_OS_LINUX) || defined(_OS_TNS) || defined(_OS_SDL12)
    #define COLOR_16
#endif

#ifdef COLOR_16
    #if defined(_OS_LINUX) || defined(_OS_TNS) || defined(_OS_SDL12)
        #define COLOR_FMT_565
        #define CONV_COLOR(r,g,b) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    #else
        #define COLOR_FMT_555
        #define CONV_COLOR(r,g,b) (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
    #endif
#else 
    #define COLOR_FMT_888
    #define CONV_COLOR(r,g,b) ((r << 16) | (g << 8) | b)
#endif

#define SW_MAX_DIST  (20.0f * 1024.0f)
#define SW_FOG_START (12.0f * 1024.0f)
#define SW_NEAR_CLIP 0.1f

namespace GAPI {

    using namespace Core;

    typedef ::Vertex Vertex;

    #ifdef COLOR_16
        typedef uint16 ColorSW;
    #else
        typedef uint32 ColorSW;
    #endif
    typedef uint16 DepthSW;

    uint8   *swLightmap;
    uint8   swLightmapNone[32 * 256];
    uint8   swLightmapShade[32 * 256];
    ColorSW *swPalette;
    ColorSW swPaletteColor[256];
    ColorSW swPaletteWater[256];
    ColorSW swPaletteGray[256];
    uint8   swGradient[256];
    Tile8   *curTile;
    bool    swDepthTest = true;

    uint8 ambient;
    int32 lightsCount;

    struct LightSW {
        uint32 intensity;
        vec3   pos;
        float  radius;
    } lights[MAX_LIGHTS], lightsRel[MAX_LIGHTS];

// Shader
    struct Shader {
        void init(Pass pass, int type, int *def, int defCount) {}
        void deinit() {}
        void bind() {}
        void setParam(UniformType uType, const vec4  &value, int count = 1) {}
        void setParam(UniformType uType, const mat4  &value, int count = 1) {}
    };

// Texture
    struct Texture {
        uint8      *memory;
        int        width, height, origWidth, origHeight;
        TexFormat  fmt;
        uint32     opt;

        Texture(int width, int height, int depth, uint32 opt) : memory(0), width(width), height(height), origWidth(width), origHeight(height), fmt(FMT_RGBA), opt(opt) {}

        void init(void *data) {
            ASSERT((opt & OPT_PROXY) == 0);

            opt &= ~(OPT_CUBEMAP | OPT_MIPMAPS);

            memory = new uint8[width * height * 4];
            if (data) {
                update(data);
            }
        }

        void deinit() {
            if (memory) {
                delete[] memory;
            }
        }

        void generateMipMap() {}

        void update(void *data) {
            memcpy(memory, data, width * height * 4);
        }

        void bind(int sampler) {
            Core::active.textures[sampler] = this;

            if (!this || (opt & OPT_PROXY)) return;
            ASSERT(memory);

            curTile = NULL;
        }

        void bindTileIndices(Tile8 *tile) {
            curTile = (Tile8*)tile;
        }

        void unbind(int sampler) {}

        void setFilterQuality(int value) {
            if (value > Settings::LOW)
                opt &= ~OPT_NEAREST;
            else
                opt |= OPT_NEAREST;
        }
    };

// Mesh
    struct Mesh {
        Index        *iBuffer;
        GAPI::Vertex *vBuffer;

        int          iCount;
        int          vCount;
        bool         dynamic;

        Mesh(bool dynamic) : iBuffer(NULL), vBuffer(NULL), dynamic(dynamic) {}

        void init(Index *indices, int iCount, ::Vertex *vertices, int vCount, int aCount) {
            this->iCount  = iCount;
            this->vCount  = vCount;

            iBuffer = new Index[iCount];
            vBuffer = new Vertex[vCount];

            update(indices, iCount, vertices, vCount);
        }

        void deinit() {
            delete[] iBuffer;
            delete[] vBuffer;
        }

        void update(Index *indices, int iCount, ::Vertex *vertices, int vCount) {
            if (indices) {
                memcpy(iBuffer, indices, iCount * sizeof(indices[0]));
            }

            if (vertices) {
                memcpy(vBuffer, vertices, vCount * sizeof(vertices[0]));
            }
        }

        void bind(const MeshRange &range) const {}

        void initNextRange(MeshRange &range, int &aIndex) const {
            range.aIndex = -1;
        }
    };


    int cullMode, blendMode;

    ColorSW *swColor;
    DepthSW *swDepth;
    short4  swClipRect;

    // Deferred full-RGBA background blit (title/loading screen), honored in
    // endFrame() so it survives the mid-frame deferred target clear.
    struct PendingTitle {
        Texture *tex;
        float   sx, sy;
        uint8   alpha;
    } pendingTitle;

    struct VertexSW {
        int32 x, y, z, w;
        int32 u, v, l;

        inline VertexSW operator + (const VertexSW &p) const {
            VertexSW ret;
            ret.x = x + p.x;
            ret.y = y;
            ret.z = z + p.z;
            ret.w = w + p.w;
            ret.u = u + p.u;
            ret.v = v + p.v;
            ret.l = l + p.l;
            return ret;
        }

        inline VertexSW operator - (const VertexSW &p) const {
            VertexSW ret;
            ret.x = x - p.x;
            ret.y = y;
            ret.z = z - p.z;
            ret.w = w - p.w;
            ret.u = u - p.u;
            ret.v = v - p.v;
            ret.l = l - p.l;
            return ret;
        }

        inline VertexSW operator * (const int32 s) const {
            VertexSW ret;
            ret.x = x * s;
            ret.y = y;
            ret.z = z * s;
            ret.w = w * s;
            ret.u = u * s;
            ret.v = v * s;
            ret.l = l * s;
            return ret;
        }

        inline VertexSW operator / (const int32 s) const {
            VertexSW ret;
            ret.x = x / s;
            ret.y = y;
            ret.z = z / s;
            ret.w = w / s;
            ret.u = u / s;
            ret.v = v / s;
            ret.l = l / s;
            return ret;
        }
    };

    struct FaceSW {
        uint32  z;       // max distance of the primitive in view space depth (0 = near, 65535 = far)
        uint32  index;   // offset into swIndices
        uint16  count;   // 3 or 4 (triangle / quad)
        Tile8   *tile;
        ColorSW *palette;
        uint8   *lightmap;
        short4  clip;

        FaceSW() {}
        FaceSW(uint32 z, uint32 index, uint16 count, Tile8 *tile)
            : z(z), index(index), count(count), tile(tile),
              palette(swPalette), lightmap(swLightmap), clip(swClipRect) {}

        static int cmp(const FaceSW &a, const FaceSW &b) {
            return int32(b.z) - int32(a.z);
        }
    };

    Array<VertexSW> swVertices;
    Array<Index>    swIndices;
    Array<FaceSW>   swFaces;
    Array<FaceSW>   swUIFaces;

    void drawTriangle(Index *indices);
    void drawQuad(Index *indices);

    void init() {
        LOG("Renderer : %s\n", "Software");
        LOG("Version  : %s\n", "0.1");
        swDepth = NULL;
    }

    void deinit() {
        delete[] swDepth;
        swVertices.clear();
        swIndices.clear();
        swFaces.clear();
        swUIFaces.clear();
    }

    void resize() {
        delete[] swDepth;
        //swDepth = new DepthSW[Core::width * Core::height];
    }

    inline mat4::ProjRange getProjRange() {
        return mat4::PROJ_ZERO_POS;
    }

    mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
        mat4 m;
        m.ortho(getProjRange(), l, r, b, t, znear, zfar);
        return m;
    }

    mat4 perspective(float fov, float aspect, float znear, float zfar, float eye) {
        mat4 m;
        m.perspective(getProjRange(), fov, aspect, znear, zfar, eye);
        return m;
    }

    bool beginFrame() {
        swVertices.reset();
        swIndices.reset();
        swFaces.reset();
        swUIFaces.reset();
        return true;
    }

    void flushFaces(Array<FaceSW> &faces) {
        faces.sort();

        for (int i = 0; i < faces.length; i++) {
            FaceSW &f = faces[i];

            swClipRect = f.clip;
            swPalette  = f.palette;
            swLightmap = f.lightmap;
            curTile    = f.tile;

            if (f.count == 3)
                drawTriangle(&swIndices[f.index]);
            else
                drawQuad(&swIndices[f.index]);
        }
    }

    void doPendingTitleBlit();

    void endFrame() {
        // A full-RGBA background texture (title/loading) cannot go through the
        // 8-bit indexed software rasterizer, so blit it into the framebuffer
        // right before the mesh faces are flushed. This avoids being wiped by
        // the deferred target clear that otherwise runs mid-frame.
        doPendingTitleBlit();
        flushFaces(swFaces);
        flushFaces(swUIFaces);

        swVertices.reset();
        swIndices.reset();
        swFaces.reset();
        swUIFaces.reset();
    }

    void resetState() {}

    void bindTarget(Texture *texture, int face) {}

    void discardTarget(bool color, bool depth) {}

    void copyTarget(Texture *dst, int xOffset, int yOffset, int x, int y, int width, int height) {}

    void setVSync(bool enable) {}

    void waitVBlank() {}

    void clear(bool color, bool depth) {
        if (color) {
            memset(swColor, 0x00, Core::width * Core::height * sizeof(ColorSW));
        }

        if (depth) {
            //memset(swDepth, 0xFF, Core::width * Core::height * sizeof(DepthSW));
        }
    }

    void setClearColor(const vec4 &color) {}

    void setViewport(const short4 &v) {}

    void setScissor(const short4 &s) {
        swClipRect.x = s.x;
        swClipRect.y = Core::active.viewport.w - (s.y + s.w);
        swClipRect.z = s.x + s.z;
        swClipRect.w = Core::active.viewport.w - s.y;
    }

    void setDepthTest(bool enable) {
        swDepthTest = enable;
    }

    void setDepthWrite(bool enable) {}

    void setColorWrite(bool r, bool g, bool b, bool a) {}

    void setAlphaTest(bool enable) {}

    void setCullMode(int rsMask) {}

    void setBlendMode(int rsMask) {}

    void setViewProj(const mat4 &mView, const mat4 &mProj) {}

    void updateLights(vec4 *lightPos, vec4 *lightColor, int count) {
        ambient = clamp(int32(active.material.y * 255), 0, 255);

        lightsCount = 0;
        for (int i = 0; i < count; i++) {
            if (lightColor[i].w >= 1.0f) {
                continue;
            }
            LightSW &light = lights[lightsCount++];
            vec4 &c = lightColor[i];
            light.intensity = uint32(((c.x + c.y + c.z) / 3.0f) * 255.0f);
            light.pos    = lightPos[i].xyz();
            light.radius = lightColor[i].w;
        }
    }

    void setFog(const vec4 &params) {}

    bool checkBackface(const VertexSW *a, const VertexSW *b, const VertexSW *c) {
        return ((b->x - a->x) >> 16) * (c->y - a->y) -
               ((c->x - a->x) >> 16) * (b->y - a->y) <= 0;
    }

    inline void sortVertices(VertexSW *&t, VertexSW *&m, VertexSW *&b) {
        if (t->y > m->y) swap(t, m);
        if (t->y > b->y) swap(t, b);
        if (m->y > b->y) swap(m, b);
    }

    inline void sortVertices(VertexSW *&t, VertexSW *&m, VertexSW *&b, VertexSW *&o) {
        if (t->y > m->y) swap(t, m);
        if (o->y > b->y) swap(o, b);
        if (t->y > o->y) swap(t, o);
        if (m->y > b->y) swap(m, b);
        if (m->y > o->y) swap(m, o);
    }

    inline void step(VertexSW &v, const VertexSW &d) {
        //v.w += d.w;
        v.u += d.u;
        v.v += d.v;
        v.l += d.l;
    }

    inline void step(VertexSW &v, const VertexSW &d, int32 count) {
        //v.w += d.w * count;
        v.u += d.u * count;
        v.v += d.v * count;
        v.l += d.l * count;
    }

    // https://www.flipcode.com/archives/Texturing_As_In_Unreal.shtml
    const int uvDither[8] = {
        32768, 16384,           0, 49152,   // (xx yy) for (y & 1 == 0)
        49152,     0,       32768, 16384    // (xx yy) for (y & 1 == 1)
    };

    void drawLine(const VertexSW &L, const VertexSW &R, int32 y) {
        int32 x1 = L.x >> 16;
        int32 x2 = R.x >> 16;

        int32 f = x2 - x1;
        if (f == 0) return;

        VertexSW dS = (R - L) / f;
        VertexSW S  = L;

        if (x1 < swClipRect.x) {
            x1 = swClipRect.x - x1;
            S.z += dS.z * x1;
            step(S, dS, x1);
            x1 = swClipRect.x;
        }
        if (x2 > swClipRect.z) x2 = swClipRect.z;

        int32 i = y * Core::width;

    #ifdef DITHER_FILTER
        const int *dithY = uvDither + ((y & 1) * 4);
    #endif

        for (int x = i + x1; x < i + x2; x++) {
            S.z += dS.z;

            DepthSW z = DepthSW(uint32(S.z) >> 16);

            {//if (swDepth[x] >= z) {
            #ifdef DITHER_FILTER
                const int *dithX = dithY + (x & 1);

                uint32 u = uint32(S.u + dithX[0]) >> 16;
                uint32 v = uint32(S.v + dithX[2]) >> 16;
            #else
                uint32 u = uint32(S.u) >> 16;
                uint32 v = uint32(S.v) >> 16;
            #endif

                uint8 index = curTile->index[(v << 8) + u];

                if (index != 0) {
                    index = swLightmap[((S.l >> (16 + 3)) << 8) + index];

                    swColor[x] = swPalette[index];
                    //swDepth[x] = z;
                }
            }

            step(S, dS);
        }
    }

    void drawPart(const VertexSW &a, const VertexSW &b, const VertexSW &c, const VertexSW &d) {
        VertexSW L, R, dL, dR;
        int32 minY, maxY;

        int32 f = c.y - a.y;
        dL = (c - a) / f;
        dR = (d - b) / f;

        L = a;
        R = b;

        minY = a.y;
        maxY = c.y;

        if (maxY < swClipRect.y || minY >= swClipRect.w) return;

        if (minY < swClipRect.y) {
            minY = swClipRect.y - minY;
            L.x += dL.x * minY;
            L.z += dL.z * minY;
            R.x += dR.x * minY;
            R.z += dR.z * minY;
            step(L, dL, minY);
            step(R, dR, minY);
            minY = swClipRect.y;
        }

        if (maxY > swClipRect.w) maxY = swClipRect.w;

        for (int y = minY; y < maxY; y++) {
            drawLine(L, R, y);
            L.x += dL.x;
            L.z += dL.z;
            R.x += dR.x;
            R.z += dR.z;
            step(L, dL);
            step(R, dR);
        }
    }

    void drawTriangle(Index *indices) {
    /*
             t
            /\ <----- top triangle
         m /__\/ n
           \  /\
             \  \ <-- bottom triangle
               \ \
                 \\
                   \
                    b
    */
        VertexSW _n;
        VertexSW *t = swVertices.items + indices[0];
        VertexSW *m = swVertices.items + indices[1];
        VertexSW *b = swVertices.items + indices[2];
        VertexSW *n = &_n;

        if (checkBackface(t, m, b))
            return;

        int32 cx1 = swClipRect.x << 16;
        int32 cx2 = swClipRect.z << 16;

        if (t->x < cx1 && m->x < cx1 && b->x < cx1)
            return;

        if (t->x > cx2 && m->x > cx2 && b->x > cx2)
            return;

        sortVertices(t, m, b);

        if (b->y < swClipRect.y || t->y > swClipRect.w)
            return;

        *n = ((*b - *t) / (b->y - t->y) * (m->y - t->y)) + *t;
        n->y = m->y;

        if (m->x > n->x) {
            swap(m, n);
        }

        if (m->y != t->y) drawPart(*t, *t, *m, *n);
        if (m->y != b->y) drawPart(*m, *n, *b, *b);
    }

    void drawQuad(Index *indices) {
    /*
             t
            /\ <----- top triangle
         m /__\/ n
           \  /\
            \   \ <-- quad
           p \/__\ o
             /\  / 
               \/ <-- bottom triangle
               b
    */
        VertexSW _n;
        VertexSW _p;
        VertexSW *t = swVertices.items + indices[0];
        VertexSW *m = swVertices.items + indices[1];
        VertexSW *b = swVertices.items + indices[2];
        VertexSW *o = swVertices.items + indices[3];
        VertexSW *n = &_n;
        VertexSW *p = &_p;

        if (checkBackface(t, m, b))
            return;

        int32 cx1 = swClipRect.x << 16;
        int32 cx2 = swClipRect.z << 16;

        if (t->x < cx1 && m->x < cx1 && o->x < cx1 && b->x < cx1)
            return;

        if (t->x > cx2 && m->x > cx2 && o->x > cx2 && b->x > cx2)
            return;

        sortVertices(t, m, b, o);

        if (b->y < swClipRect.y || t->y > swClipRect.w)
            return;

        if (checkBackface(t, b, m) == checkBackface(t, b, o)) {

            VertexSW d = (*b - *t) / (b->y - t->y);

            *n = *t + d * (m->y - t->y);
            *p = *t + d * (o->y - t->y);

            n->y = m->y;
            p->y = o->y;

        } else {

            if (o->y != t->y) {
                *n = *t + ((*o - *t) / (o->y - t->y) * (m->y - t->y));
                n->y = m->y;
            }

            if (m->y != b->y) {
                *p = *b + ((*m - *b) / (m->y - b->y) * (o->y - b->y));
                p->y = o->y;
            }

        }

        if (o->y != t->y && m->x > n->x) swap(m, n);
        if (m->y != b->y && p->x > o->x) swap(p, o);

        if (t->y != m->y) drawPart(*t, *t, *m, *n);
        if (m->y != o->y) drawPart(*m, *n, *p, *o);
        if (o->y != b->y) drawPart(*p, *o, *b, *b);
    }

    void applyLighting(VertexSW &result, const Vertex &vertex, float depth) {
        float lighting = 0.0f;

        if (lightsCount == 0) {
            // No dynamic lights: skip building coord/normal and the (dead)
            // light loop. Only the fog falloff affects the ambient term.
            float d = depth - SW_FOG_START;
            if (d > 0.0f)
                lighting = result.l * clamp(1.0f - d / (SW_MAX_DIST - SW_FOG_START), 0.0f, 1.0f);
            else
                lighting = result.l;
        } else {
            vec3 coord  = vec3(float(vertex.coord.x), float(vertex.coord.y), float(vertex.coord.z));
            vec3 normal = vec3(float(vertex.normal.x), float(vertex.normal.y), float(vertex.normal.z)).normal();

            for (int i = 0; i < lightsCount; i++) {
                LightSW &light = lightsRel[i];
                vec3 dir = (light.pos - coord) * light.radius;
                float att = dir.length2();
                float len = sqrtf(att);
                float lum = (normal.x * dir.x + normal.y * dir.y + normal.z * dir.z) / len;
                lighting += (max(0.0f, lum) * max(0.0f, 1.0f - att)) * light.intensity;
            }

            lighting += result.l;

            depth -= SW_FOG_START;
            if (depth > 0.0f) {
                lighting *= clamp(1.0f - depth / (SW_MAX_DIST - SW_FOG_START), 0.0f, 1.0f);
            }
        }

        result.l = (255 - min(255, int32(lighting))) << 16;
    }

    struct ClipVertSW {
        vec4   c;   // clip space position (w = view depth)
        Vertex src; // source attributes (original or interpolated)
    };

    Vertex lerpVertexSW(const Vertex &a, const Vertex &b, float t) {
        Vertex v;
        v.coord = short4(int16(a.coord.x + (b.coord.x - a.coord.x) * t),
                         int16(a.coord.y + (b.coord.y - a.coord.y) * t),
                         int16(a.coord.z + (b.coord.z - a.coord.z) * t),
                         b.coord.w);
        v.normal = short4(int16(a.normal.x + (b.normal.x - a.normal.x) * t),
                          int16(a.normal.y + (b.normal.y - a.normal.y) * t),
                          int16(a.normal.z + (b.normal.z - a.normal.z) * t),
                          b.normal.w);
        v.texCoord = short4(int16(a.texCoord.x + (b.texCoord.x - a.texCoord.x) * t),
                            int16(a.texCoord.y + (b.texCoord.y - a.texCoord.y) * t),
                            b.texCoord.z, b.texCoord.w);
        v.color = ubyte4(uint8(a.color.x + (b.color.x - a.color.x) * t),
                         a.color.y, a.color.z, b.color.w);
        v.light = ubyte4(uint8(a.light.x + (b.light.x - a.light.x) * t),
                         a.light.y, a.light.z, b.light.w);
        return v;
    }

    int clipNearSW(ClipVertSW *out, const ClipVertSW *in, int count) {
        int n = 0;
        for (int i = 0; i < count; i++) {
            const ClipVertSW &a = in[i];
            const ClipVertSW &b = in[(i + 1) % count];
            bool aIn = a.c.w >= SW_NEAR_CLIP;
            bool bIn = b.c.w >= SW_NEAR_CLIP;
            if (aIn) out[n++] = a;
            if (aIn != bIn) {
                float t = (SW_NEAR_CLIP - a.c.w) / (b.c.w - a.c.w);
                out[n].c   = a.c.lerp(b.c, t);
                out[n].src = lerpVertexSW(a.src, b.src, t);
                n++;
            }
        }
        return n;
    }

    Index projectVertexSW(const ClipVertSW &v, bool colored, uint32 &far) {
        vec4 c = v.c;
        c.x /= c.w;
        c.y /= c.w;
        c.z /= c.w;
        c.x = clamp(c.x, -16384.0f, 16384.0f);
        c.y = clamp(c.y, -16384.0f, 16384.0f);

        VertexSW result;
        result.x = int32(c.x) << 16;
        result.y = int32(c.y);
        result.z = uint32(clamp(c.z, 0.0f, 1.0f) * 65535.0f) << 16;
        result.w = int32(c.w);

        if (colored) {
            result.u = v.src.color.x << 16;
            result.v = 0;
        } else {
            result.u = (v.src.texCoord.x << 16);// / result.w;
            result.v = (v.src.texCoord.y << 16);// / result.w;
        }
        result.w = result.w << 16;
        result.l = ((v.src.light.x * ambient) >> 8);

        applyLighting(result, v.src, c.w);

        far = uint32(result.z) >> 16;

        return Index(swVertices.push(result));
    }

    void emitPrimitiveSW(const Index *prim, int pCount, const Vertex *vertices, int vStart, const mat4 &swMatrix, bool colored, Array<FaceSW> &faces) {
        ClipVertSW cv[4];
        for (int i = 0; i < pCount; i++) {
            const Vertex &vertex = vertices[vStart + prim[i]];
            cv[i].src = vertex;
            cv[i].c   = swMatrix * vec4(vertex.coord.x, vertex.coord.y, vertex.coord.z, 1.0f);
        }

        for (int i = 0; i < pCount; i++) { // far plane: discard whole primitive
            if (cv[i].c.w > SW_MAX_DIST) return;
        }

        bool nearClip = false;
        for (int i = 0; i < pCount; i++) {
            if (cv[i].c.w < SW_NEAR_CLIP) {
                nearClip = true;
                break;
            }
        }

        ClipVertSW tmp[8];
        ClipVertSW *poly = cv;
        int count = pCount;

        if (nearClip) {
            count = clipNearSW(tmp, cv, pCount);
            poly  = tmp;
            if (count < 3) return; // completely behind the camera
        }

        Index  idx[8];
        uint32 vfar[8];
        for (int i = 0; i < count; i++) {
            idx[i] = projectVertexSW(poly[i], colored, vfar[i]);
        }

        Tile8 *tile = colored ? (Tile8*)swGradient : curTile;

        if (count <= 4) {
            uint32 maxZ = 0;
            for (int i = 0; i < count; i++) {
                swIndices.push(idx[i]);
                if (vfar[i] > maxZ) maxZ = vfar[i];
            }
            faces.push(FaceSW(maxZ, swIndices.length - count, count, tile));
        } else { // triangle fan
            for (int i = 1; i + 1 < count; i++) {
                swIndices.push(idx[0]);
                swIndices.push(idx[i]);
                swIndices.push(idx[i + 1]);
                uint32 maxZ = vfar[0];
                if (vfar[i]     > maxZ) maxZ = vfar[i];
                if (vfar[i + 1] > maxZ) maxZ = vfar[i + 1];
                faces.push(FaceSW(maxZ, swIndices.length - 3, 3, tile));
            }
        }
    }

    bool transform(const Index *indices, const Vertex *vertices, int iStart, int iCount, int vStart) {
        mat4 swMatrix;
        swMatrix.viewport(0.0f, (float)Core::height, (float)Core::width, -(float)Core::height, 0.0f, 1.0f);
        swMatrix = swMatrix * mViewProj * mModel;

        const bool colored = vertices[vStart + indices[iStart]].color.w == 142;
        Array<FaceSW> &faces = swDepthTest ? swFaces : swUIFaces;
        Index prim[4];
        int pCount = 0;
        int vIndex = 0;
        bool isTriangle = false;

        for (int i = 0; i < iCount; i++) {
            const Index  index   = indices[iStart + i];
            const Vertex &vertex = vertices[vStart + index];

            vIndex++;

            if (vIndex == 1) {
                isTriangle = vertex.normal.w == 1;
                pCount = 0;
            } else {
                if (vIndex == 4) { // loader splits quads to two triangles with indices 012[02]3, we ignore [02] to make it quad again!
                    vIndex++;
                    i++;
                    continue;
                }
            }

            prim[pCount++] = index;

            if (isTriangle && vIndex == 3 && pCount == 3) {
                emitPrimitiveSW(prim, pCount, vertices, vStart, swMatrix, colored, faces);
                vIndex = 0;
            } else if (vIndex == 6 && pCount == 4) {
                emitPrimitiveSW(prim, pCount, vertices, vStart, swMatrix, colored, faces);
                vIndex = 0;
            }
        }

        return colored;
    }

    void transformLights() {
        memcpy(lightsRel, lights, sizeof(LightSW) * lightsCount);

        mat4 mModelInv = mModel.inverseOrtho();
        for (int i = 0; i < lightsCount; i++) {
            lightsRel[i].pos = mModelInv * lights[i].pos;
        }
    }

    void DIP(Mesh *mesh, const MeshRange &range) {
        if (curTile == NULL) {
            //uint32 *tex = (uint32*)Core::active.textures[0]->memory; // TODO
            return;
        }

        transformLights();

        transform(mesh->iBuffer, mesh->vBuffer, range.iStart, range.iCount, range.vStart);
    }

    void initPalette(Color24 *palette, uint8 *lightmap) {
        for (uint32 i = 0; i < 256; i++) {
            const Color24 &p = palette[i];
            swPaletteColor[i] = CONV_COLOR(p.r, p.g, p.b);
            swPaletteWater[i] = CONV_COLOR((uint32(p.r) * 150) >> 8, (uint32(p.g) * 230) >> 8, (uint32(p.b) * 230) >> 8);
            swPaletteGray[i]  = CONV_COLOR((i * 57) >> 8, (i * 29) >> 8, (i * 112) >> 8);
            swGradient[i]     = i;
        }

        for (uint32 i = 0; i < 256 * 32; i++) {
            swLightmapNone[i]  = i % 256;
            swLightmapShade[i] = lightmap[i];
        }

        swLightmap = swLightmapShade;
        swPalette  = swPaletteColor;
    }

    void setPalette(ColorSW *palette) {
        swPalette = palette;
    }

    void setShading(bool enabled) {
        swLightmap = enabled ? swLightmapShade : swLightmapNone;
    }

    vec4 copyPixel(int x, int y) {
        return vec4(0.0f); // TODO: read from framebuffer
    }

    // Software renderer only rasterizes 8-bit indexed (Tile8) textures, so a
    // full-RGBA background image (title/loading screen) cannot be drawn through
    // the mesh pipeline. We record it here and blit it into the framebuffer at
    // the end of the frame (see doPendingTitleBlit) so it lands beneath the UI.
    void blitTitleBG(Texture *tex, float sx, float sy, uint8 alpha) {
        pendingTitle.tex    = tex;
        pendingTitle.sx     = sx;
        pendingTitle.sy     = sy;
        pendingTitle.alpha  = alpha;
    }

    void doPendingTitleBlit() {
        Texture *tex = pendingTitle.tex;
        if (!tex || !tex->memory || !swColor)
            return;
        pendingTitle.tex = NULL;

        const uint8 *mem = tex->memory;
        const int texW = tex->width;
        const int texH = tex->height;
        const int origW = tex->origWidth;
        const int origH = tex->origHeight;
        const uint8 alpha = pendingTitle.alpha;
        const float sx = pendingTitle.sx;
        const float sy = pendingTitle.sy;

        if (origW <= 0 || origH <= 0)
            return;

        float aspectSrc = (sx * origW) / (sy * origH);
        float aspectDst = float(Core::width) / float(Core::height);
        float aspectImg = aspectSrc / aspectDst;

        int drawW, drawH;
        if (aspectImg >= 1.0f) {
            drawW = Core::width;
            drawH = int(Core::width / aspectSrc);
        } else {
            drawH = Core::height;
            drawW = int(Core::height * aspectSrc);
        }

        int x0 = (Core::width - drawW) / 2;
        int y0 = (Core::height - drawH) / 2;

        if (drawW <= 0 || drawH <= 0) return;

        if (x0 < swClipRect.x) x0 = swClipRect.x;
        if (y0 < swClipRect.y) y0 = swClipRect.y;
        int x1 = x0 + drawW; if (x1 > swClipRect.z) x1 = swClipRect.z;
        int y1 = y0 + drawH; if (y1 > swClipRect.w) y1 = swClipRect.w;

        for (int y = y0; y < y1; y++) {
            int syi = int(((y - (Core::height - drawH) / 2) * origH) / drawH);
            if (syi < 0) syi = 0;
            if (syi >= origH) syi = origH - 1;

            const uint8 *row = mem + syi * texW * 4;
            ColorSW *dst = swColor + y * Core::width;

            for (int x = x0; x < x1; x++) {
                int sxi = int(((x - (Core::width - drawW) / 2) * origW) / drawW);
                if (sxi < 0) sxi = 0;
                if (sxi >= origW) sxi = origW - 1;

                const uint8 *p = row + sxi * 4;

                if (alpha >= 255) {
                    dst[x] = CONV_COLOR(p[0], p[1], p[2]);
                } else {
                    ColorSW bg = dst[x];
                    #ifdef COLOR_FMT_565
                        int br = (bg >> 11) & 0x1F;
                        int bgc = (bg >> 5) & 0x3F;
                        int bb = bg & 0x1F;
                        int fr = (p[0] >> 3);
                        int fg = (p[1] >> 2);
                        int fb = (p[2] >> 3);
                        int a = alpha;
                        int ia = 255 - a;
                        int r = (fr * a + br * ia) / 255;
                        int g = (fg * a + bgc * ia) / 255;
                        int b = (fb * a + bb * ia) / 255;
                        dst[x] = (r << 11) | (g << 5) | b;
                    #else
                        int br = (bg >> 10) & 0x1F;
                        int bgc = (bg >> 5) & 0x1F;
                        int bb = bg & 0x1F;
                        int a = alpha;
                        int ia = 255 - a;
                        dst[x] = CONV_COLOR(
                            (p[0] * a + (br << 3) * ia) / 255,
                            (p[1] * a + (bgc << 3) * ia) / 255,
                            (p[2] * a + (bb << 3) * ia) / 255);
                    #endif
                }
            }
        }
    }
}

#endif
