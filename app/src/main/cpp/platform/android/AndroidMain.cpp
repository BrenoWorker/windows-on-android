#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <time.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../../shell/Shell.h"

using namespace wd;

namespace {

struct Engine {
    android_app* app = nullptr;
    JNIEnv* env = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    int width = 0;
    int height = 0;
    bool focused = true;
    int primaryPointer = -1;
    double insetsTime = 0;
    std::unique_ptr<Shell> shell;
};

std::string prop(const char* name) {
    char buf[PROP_VALUE_MAX] = {0};
    __system_property_get(name, buf);
    return buf;
}

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void clearException(JNIEnv* env) {
    if (env->ExceptionCheck()) env->ExceptionClear();
}

uint32_t unicodeFor(Engine& e, int32_t keyCode, int32_t meta) {
    JNIEnv* env = e.env;
    if (!env) return 0;
    jclass cls = env->FindClass("android/view/KeyEvent");
    if (!cls) {
        clearException(env);
        return 0;
    }
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(II)V");
    jmethodID get = env->GetMethodID(cls, "getUnicodeChar", "(I)I");
    uint32_t result = 0;
    if (ctor && get) {
        jobject ev = env->NewObject(cls, ctor, (jint)AKEY_EVENT_ACTION_DOWN, (jint)keyCode);
        if (ev) {
            jint cp = env->CallIntMethod(ev, get, (jint)meta);
            if (cp > 0) result = (uint32_t)cp;
            env->DeleteLocalRef(ev);
        }
    }
    clearException(env);
    env->DeleteLocalRef(cls);
    return result;
}

float batteryLevel(Engine& e) {
    FILE* f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (f) {
        int v = -1;
        int n = fscanf(f, "%d", &v);
        fclose(f);
        if (n == 1 && v >= 0 && v <= 100) return v / 100.0f;
    }
    JNIEnv* env = e.env;
    if (!env) return 1.0f;
    float level = 1.0f;
    jobject activity = e.app->activity->clazz;
    jclass filterCls = env->FindClass("android/content/IntentFilter");
    jclass ctxCls = env->FindClass("android/content/Context");
    jclass intentCls = env->FindClass("android/content/Intent");
    if (filterCls && ctxCls && intentCls) {
        jmethodID filterCtor = env->GetMethodID(filterCls, "<init>", "(Ljava/lang/String;)V");
        jmethodID reg = env->GetMethodID(ctxCls, "registerReceiver",
                                         "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
        jmethodID getInt = env->GetMethodID(intentCls, "getIntExtra", "(Ljava/lang/String;I)I");
        jstring action = env->NewStringUTF("android.intent.action.BATTERY_CHANGED");
        jobject filter = filterCtor ? env->NewObject(filterCls, filterCtor, action) : nullptr;
        jobject intent = (filter && reg) ? env->CallObjectMethod(activity, reg, (jobject) nullptr, filter) : nullptr;
        clearException(env);
        if (intent && getInt) {
            jstring kLevel = env->NewStringUTF("level");
            jstring kScale = env->NewStringUTF("scale");
            jint lv = env->CallIntMethod(intent, getInt, kLevel, (jint)-1);
            jint sc = env->CallIntMethod(intent, getInt, kScale, (jint)-1);
            if (lv >= 0 && sc > 0) level = (float)lv / (float)sc;
            env->DeleteLocalRef(kLevel);
            env->DeleteLocalRef(kScale);
            env->DeleteLocalRef(intent);
        }
        if (filter) env->DeleteLocalRef(filter);
        env->DeleteLocalRef(action);
    }
    clearException(env);
    if (filterCls) env->DeleteLocalRef(filterCls);
    if (ctxCls) env->DeleteLocalRef(ctxCls);
    if (intentCls) env->DeleteLocalRef(intentCls);
    return level;
}

class JniWebBridge : public WebBridge {
public:
    JniWebBridge(JNIEnv* env, jobject activity) : env_(env), activity_(activity) {
        jclass cls = env->GetObjectClass(activity);
        create_ = env->GetMethodID(cls, "webCreate", "()I");
        destroy_ = env->GetMethodID(cls, "webDestroy", "(I)V");
        layout_ = env->GetMethodID(cls, "webLayout", "(IIIIIZ)V");
        load_ = env->GetMethodID(cls, "webLoad", "(I[B)V");
        action_ = env->GetMethodID(cls, "webAction", "(II)V");
        text_ = env->GetMethodID(cls, "webText", "(I[B)V");
        key_ = env->GetMethodID(cls, "webKey", "(II)V");
        state_ = env->GetMethodID(cls, "webState", "(I)Ljava/lang/String;");
        snapshot_ = env->GetMethodID(cls, "webSnapshot", "(I)[I");
        clearException(env);
        env->DeleteLocalRef(cls);
    }

    bool valid() const { return create_ && destroy_ && layout_ && load_ && action_ && text_ && key_ && state_ && snapshot_; }

    int create() override {
        jint id = env_->CallIntMethod(activity_, create_);
        clearException(env_);
        return (int)id;
    }
    void destroy(int id) override {
        env_->CallVoidMethod(activity_, destroy_, (jint)id);
        clearException(env_);
    }
    void layout(int id, int x, int y, int w, int h, bool visible) override {
        env_->CallVoidMethod(activity_, layout_, (jint)id, (jint)x, (jint)y, (jint)w, (jint)h, (jboolean)visible);
        clearException(env_);
    }
    void load(int id, const std::string& url) override { callBytes(load_, id, url); }
    void text(int id, const std::string& t) override { callBytes(text_, id, t); }
    void action(int id, int a) override {
        env_->CallVoidMethod(activity_, action_, (jint)id, (jint)a);
        clearException(env_);
    }
    void key(int id, int k) override {
        env_->CallVoidMethod(activity_, key_, (jint)id, (jint)k);
        clearException(env_);
    }
    WebState state(int id) override {
        WebState st;
        jstring js = (jstring)env_->CallObjectMethod(activity_, state_, (jint)id);
        clearException(env_);
        if (!js) return st;
        const char* c = env_->GetStringUTFChars(js, nullptr);
        std::string s = c ? c : "";
        if (c) env_->ReleaseStringUTFChars(js, c);
        env_->DeleteLocalRef(js);
        std::vector<std::string> f;
        size_t p = 0;
        while (f.size() < 9) {
            size_t t = s.find('\t', p);
            if (t == std::string::npos) break;
            f.push_back(s.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 9) return st;
        f.push_back(s.substr(p));
        st.progress = atoi(f[0].c_str());
        st.canBack = f[1] == "1";
        st.canForward = f[2] == "1";
        st.loading = f[3] == "1";
        st.editable = f[4] == "1";
        st.desktop = f[5] == "1";
        st.fullscreen = f[6] == "1";
        st.controls = f[7] == "1";
        st.url = f[8];
        st.title = f[9];
        return st;
    }
    bool snapshot(int id, std::vector<unsigned char>& rgba, int& w, int& h) override {
        jintArray arr = (jintArray)env_->CallObjectMethod(activity_, snapshot_, (jint)id);
        clearException(env_);
        if (!arr) return false;
        jsize n = env_->GetArrayLength(arr);
        bool ok = false;
        if (n > 2) {
            std::vector<jint> data((size_t)n);
            env_->GetIntArrayRegion(arr, 0, n, data.data());
            w = data[0];
            h = data[1];
            if (w > 0 && h > 0 && (jsize)(w * h + 2) <= n) {
                rgba.resize((size_t)w * h * 4);
                for (size_t i = 0; i < (size_t)w * h; i++) {
                    uint32_t v = (uint32_t)data[i + 2];
                    rgba[i * 4 + 0] = (v >> 16) & 255;
                    rgba[i * 4 + 1] = (v >> 8) & 255;
                    rgba[i * 4 + 2] = v & 255;
                    rgba[i * 4 + 3] = 255;
                }
                ok = true;
            }
        }
        env_->DeleteLocalRef(arr);
        return ok;
    }

private:
    void callBytes(jmethodID m, int id, const std::string& s) {
        jbyteArray b = env_->NewByteArray((jsize)s.size());
        if (!b) {
            clearException(env_);
            return;
        }
        env_->SetByteArrayRegion(b, 0, (jsize)s.size(), (const jbyte*)s.data());
        env_->CallVoidMethod(activity_, m, (jint)id, b);
        clearException(env_);
        env_->DeleteLocalRef(b);
    }

    JNIEnv* env_;
    jobject activity_;
    jmethodID create_ = nullptr, destroy_ = nullptr, layout_ = nullptr, load_ = nullptr, action_ = nullptr;
    jmethodID text_ = nullptr, key_ = nullptr, state_ = nullptr, snapshot_ = nullptr;
};

void updateInsets(Engine& e) {
    if (!e.env || !e.shell) return;
    jclass cls = e.env->GetObjectClass(e.app->activity->clazz);
    jmethodID m = e.env->GetMethodID(cls, "getSafeInsets", "()[I");
    clearException(e.env);
    e.env->DeleteLocalRef(cls);
    if (!m) return;
    jintArray arr = (jintArray)e.env->CallObjectMethod(e.app->activity->clazz, m);
    clearException(e.env);
    if (!arr) return;
    jint v[4] = {0, 0, 0, 0};
    if (e.env->GetArrayLength(arr) >= 4) e.env->GetIntArrayRegion(arr, 0, 4, v);
    e.env->DeleteLocalRef(arr);
    e.shell->setInsets((float)v[0], (float)v[1], (float)v[2], (float)v[3]);
}

bool initDisplay(Engine& e) {
    if (e.display == EGL_NO_DISPLAY) {
        e.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(e.display, nullptr, nullptr)) return false;
        const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                                  EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(e.display, attribs, &e.config, 1, &count) || count < 1) return false;
        const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        e.context = eglCreateContext(e.display, e.config, EGL_NO_CONTEXT, ctxAttribs);
        if (e.context == EGL_NO_CONTEXT) return false;
    }
    e.surface = eglCreateWindowSurface(e.display, e.config, e.app->window, nullptr);
    if (e.surface == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(e.display, e.surface, e.surface, e.context)) return false;
    eglSwapInterval(e.display, 1);
    eglQuerySurface(e.display, e.surface, EGL_WIDTH, &e.width);
    eglQuerySurface(e.display, e.surface, EGL_HEIGHT, &e.height);

    if (!e.shell) {
        Platform p;
        p.dataDir = e.app->activity->internalDataPath ? e.app->activity->internalDataPath : "/data/local/tmp";
        p.fontPaths = {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
                       "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf"};
        p.monoFontPaths = {"/system/fonts/DroidSansMono.ttf", "/system/fonts/CutiveMono.ttf",
                           "/system/fonts/NotoSansMono-Regular.ttf"};
        int dpi = AConfiguration_getDensity(e.app->config);
        if (dpi <= 0 || dpi == ACONFIGURATION_DENSITY_ANY || dpi == ACONFIGURATION_DENSITY_NONE) dpi = 320;
        p.density = dpi / 160.0f;
        p.model = prop("ro.product.model");
        p.manufacturer = prop("ro.product.manufacturer");
        p.osVersion = prop("ro.build.version.release");
        p.abi = prop("ro.product.cpu.abi");
        if (p.model.empty()) p.model = "Android device";
        if (!p.manufacturer.empty()) p.manufacturer[0] = (char)toupper((unsigned char)p.manufacturer[0]);
        Engine* ep = &e;
        p.quit = [ep]() { ANativeActivity_finish(ep->app->activity); };
        p.batteryLevel = [ep]() { return batteryLevel(*ep); };
        if (e.env) {
            auto bridge = std::make_shared<JniWebBridge>(e.env, e.app->activity->clazz);
            if (bridge->valid()) p.web = bridge;
        }
        e.shell = std::make_unique<Shell>(p);
        e.shell->init();
    }
    e.shell->resize(e.width, e.height);
    updateInsets(e);
    return true;
}

void termSurface(Engine& e) {
    if (e.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e.surface != EGL_NO_SURFACE) eglDestroySurface(e.display, e.surface);
    }
    e.surface = EGL_NO_SURFACE;
}

void termAll(Engine& e) {
    if (e.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(e.display, e.surface, e.surface, e.context);
        e.shell.reset();
        eglMakeCurrent(e.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (e.surface != EGL_NO_SURFACE) eglDestroySurface(e.display, e.surface);
        if (e.context != EGL_NO_CONTEXT) eglDestroyContext(e.display, e.context);
        eglTerminate(e.display);
    }
    e.display = EGL_NO_DISPLAY;
    e.context = EGL_NO_CONTEXT;
    e.surface = EGL_NO_SURFACE;
}

void drawFrame(Engine& e) {
    if (e.surface == EGL_NO_SURFACE || !e.shell) return;
    EGLint w = 0, h = 0;
    eglQuerySurface(e.display, e.surface, EGL_WIDTH, &w);
    eglQuerySurface(e.display, e.surface, EGL_HEIGHT, &h);
    if (w != e.width || h != e.height) {
        e.width = w;
        e.height = h;
        e.shell->resize(w, h);
        updateInsets(e);
    }
    double t = nowSeconds();
    if (t - e.insetsTime > 1.0) {
        e.insetsTime = t;
        updateInsets(e);
    }
    e.shell->frame(nowSeconds());
    if (!eglSwapBuffers(e.display, e.surface)) {
        EGLint err = eglGetError();
        if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) termSurface(e);
    }
}

int mapKey(int32_t code) {
    switch (code) {
        case AKEYCODE_DEL: return KeyBackspace;
        case AKEYCODE_FORWARD_DEL: return KeyDelete;
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: return KeyEnter;
        case AKEYCODE_DPAD_LEFT: return KeyLeft;
        case AKEYCODE_DPAD_RIGHT: return KeyRight;
        case AKEYCODE_DPAD_UP: return KeyUp;
        case AKEYCODE_DPAD_DOWN: return KeyDown;
        case AKEYCODE_MOVE_HOME: return KeyHome;
        case AKEYCODE_MOVE_END: return KeyEnd;
        case AKEYCODE_TAB: return KeyTab;
        case AKEYCODE_ESCAPE: return KeyEscape;
        default: return KeyNone;
    }
}

int32_t onInput(android_app* app, AInputEvent* event) {
    Engine& e = *(Engine*)app->userData;
    if (!e.shell) return 0;
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        switch (masked) {
            case AMOTION_EVENT_ACTION_DOWN:
                e.primaryPointer = AMotionEvent_getPointerId(event, 0);
                e.shell->pointer(0, AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
                return 1;
            case AMOTION_EVENT_ACTION_MOVE: {
                size_t n = AMotionEvent_getPointerCount(event);
                for (size_t i = 0; i < n; i++) {
                    if (AMotionEvent_getPointerId(event, i) != e.primaryPointer) continue;
                    size_t hist = AMotionEvent_getHistorySize(event);
                    for (size_t hI = 0; hI < hist; hI++)
                        e.shell->pointer(1, AMotionEvent_getHistoricalX(event, i, hI), AMotionEvent_getHistoricalY(event, i, hI));
                    e.shell->pointer(1, AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
                }
                return 1;
            }
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (AMotionEvent_getPointerId(event, index) == e.primaryPointer) {
                    e.shell->pointer(2, AMotionEvent_getX(event, index), AMotionEvent_getY(event, index));
                    e.primaryPointer = -1;
                }
                return 1;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                if (e.primaryPointer >= 0) {
                    size_t n = AMotionEvent_getPointerCount(event);
                    size_t idx = 0;
                    for (size_t i = 0; i < n; i++)
                        if (AMotionEvent_getPointerId(event, i) == e.primaryPointer) idx = i;
                    e.shell->pointer(2, AMotionEvent_getX(event, idx), AMotionEvent_getY(event, idx));
                }
                e.primaryPointer = -1;
                return 1;
            default: return 1;
        }
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        if (code == AKEYCODE_BACK) {
            if (action == AKEY_EVENT_ACTION_UP) e.shell->back();
            return 1;
        }
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_VOLUME_MUTE ||
            code == AKEYCODE_POWER || code == AKEYCODE_HOME)
            return 0;
        if (action != AKEY_EVENT_ACTION_DOWN) return 1;
        int k = mapKey(code);
        if (k != KeyNone) {
            e.shell->hardwareKeyInput();
            e.shell->key(k);
            return 1;
        }
        uint32_t cp = unicodeFor(e, code, AKeyEvent_getMetaState(event));
        if (cp >= 32) {
            e.shell->hardwareKeyInput();
            e.shell->character(cp);
            return 1;
        }
        return 0;
    }
    return 0;
}

void onCmd(android_app* app, int32_t cmd) {
    Engine& e = *(Engine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                initDisplay(e);
                drawFrame(e);
            }
            break;
        case APP_CMD_TERM_WINDOW: termSurface(e); break;
        case APP_CMD_GAINED_FOCUS: e.focused = true; break;
        case APP_CMD_LOST_FOCUS: e.focused = false; break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED: drawFrame(e); break;
        default: break;
    }
}

}

void android_main(android_app* app) {
    Engine engine;
    engine.app = app;
    app->userData = &engine;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;
    app->activity->vm->AttachCurrentThread(&engine.env, nullptr);
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    while (true) {
        int events = 0;
        android_poll_source* source = nullptr;
        bool canDraw = engine.surface != EGL_NO_SURFACE;
        int timeout = canDraw ? 0 : -1;
        int id;
        while ((id = ALooper_pollOnce(timeout, nullptr, &events, (void**)&source)) >= 0 || id == ALOOPER_POLL_CALLBACK) {
            if (id >= 0 && source) source->process(app, source);
            if (app->destroyRequested) {
                termAll(engine);
                app->activity->vm->DetachCurrentThread();
                return;
            }
            canDraw = engine.surface != EGL_NO_SURFACE;
            timeout = canDraw ? 0 : -1;
        }
        drawFrame(engine);
    }
}
