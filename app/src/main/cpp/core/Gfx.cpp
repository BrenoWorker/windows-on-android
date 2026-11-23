#include "Gfx.h"

#include <cmath>

#include "Utf8.h"

namespace wd {

static const int kFloatsPerVertex = 17;
static const int kMaxQuads = 16000;

static const char* kVertexShader = R"(
attribute vec2 aPos;
attribute vec4 aLocal;
attribute vec4 aShape;
attribute vec4 aColor;
attribute vec3 aUv;
uniform vec2 uScreen;
varying vec4 vLocal;
varying vec4 vShape;
varying vec4 vColor;
varying vec3 vUv;
void main() {
    vLocal = aLocal;
    vShape = aShape;
    vColor = aColor;
    vUv = aUv;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

static const char* kFragmentShader = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec4 vLocal;
varying vec4 vShape;
varying vec4 vColor;
varying vec3 vUv;
uniform sampler2D uTex;
void main() {
    if (vUv.z > 1.5) {
        gl_FragColor = texture2D(uTex, vUv.xy) * vColor;
        return;
    }
    if (vUv.z > 0.5) {
        float t = texture2D(uTex, vUv.xy).a;
        gl_FragColor = vec4(vColor.rgb, vColor.a * t);
        return;
    }
    vec2 q = abs(vLocal.xy) - vLocal.zw + vec2(vShape.x);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vShape.x;
    if (vShape.z > 0.0) d = abs(d + vShape.z * 0.5) - vShape.z * 0.5;
    float b = max(vShape.y, 0.6);
    float a = 1.0 - smoothstep(-b, b, d);
    gl_FragColor = vec4(vColor.rgb, vColor.a * a);
}
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Gfx::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    aPos_ = glGetAttribLocation(prog_, "aPos");
    aLocal_ = glGetAttribLocation(prog_, "aLocal");
    aShape_ = glGetAttribLocation(prog_, "aShape");
    aColor_ = glGetAttribLocation(prog_, "aColor");
    aUv_ = glGetAttribLocation(prog_, "aUv");
    locScreen_ = glGetUniformLocation(prog_, "uScreen");
    locTex_ = glGetUniformLocation(prog_, "uTex");

    std::vector<unsigned short> idx((size_t)kMaxQuads * 6);
    for (int i = 0; i < kMaxQuads; i++) {
        unsigned short b = (unsigned short)(i * 4);
        idx[i * 6 + 0] = b;
        idx[i * 6 + 1] = b + 1;
        idx[i * 6 + 2] = b + 2;
        idx[i * 6 + 3] = b + 2;
        idx[i * 6 + 4] = b + 1;
        idx[i * 6 + 5] = b + 3;
    }
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(unsigned short)), idx.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &vbo_);

    unsigned char white[4] = {255, 255, 255, 255};
    whiteTex_ = createTexture(1, 1, white);
    verts_.reserve((size_t)kMaxQuads * 4 * kFloatsPerVertex);
    layers_.push_back(Layer{});
    return true;
}

void Gfx::resize(int pw, int ph, float scale) {
    pw_ = pw > 0 ? pw : 1;
    ph_ = ph > 0 ? ph : 1;
    scale_ = scale > 0.1f ? scale : 1;
}

void Gfx::begin(Color clear) {
    glViewport(0, 0, pw_, ph_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(prog_);
    glUniform2f(locScreen_, (float)pw_, (float)ph_);
    glUniform1i(locTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    curTex_ = whiteTex_;
    glBindTexture(GL_TEXTURE_2D, curTex_);
    layers_.clear();
    layers_.push_back(Layer{});
    clips_.clear();
    verts_.clear();
    quads_ = 0;
}

void Gfx::end() {
    flush();
    glDisable(GL_SCISSOR_TEST);
}

void Gfx::flush() {
    if (quads_ == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts_.size() * sizeof(float)), verts_.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray((GLuint)aPos_);
    glVertexAttribPointer((GLuint)aPos_, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray((GLuint)aLocal_);
    glVertexAttribPointer((GLuint)aLocal_, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray((GLuint)aShape_);
    glVertexAttribPointer((GLuint)aShape_, 4, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray((GLuint)aColor_);
    glVertexAttribPointer((GLuint)aColor_, 4, GL_FLOAT, GL_FALSE, stride, (void*)(10 * sizeof(float)));
    glEnableVertexAttribArray((GLuint)aUv_);
    glVertexAttribPointer((GLuint)aUv_, 3, GL_FLOAT, GL_FALSE, stride, (void*)(14 * sizeof(float)));
    glDrawElements(GL_TRIANGLES, quads_ * 6, GL_UNSIGNED_SHORT, (void*)0);
    verts_.clear();
    quads_ = 0;
}

void Gfx::bindTexture(GLuint tex) {
    if (tex == curTex_) return;
    flush();
    curTex_ = tex;
    glBindTexture(GL_TEXTURE_2D, tex);
}

void Gfx::emit(float cx, float cy, float ax, float ay, float hw, float hh, float ext, float rad, float soft,
               float strokeW, Color top, Color bottom) {
    if (quads_ >= kMaxQuads) flush();
    const Layer& L = layers_.back();
    float k = L.s * scale_;
    float rmax = std::min(hw, hh);
    if (rad > rmax) rad = rmax;
    if (rad < 0) rad = 0;
    float ex = ext + 1.0f / k;
    float lxs[4] = {-hw - ex, hw + ex, -hw - ex, hw + ex};
    float lys[4] = {-hh - ex, -hh - ex, hh + ex, hh + ex};
    for (int i = 0; i < 4; i++) {
        float lx = lxs[i], ly = lys[i];
        float wx = cx + ax * lx - ay * ly;
        float wy = cy + ay * lx + ax * ly;
        float px = (wx * L.s + L.tx) * scale_;
        float py = (wy * L.s + L.ty) * scale_;
        Color c = i < 2 ? top : bottom;
        float v[kFloatsPerVertex] = {px,     py,     lx * k, ly * k,       hw * k,      hh * k,
                                     rad * k, soft * k, strokeW * k, 0,         c.r,         c.g,
                                     c.b,    c.a * L.alpha, 0, 0, 0};
        verts_.insert(verts_.end(), v, v + kFloatsPerVertex);
    }
    quads_++;
}

void Gfx::emitTextured(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c,
                       float mode) {
    if (quads_ >= kMaxQuads) flush();
    const Layer& L = layers_.back();
    float xs[4] = {x0, x1, x0, x1};
    float ys[4] = {y0, y0, y1, y1};
    float us[4] = {u0, u1, u0, u1};
    float vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        float px = (xs[i] * L.s + L.tx) * scale_;
        float py = (ys[i] * L.s + L.ty) * scale_;
        float v[kFloatsPerVertex] = {px, py, 0, 0, 1, 1, 0, 0, 0, 0, c.r, c.g, c.b, c.a * L.alpha, us[i], vs[i], mode};
        verts_.insert(verts_.end(), v, v + kFloatsPerVertex);
    }
    quads_++;
}

void Gfx::rect(const Rect& r, Color c, float radius) {
    if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
    emit(r.cx(), r.cy(), 1, 0, r.w * 0.5f, r.h * 0.5f, 0, radius, 0, 0, c, c);
}

void Gfx::rectV(const Rect& r, Color top, Color bottom, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    emit(r.cx(), r.cy(), 1, 0, r.w * 0.5f, r.h * 0.5f, 0, radius, 0, 0, top, bottom);
}

void Gfx::stroke(const Rect& r, Color c, float radius, float thickness) {
    if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
    emit(r.cx(), r.cy(), 1, 0, r.w * 0.5f, r.h * 0.5f, 0, radius, 0, thickness, c, c);
}

void Gfx::shadow(const Rect& r, float radius, float blur, Color c) {
    if (c.a <= 0) return;
    emit(r.cx(), r.cy(), 1, 0, r.w * 0.5f, r.h * 0.5f, blur * 1.2f, radius + blur * 0.5f, blur, 0, c, c);
}

void Gfx::circle(float cx, float cy, float rad, Color c) {
    emit(cx, cy, 1, 0, rad, rad, 0, rad, 0, 0, c, c);
}

void Gfx::ring(float cx, float cy, float rad, float thick, Color c) {
    emit(cx, cy, 1, 0, rad, rad, 0, rad, 0, thick, c, c);
}

void Gfx::line(float x0, float y0, float x1, float y1, float thick, Color c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        circle(x0, y0, thick * 0.5f, c);
        return;
    }
    float ax = dx / len, ay = dy / len;
    float hw = len * 0.5f + thick * 0.5f, hh = thick * 0.5f;
    emit((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, ax, ay, hw, hh, 0, hh, 0, 0, c, c);
}

void Gfx::arc(float cx, float cy, float rad, float a0, float a1, float thick, Color c) {
    int seg = std::max(4, (int)(std::fabs(a1 - a0) * rad / 2.5f));
    float px = cx + std::cos(a0) * rad, py = cy + std::sin(a0) * rad;
    for (int i = 1; i <= seg; i++) {
        float a = a0 + (a1 - a0) * i / seg;
        float nx = cx + std::cos(a) * rad, ny = cy + std::sin(a) * rad;
        line(px, py, nx, ny, thick, c);
        px = nx;
        py = ny;
    }
}

void Gfx::rotRect(float cx, float cy, float hw, float hh, float angle, float radius, Color top, Color bottom) {
    emit(cx, cy, std::cos(angle), std::sin(angle), hw, hh, 0, radius, 0, 0, top, bottom);
}

void Gfx::image(GLuint tex, const Rect& r, Color tint) {
    bindTexture(tex);
    emitTextured(r.x, r.y, r.r(), r.b(), 0, 0, 1, 1, tint, 2);
}

int Gfx::addFont(const std::vector<std::string>& paths) {
    auto f = std::make_unique<Font>();
    if (!f->loadFirst(paths)) return -1;
    fonts_.push_back(std::move(f));
    return (int)fonts_.size() - 1;
}

Font* Gfx::pickFont(int font) {
    if (font >= 0 && font < (int)fonts_.size()) return fonts_[(size_t)font].get();
    if (!fonts_.empty()) return fonts_[0].get();
    return nullptr;
}

float Gfx::text(float x, float y, float size, const std::string& s, Color c, int font) {
    Font* f = pickFont(font);
    if (!f || s.empty()) return 0;
    int px = (int)std::lround(size * scale_);
    FontAtlas* a = f->atlas(px);
    if (!a) return 0;
    float inv = 1.0f / scale_;
    float ratio = (float)px / (size * scale_);
    float k = inv / ratio;
    const Layer& L = layers_.back();
    if (L.s == 1.0f) {
        x = std::round((x + L.tx) * scale_) / scale_ - L.tx;
        y = std::round((y + L.ty) * scale_) / scale_ - L.ty;
    }
    bindTexture(a->tex);
    float pen = 0;
    float base = a->ascent;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = utf8Decode(s, i);
        if (cp == '\t') cp = ' ';
        int gi = a->find(cp);
        if (gi < 0) gi = a->find('?');
        if (gi < 0) continue;
        stbtt_aligned_quad q;
        float yy = base;
        stbtt_GetPackedQuad(a->glyphs.data(), a->width, a->height, gi, &pen, &yy, &q, 0);
        if (q.x1 > q.x0) emitTextured(x + q.x0 * k, y + q.y0 * k, x + q.x1 * k, y + q.y1 * k, q.s0, q.t0, q.s1, q.t1, c, 1);
    }
    return pen * k;
}

void Gfx::textBold(float x, float y, float size, const std::string& s, Color c, int font) {
    text(x, y, size, s, c, font);
    text(x + 0.45f / scale_ * 1.6f, y, size, s, c, font);
}

float Gfx::textWidth(float size, const std::string& s, int font) {
    Font* f = pickFont(font);
    if (!f || s.empty()) return 0;
    int px = (int)std::lround(size * scale_);
    FontAtlas* a = f->atlas(px);
    if (!a) return 0;
    float k = (1.0f / scale_) / ((float)px / (size * scale_));
    float pen = 0;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = utf8Decode(s, i);
        if (cp == '\t') cp = ' ';
        int gi = a->find(cp);
        if (gi < 0) gi = a->find('?');
        if (gi < 0) continue;
        pen += a->glyphs[(size_t)gi].xadvance;
    }
    return pen * k;
}

size_t Gfx::hitIndex(float size, const std::string& s, float localX, int font) {
    Font* f = pickFont(font);
    if (!f) return 0;
    int px = (int)std::lround(size * scale_);
    FontAtlas* a = f->atlas(px);
    if (!a) return 0;
    float k = (1.0f / scale_) / ((float)px / (size * scale_));
    float pen = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t start = i;
        uint32_t cp = utf8Decode(s, i);
        if (cp == '\t') cp = ' ';
        int gi = a->find(cp);
        if (gi < 0) gi = a->find('?');
        float adv = gi >= 0 ? a->glyphs[(size_t)gi].xadvance * k : 0;
        if (localX < pen + adv * 0.5f) return start;
        pen += adv;
    }
    return s.size();
}

float Gfx::lineHeight(float size, int font) {
    Font* f = pickFont(font);
    if (!f) return size;
    int px = (int)std::lround(size * scale_);
    FontAtlas* a = f->atlas(px);
    if (!a) return size;
    return (a->ascent - a->descent) / scale_;
}

void Gfx::textIn(const Rect& r, float size, const std::string& s, Color c, int align, int font) {
    float w = textWidth(size, s, font);
    float lh = lineHeight(size, font);
    float x = r.x;
    if (align == AlignCenter) x = r.x + (r.w - w) * 0.5f;
    if (align == AlignRight) x = r.r() - w;
    text(x, r.y + (r.h - lh) * 0.5f, size, s, c, font);
}

std::string Gfx::ellipsize(float size, const std::string& s, float maxW, int font) {
    if (textWidth(size, s, font) <= maxW) return s;
    std::string dots = "\xE2\x80\xA6";
    std::string cur = s;
    while (!cur.empty()) {
        cur.erase(utf8Prev(cur, cur.size()));
        if (textWidth(size, cur + dots, font) <= maxW) return cur + dots;
    }
    return dots;
}

void Gfx::applyScissor() {
    if (clips_.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const Clip& c = clips_.back();
    glEnable(GL_SCISSOR_TEST);
    glScissor(c.x, ph_ - (c.y + c.h), std::max(0, c.w), std::max(0, c.h));
}

void Gfx::pushClip(const Rect& r) {
    flush();
    const Layer& L = layers_.back();
    float x0 = (r.x * L.s + L.tx) * scale_, y0 = (r.y * L.s + L.ty) * scale_;
    float x1 = (r.r() * L.s + L.tx) * scale_, y1 = (r.b() * L.s + L.ty) * scale_;
    Clip c{(int)std::floor(x0), (int)std::floor(y0), (int)std::ceil(x1 - std::floor(x0)), (int)std::ceil(y1 - std::floor(y0))};
    if (!clips_.empty()) {
        const Clip& p = clips_.back();
        int nx = std::max(c.x, p.x), ny = std::max(c.y, p.y);
        int nr = std::min(c.x + c.w, p.x + p.w), nb = std::min(c.y + c.h, p.y + p.h);
        c = {nx, ny, std::max(0, nr - nx), std::max(0, nb - ny)};
    }
    clips_.push_back(c);
    applyScissor();
}

void Gfx::popClip() {
    flush();
    if (!clips_.empty()) clips_.pop_back();
    applyScissor();
}

void Gfx::pushLayer(float ox, float oy, float s, float alpha, float tx, float ty) {
    const Layer& P = layers_.back();
    Layer n;
    float lt_x = ox * (1 - s) + tx, lt_y = oy * (1 - s) + ty;
    n.s = P.s * s;
    n.tx = P.s * lt_x + P.tx;
    n.ty = P.s * lt_y + P.ty;
    n.alpha = P.alpha * alpha;
    layers_.push_back(n);
}

void Gfx::popLayer() {
    if (layers_.size() > 1) layers_.pop_back();
}

GLuint Gfx::createTexture(int w, int h, const void* rgba) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, curTex_);
    return t;
}

void Gfx::updateTexture(GLuint tex, int w, int h, const void* rgba) {
    flush();
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, curTex_);
}

void Gfx::deleteTexture(GLuint tex) {
    flush();
    if (curTex_ == tex) {
        curTex_ = whiteTex_;
        glBindTexture(GL_TEXTURE_2D, curTex_);
    }
    glDeleteTextures(1, &tex);
}

}
