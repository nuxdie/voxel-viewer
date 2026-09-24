// JNI bridge: the Android app drives the shared C++ viewer through these calls.
// Every function except nativeLoad/nativeStatus runs on the GLSurfaceView render thread.
#include <jni.h>
#include <android/log.h>

#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "camera.h"
#include "loader.h"
#include "mesher.h"
#include "renderer.h"
#include "smooth_mesher.h"
#include "splats.h"

using namespace vox;

namespace {

enum Mode { kBlocks = 0, kSmooth = 1, kPainted = 2, kWatercolor = 3, kModeCount = 4 };
const char* kModeNames[] = {"Blocks", "Smooth", "Painted", "Watercolor"};

struct Meshes {
    std::unique_ptr<VoxelModel> model;  // set when a new file was loaded
    std::vector<ChunkMesh> blocky;
    std::vector<SmoothChunkMesh> smooth;
    std::vector<SplatChunk> splats;
    int mode = kBlocks;
    const VoxelModel* source = nullptr;  // remesh jobs: the model they were built from
    std::string name, error;
    double ms = 0;
};

void buildFor(const VoxelModel& model, int mode, const MeshOptions& opts, Meshes& out) {
    out.mode = mode;
    if (mode == kSmooth) out.smooth = buildSmoothMeshes(model, opts, opts.smoothIterations);
    else if (mode == kPainted || mode == kWatercolor) out.splats = buildSplats(model, opts);
    else out.blocky = buildMeshes(model, opts);
}

std::string formatCount(size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

struct Viewer {
    std::unique_ptr<Renderer> renderer;
    Camera camera;
    RenderSettings settings;
    MeshOptions opts;
    std::shared_ptr<VoxelModel> model;  // shared with background remesh jobs
    std::string name;
    int mode = kBlocks;
    int uploadedMode = -1;
    int clipLayer = INT32_MAX;
    int width = 1, height = 1;
    std::future<Meshes> job;      // remesh of the current model
    std::future<Meshes> loadJob;  // loading a new file
    bool dirty = false;           // meshes do not match the current mode/options

    std::mutex statusMutex;
    std::string status = "Tap Open to load a .vox, .schem, .schematic or .litematic file";

    void setStatus(const std::string& s) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status = s;
    }

    void updateStatus() {
        if (!model) return;
        IVec3 s = model->size();
        std::string t = name + " · " + std::to_string(s.x) + "×" + std::to_string(s.y) + "×" + std::to_string(s.z) +
                        " · " + formatCount(model->voxelCount()) + " voxels · " + kModeNames[mode];
        if (clipLayer != INT32_MAX) t += " · slice y≤" + std::to_string(clipLayer);
        if (opts.hideDecorations) t += " · no decorations";
        if (settings.night) t += " · night";
        if (job.valid() || loadJob.valid()) t += " · working…";
        setStatus(t);
    }

    void upload(Meshes& m) {
        IVec3 mn = model->boundsMin(), mx = model->boundsMax();
        if (model->empty()) mn = mx = {0, 0, 0};
        settings.paintStyle = m.mode == kWatercolor ? 1 : 0;
        if (m.mode == kSmooth) renderer->upload(m.smooth, mn, mx);
        else if (m.mode == kPainted || m.mode == kWatercolor) renderer->upload(m.splats, mn, mx);
        else renderer->upload(m.blocky, mn, mx);
        uploadedMode = m.mode;
    }

    void frameModel() {
        if (!model || model->empty()) return;
        IVec3 mn = model->boundsMin(), mx = model->boundsMax();
        camera.frame(Vec3(float(mn.x), float(mn.y), float(mn.z)), Vec3(float(mx.x + 1), float(mx.y + 1), float(mx.z + 1)));
    }

    void setClip(int layer) {
        if (model && !model->empty() && layer < model->boundsMax().y) {
            clipLayer = std::max(layer, model->boundsMin().y);
            settings.clipY = float(clipLayer + 1) + 0.001f;
        } else {
            clipLayer = INT32_MAX;
            settings.clipY = 1e9f;
        }
        updateStatus();
    }

    // Request new meshes for the current mode/options; built in the background as soon as no
    // other job is running, so requests made while busy are never lost.
    void remesh() {
        dirty = true;
        startRemesh();
    }

    void startRemesh() {
        if (!model || job.valid() || loadJob.valid()) return;
        dirty = false;
        std::shared_ptr<VoxelModel> m = model;
        int md = mode;
        MeshOptions o = opts;
        job = std::async(std::launch::async, [m, md, o] {
            Meshes out;
            out.source = m.get();
            buildFor(*m, md, o, out);
            return out;
        });
        updateStatus();
    }

    static bool ready(std::future<Meshes>& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void poll() {
        if (ready(loadJob)) {
            Meshes m = loadJob.get();
            apply(m);
        }
        if (ready(job)) {
            Meshes m = job.get();
            // Drop remesh results that belong to a model replaced in the meantime.
            if (m.source == model.get()) apply(m);
        }
        if (dirty) startRemesh();
    }

    void apply(Meshes& m) {
        if (!m.error.empty()) {
            setStatus("Could not open " + m.name + ": " + m.error);
            return;
        }
        bool newModel = m.model != nullptr;
        if (newModel) {
            model = std::shared_ptr<VoxelModel>(std::move(m.model));
            name = m.name;
        }
        upload(m);
        if (newModel) {
            frameModel();
            setClip(INT32_MAX);
        }
        // The mode may have changed while a load was running.
        if (uploadedMode != mode) dirty = true;
        updateStatus();
    }
};

Viewer* g = nullptr;

std::string jstr(JNIEnv* env, jstring s) {
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return r;
}

}  // namespace

#define JNI(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_nuxdie_voxelviewer_NativeLib_##name

JNI(void, nativeCreate)(JNIEnv*, jclass) {
    if (!g) g = new Viewer();
}

// Called whenever a new EGL context is created; old GL objects are gone at that point.
JNI(jboolean, nativeSurfaceCreated)(JNIEnv*, jclass) {
    g->renderer.reset(new Renderer());  // do not call shutdown(): its GL names belonged to the old context
    if (!g->renderer->init()) {
        __android_log_print(ANDROID_LOG_ERROR, "voxel-viewer", "renderer init failed");
        return JNI_FALSE;
    }
    g->uploadedMode = -1;
    if (g->model) g->remesh();
    return JNI_TRUE;
}

JNI(void, nativeResize)(JNIEnv*, jclass, jint w, jint h) {
    g->width = std::max(1, int(w));
    g->height = std::max(1, int(h));
}

JNI(void, nativeDraw)(JNIEnv*, jclass) {
    g->poll();
    g->renderer->render(g->camera, g->width, g->height, g->settings);
}

JNI(void, nativeLoad)(JNIEnv* env, jclass, jbyteArray data, jstring jname) {
    std::string name = jstr(env, jname);
    if (g->loadJob.valid()) {
        g->setStatus("Still loading the previous file…");
        return;
    }
    jsize n = env->GetArrayLength(data);
    auto bytes = std::make_shared<std::vector<uint8_t>>(size_t(n));
    env->GetByteArrayRegion(data, 0, n, reinterpret_cast<jbyte*>(bytes->data()));
    g->setStatus("Loading " + name + "…");
    int mode = g->mode;
    MeshOptions o = g->opts;
    g->loadJob = std::async(std::launch::async, [bytes, name, mode, o] {
        Meshes m;
        m.name = name;
        try {
            auto t0 = std::chrono::steady_clock::now();
            m.model = loadModelData(*bytes);
            buildFor(*m.model, mode, o, m);
            m.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        } catch (const std::exception& e) {
            m.model.reset();
            m.error = e.what();
        }
        return m;
    });
}

JNI(void, nativeOrbit)(JNIEnv*, jclass, jfloat dx, jfloat dy) {
    if (g->camera.mode() == Camera::Mode::Orbit) g->camera.orbit(dx, dy);
}

JNI(void, nativePan)(JNIEnv*, jclass, jfloat dx, jfloat dy) { g->camera.pan(dx, dy, g->height); }

// factor > 1 zooms in (pinch out).
JNI(void, nativeZoom)(JNIEnv*, jclass, jfloat factor) {
    if (factor > 0) g->camera.zoom(std::log(factor) / std::log(1.0f / 0.88f));
}

JNI(void, nativeFrame)(JNIEnv*, jclass) { g->frameModel(); }

JNI(jint, nativeCycleMode)(JNIEnv*, jclass) {
    int prev = g->mode;
    g->mode = (g->mode + 1) % kModeCount;
    // Painted and watercolor share the same splats: switching between them needs no remesh.
    bool splatPair = (prev == kPainted && g->mode == kWatercolor);
    if (splatPair && g->uploadedMode == kPainted) {
        g->settings.paintStyle = 1;
        g->uploadedMode = kWatercolor;
    } else {
        g->remesh();
    }
    g->updateStatus();
    return g->mode;
}

JNI(jstring, nativeModeName)(JNIEnv* env, jclass) { return env->NewStringUTF(kModeNames[g->mode]); }

JNI(void, nativeSlice)(JNIEnv*, jclass, jint delta) {
    if (!g->model || g->model->empty()) return;
    int cur = g->clipLayer == INT32_MAX ? g->model->boundsMax().y : g->clipLayer;
    g->setClip(cur + delta);
}

JNI(void, nativeClearSlice)(JNIEnv*, jclass) { g->setClip(INT32_MAX); }

JNI(void, nativeToggleNight)(JNIEnv*, jclass) {
    g->settings.night = !g->settings.night;
    g->updateStatus();
}

JNI(void, nativeToggleDecorations)(JNIEnv*, jclass) {
    g->opts.hideDecorations = !g->opts.hideDecorations;
    g->remesh();
    g->updateStatus();
}

// Thread-safe: polled from the UI thread.
JNI(jstring, nativeStatus)(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(g->statusMutex);
    return env->NewStringUTF(g->status.c_str());
}
