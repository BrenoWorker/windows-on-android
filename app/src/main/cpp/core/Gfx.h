#pragma once
#include <GLES2/gl2.h>

#include <memory>
#include <string>
#include <vector>

#include "Font.h"
#include "Geometry.h"

namespace wd {

enum Align { AlignLeft = 0, AlignCenter = 1, AlignRight = 2 };

class Gfx {
public:
    bool init();
    void resize(int pw, int ph, float scale);
    void begin(Color clear);
    void end();

    float scale() const { return scale_; }
    float width() const { return pw_ / scale_; }
    float height() const { return ph_ / scale_; }
    int pixelWidth() const { return pw_; }
    int pixelHeight() const { return ph_; }

    void rect(const Rect& r, Color c, float radius = 0);
    void rectV(const Rect& r, Color top, Color bottom, float radius = 0);
    void stroke(const Rect& r, Color c, float radius, float thickness);
    void shadow(const Rect& r, float radius, float blur, Color c);
    void circle(float cx, float cy, float rad, Color c);
    void ring(float cx, float cy, float rad, float thick, Color c);
    void line(float x0, float y0, float x1, float y1, float thick, Color c);
    void arc(float cx, float cy, float rad, float a0, float a1, float thick, Color c);
    void rotRect(float cx, float cy, float hw, float hh, float angle, float radius, Color top, Color bottom);
    void image(GLuint tex, const Rect& r, Color tint = {1, 1, 1, 1});

    int addFont(const std::vector<std::string>& paths);
    float text(float x, float y, float size, const std::string& s, Color c, int font = 0);
    float textWidth(float size, const std::string& s, int font = 0);
    float lineHeight(float size, int font = 0);
    void textIn(const Rect& r, float size, const std::string& s, Color c, int align = AlignLeft, int font = 0);
    void textBold(float x, float y, float size, const std::string& s, Color c, int font = 0);
    std::string ellipsize(float size, const std::string& s, float maxW, int font = 0);
    size_t hitIndex(float size, const std::string& s, float localX, int font = 0);

    void pushClip(const Rect& r);
    void popClip();
    void pushLayer(float ox, float oy, float s, float alpha, float tx = 0, float ty = 0);
    void popLayer();
    float layerAlpha() const { return layers_.back().alpha; }

    GLuint createTexture(int w, int h, const void* rgba);
    void updateTexture(GLuint tex, int w, int h, const void* rgba);
    void deleteTexture(GLuint tex);

private:
    struct Layer {
        float s = 1, tx = 0, ty = 0, alpha = 1;
    };
    struct Clip {
        int x, y, w, h;
    };
    void flush();
    void bindTexture(GLuint tex);
    void emit(float cx, float cy, float ax, float ay, float hw, float hh, float ext, float rad, float soft,
              float strokeW, Color top, Color bottom);
    void emitTextured(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c,
                      float mode);
    void applyScissor();
    Font* pickFont(int font);

    int pw_ = 1, ph_ = 1;
    float scale_ = 1;
    GLuint prog_ = 0, ibo_ = 0, vbo_ = 0, whiteTex_ = 0;
    GLint locScreen_ = -1, locTex_ = -1;
    GLint aPos_ = -1, aLocal_ = -1, aShape_ = -1, aColor_ = -1, aUv_ = -1;
    GLuint curTex_ = 0;
    std::vector<float> verts_;
    int quads_ = 0;
    std::vector<Layer> layers_;
    std::vector<Clip> clips_;
    std::vector<std::unique_ptr<Font>> fonts_;
};

}
