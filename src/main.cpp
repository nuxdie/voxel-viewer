// voxel-viewer: OpenGL viewer for MagicaVoxel and Minecraft (WorldEdit, Sponge, Litematica) files.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "gl_loader.h"  // must come before GLFW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "camera.h"
#include "image_writer.h"
#include "loader.h"
#include "mesher.h"
#include "renderer.h"

namespace fs = std::filesystem;
using namespace vox;

namespace {

const char* kHelp = R"(voxel-viewer controls
  Mouse     left drag: orbit (fly mode: look)   right/middle drag: pan   wheel: zoom
  Tab       toggle orbit / fly camera
  W A S D   fly: move   Q/E or Ctrl/Space: down/up   Shift: faster
  F         frame model (reset view)
  1 2 3 4   front / right / top / isometric view
  PgUp/PgDn move the cut-away slice up/down (Shift: x10)   Home: remove slice
  G         toggle grid            B   toggle bounding box
  X         toggle wireframe       O   toggle ambient occlusion
  V         toggle small decorations (torches, flowers, rails...)
  L         toggle light/dark background
  ] / [     next / previous file in the folder
  Ctrl+O    open file dialog (zenity/kdialog)   or drag & drop files onto the window
  F12       save screenshot (PNG)
  H         print this help       Esc: quit
)";

struct Loaded {
    std::unique_ptr<VoxelModel> model;
    std::vector<ChunkMesh> meshes;
    std::string path, error;
    double loadMs = 0, meshMs = 0;
};

Loaded loadAndMesh(const std::string& path, MeshOptions opts) {
    Loaded r;
    r.path = path;
    try {
        auto t0 = std::chrono::steady_clock::now();
        r.model = loadModelFile(path);
        auto t1 = std::chrono::steady_clock::now();
        r.meshes = buildMeshes(*r.model, opts);
        auto t2 = std::chrono::steady_clock::now();
        r.loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.meshMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    } catch (const std::exception& e) {
        r.error = e.what();
        r.model.reset();
    }
    return r;
}

std::string runFileDialog() {
    const char* cmds[] = {
        "zenity --file-selection --title='Open voxel model' "
        "--file-filter='Voxel models | *.vox *.schem *.schematic *.litematic *.nbt' --file-filter='All files | *' 2>/dev/null",
        "kdialog --getopenfilename . 'Voxel models (*.vox *.schem *.schematic *.litematic *.nbt)' 2>/dev/null",
    };
    for (const char* cmd : cmds) {
        FILE* p = popen(cmd, "r");
        if (!p) continue;
        char buf[4096];
        std::string out;
        while (fgets(buf, sizeof(buf), p)) out += buf;
        int status = pclose(p);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        if (status == 0 && !out.empty()) return out;
        if (status == 0 || WEXITSTATUS(status) == 1) return {};  // dialog ran but was cancelled
    }
    std::fprintf(stderr, "No file dialog available (install zenity or kdialog), or drag & drop a file.\n");
    return {};
}

std::string formatCount(size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

class App {
public:
    int run(int argc, char** argv);

private:
    bool parseArgs(int argc, char** argv);
    void setPlaylistFromFile(const std::string& path);
    void openFile(const std::string& path);
    void pollLoad();
    void applyLoaded(Loaded&& l);
    void remesh();
    void updateTitle();
    void saveScreenshot(const std::string& path);
    void onKey(int key, int mods);
    void setClip(int layer);

    GLFWwindow* win_ = nullptr;
    Renderer renderer_;
    Camera camera_;
    RenderSettings settings_;
    MeshOptions meshOpts_;
    std::unique_ptr<VoxelModel> model_;
    std::string currentPath_;
    std::vector<std::string> playlist_;
    std::future<Loaded> pending_;
    std::string pendingPath_;
    std::future<std::string> dialog_;
    int clipLayer_ = INT32_MAX;  // highest visible voxel layer (world Y)

    // Mouse state
    double lastX_ = 0, lastY_ = 0;
    bool rotating_ = false, panning_ = false;

    // Options
    std::vector<std::string> files_;
    std::string screenshotPath_;
    int width_ = 1600, height_ = 1000;
    bool usePrime_ = true;
    int msaa_ = 8;
    float fps_ = 0;
    std::string status_;
};

bool App::parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "-h" || a == "--help") {
            std::printf("usage: voxel-viewer [options] [file...]\n\n"
                        "Supported: MagicaVoxel .vox, WorldEdit/MCEdit .schematic, Sponge .schem (v1-v3),\n"
                        "           Litematica .litematic, Minecraft structure .nbt\n\n"
                        "options:\n"
                        "  --screenshot FILE.png  render the first file to a PNG and exit\n"
                        "  --size WxH             window / screenshot size (default 1600x1000)\n"
                        "  --hide-decorations     hide torches, flowers, rails and other small blocks\n"
                        "  --no-ao                disable ambient occlusion\n"
                        "  --msaa N               multisample count (default 8, 0 to disable)\n"
                        "  --no-prime             do not request the NVIDIA GPU on hybrid-graphics laptops\n\n%s",
                        kHelp);
            return false;
        } else if (a == "--screenshot") {
            screenshotPath_ = next();
        } else if (a == "--size") {
            std::sscanf(next().c_str(), "%dx%d", &width_, &height_);
            width_ = std::max(64, width_);
            height_ = std::max(64, height_);
        } else if (a == "--hide-decorations") {
            meshOpts_.hideDecorations = true;
        } else if (a == "--no-ao") {
            settings_.aoStrength = 0;
        } else if (a == "--msaa") {
            msaa_ = std::atoi(next().c_str());
        } else if (a == "--no-prime") {
            usePrime_ = false;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s (see --help)\n", a.c_str());
            return false;
        } else {
            files_.push_back(a);
        }
    }
    return true;
}

void App::setPlaylistFromFile(const std::string& path) {
    playlist_.clear();
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    if (dir.empty()) dir = ".";
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && hasSupportedExtension(e.path().string())) playlist_.push_back(e.path().string());
    std::sort(playlist_.begin(), playlist_.end());
    if (std::find(playlist_.begin(), playlist_.end(), path) == playlist_.end()) {
        // Path spelled differently (e.g. no "./"); fall back to matching by filename.
        bool found = false;
        for (auto& p : playlist_)
            if (fs::path(p).filename() == fs::path(path).filename()) { p = path; found = true; }
        if (!found) playlist_.insert(playlist_.begin(), path);
    }
}

void App::openFile(const std::string& path) {
    if (pending_.valid()) return;  // one load at a time
    pendingPath_ = path;
    pending_ = std::async(std::launch::async, loadAndMesh, path, meshOpts_);
    status_ = "loading…";
    updateTitle();
}

void App::pollLoad() {
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        applyLoaded(pending_.get());
    }
    if (dialog_.valid() && dialog_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string p = dialog_.get();
        if (!p.empty()) {
            setPlaylistFromFile(p);
            openFile(p);
        }
    }
}

void App::applyLoaded(Loaded&& l) {
    status_.clear();
    if (!l.model) {
        std::fprintf(stderr, "%s: %s\n", l.path.c_str(), l.error.c_str());
        status_ = "error: " + l.error;
        updateTitle();
        return;
    }
    bool sameFile = l.path == currentPath_;
    model_ = std::move(l.model);
    currentPath_ = l.path;
    IVec3 mn = model_->boundsMin(), mx = model_->boundsMax();
    if (model_->empty()) mn = mx = {0, 0, 0};
    renderer_.upload(l.meshes, mn, mx);
    if (!sameFile) {
        camera_.frame(Vec3(float(mn.x), float(mn.y), float(mn.z)), Vec3(float(mx.x + 1), float(mx.y + 1), float(mx.z + 1)));
        setClip(INT32_MAX);
    }
    IVec3 s = model_->size();
    std::printf("%s: %s, %d x %d x %d, %s voxels, %zu materials, %s triangles, %.1f MB GPU (load %.0f ms, mesh %.0f ms)\n",
                l.path.c_str(), model_->format.c_str(), s.x, s.y, s.z, formatCount(model_->voxelCount()).c_str(),
                model_->materials.size() - 1, formatCount(renderer_.totalTriangles()).c_str(),
                double(renderer_.gpuBytes()) / (1024.0 * 1024.0), l.loadMs, l.meshMs);
    std::fflush(stdout);
    updateTitle();
}

void App::remesh() {
    if (!model_ || pending_.valid()) return;
    auto t0 = std::chrono::steady_clock::now();
    auto meshes = buildMeshes(*model_, meshOpts_);
    renderer_.upload(meshes, model_->boundsMin(), model_->boundsMax());
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("remeshed in %.0f ms (%s triangles)\n", ms, formatCount(renderer_.totalTriangles()).c_str());
}

void App::setClip(int layer) {
    clipLayer_ = layer;
    if (model_ && !model_->empty() && layer < model_->boundsMax().y) {
        clipLayer_ = std::max(layer, model_->boundsMin().y);
        settings_.clipY = float(clipLayer_ + 1) + 0.001f;
    } else {
        clipLayer_ = INT32_MAX;
        settings_.clipY = 1e9f;
    }
    updateTitle();
}

void App::updateTitle() {
    if (!win_) return;
    std::string t = "voxel-viewer";
    if (model_) {
        IVec3 s = model_->size();
        t += " — " + fs::path(currentPath_).filename().string();
        t += " — " + model_->format;
        t += " — " + std::to_string(s.x) + "×" + std::to_string(s.y) + "×" + std::to_string(s.z);
        t += " — " + formatCount(model_->voxelCount()) + " voxels";
        if (clipLayer_ != INT32_MAX) t += " — slice y≤" + std::to_string(clipLayer_);
        if (meshOpts_.hideDecorations) t += " — decorations hidden";
        if (camera_.mode() == Camera::Mode::Fly) t += " — FLY";
        char buf[64];
        std::snprintf(buf, sizeof(buf), " — %.0f fps", double(fps_));
        t += buf;
    } else if (status_.empty()) {
        t += " — drop a .vox / .schem / .schematic / .litematic file here, or press Ctrl+O";
    }
    if (!status_.empty()) t += " — " + status_;
    glfwSetWindowTitle(win_, t.c_str());
}

void App::saveScreenshot(const std::string& path) {
    int w, h;
    glfwGetFramebufferSize(win_, &w, &h);
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 3), flipped(px.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[size_t(y) * size_t(w) * 3], &px[size_t(h - 1 - y) * size_t(w) * 3], size_t(w) * 3);
    if (writePng(path, w, h, flipped.data())) std::printf("saved %s (%dx%d)\n", path.c_str(), w, h);
    else std::fprintf(stderr, "failed to write %s\n", path.c_str());
}

void App::onKey(int key, int mods) {
    bool shift = mods & GLFW_MOD_SHIFT, ctrl = mods & GLFW_MOD_CONTROL;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win_, 1); break;
        case GLFW_KEY_H: std::fputs(kHelp, stdout); std::fflush(stdout); break;
        case GLFW_KEY_TAB:
            camera_.setMode(camera_.mode() == Camera::Mode::Orbit ? Camera::Mode::Fly : Camera::Mode::Orbit);
            updateTitle();
            break;
        case GLFW_KEY_F:
            if (model_ && !model_->empty()) {
                IVec3 mn = model_->boundsMin(), mx = model_->boundsMax();
                camera_.frame(Vec3(float(mn.x), float(mn.y), float(mn.z)), Vec3(float(mx.x + 1), float(mx.y + 1), float(mx.z + 1)));
                updateTitle();
            }
            break;
        case GLFW_KEY_1: camera_.setView(0, 0); break;
        case GLFW_KEY_2: camera_.setView(90, 0); break;
        case GLFW_KEY_3: camera_.setView(0, 89); break;
        case GLFW_KEY_4: camera_.setView(45, 30); break;
        case GLFW_KEY_G: settings_.grid = !settings_.grid; break;
        case GLFW_KEY_B: settings_.bounds = !settings_.bounds; break;
        case GLFW_KEY_X: settings_.wireframe = !settings_.wireframe; break;
        case GLFW_KEY_O:
            if (ctrl) {
                if (!dialog_.valid()) dialog_ = std::async(std::launch::async, runFileDialog);
            } else {
                settings_.aoStrength = settings_.aoStrength > 0 ? 0.0f : 1.0f;
            }
            break;
        case GLFW_KEY_L: settings_.darkBackground = !settings_.darkBackground; break;
        case GLFW_KEY_V:
            meshOpts_.hideDecorations = !meshOpts_.hideDecorations;
            remesh();
            updateTitle();
            break;
        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_PAGE_DOWN:
            if (model_ && !model_->empty()) {
                int step = (shift ? 10 : 1) * (key == GLFW_KEY_PAGE_UP ? 1 : -1);
                int cur = clipLayer_ == INT32_MAX ? model_->boundsMax().y : clipLayer_;
                setClip(cur + step);
            }
            break;
        case GLFW_KEY_HOME: setClip(INT32_MAX); break;
        case GLFW_KEY_RIGHT_BRACKET:
        case GLFW_KEY_LEFT_BRACKET:
            if (playlist_.size() > 1) {
                auto it = std::find(playlist_.begin(), playlist_.end(), currentPath_);
                long i = it == playlist_.end() ? 0 : long(it - playlist_.begin());
                long n = long(playlist_.size());
                i = (i + (key == GLFW_KEY_RIGHT_BRACKET ? 1 : n - 1)) % n;
                openFile(playlist_[size_t(i)]);
            }
            break;
        case GLFW_KEY_F12: {
            std::string base = currentPath_.empty() ? "voxel-viewer" : fs::path(currentPath_).stem().string();
            std::string p;
            for (int i = 0;; ++i) {
                p = base + (i ? "-" + std::to_string(i) : std::string()) + ".png";
                if (!fs::exists(p)) break;
            }
            saveScreenshot(p);
            break;
        }
        default: break;
    }
}

int App::run(int argc, char** argv) {
    if (!parseArgs(argc, argv)) return 1;

    // Hybrid-graphics laptops: if the NVIDIA driver is loaded but the default context is on
    // another GPU, we re-exec ourselves with PRIME render offload enabled (see below).
    // Offload is never requested when NVIDIA already drives the display: on desktops
    // that can leave the window unpresented.
    bool nvidiaPresent = fs::exists("/proc/driver/nvidia/version");
    bool primeRetry = std::getenv("VV_PRIME_REEXEC") != nullptr;

    glfwSetErrorCallback([](int code, const char* msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); });
    if (!glfwInit()) return 1;
    bool headless = !screenshotPath_.empty();
    auto createWindow = [&](int samples) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_SAMPLES, samples);
        if (headless) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        return glfwCreateWindow(width_, height_, "voxel-viewer", nullptr, nullptr);
    };
    win_ = createWindow(msaa_);
    if (!win_ && msaa_ > 0) win_ = createWindow(0);
    if (!win_ && primeRetry) {
        // PRIME offload not configured: fall back to the default GPU.
        std::fprintf(stderr, "NVIDIA offload context failed, using the default GPU\n");
        glfwTerminate();
        unsetenv("__NV_PRIME_RENDER_OFFLOAD");
        unsetenv("__GLX_VENDOR_LIBRARY_NAME");
        if (!glfwInit()) return 1;
        win_ = createWindow(msaa_);
        if (!win_ && msaa_ > 0) win_ = createWindow(0);
    }
    if (!win_) {
        std::fprintf(stderr, "Could not create an OpenGL 3.3 core window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(1);
    if (!gl::load([](const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); })) return 1;
    {
        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        bool onNvidia = vendor && std::strstr(vendor, "NVIDIA");
        if (!onNvidia && nvidiaPresent && usePrime_ && !primeRetry && !std::getenv("__GLX_VENDOR_LIBRARY_NAME")) {
            std::printf("Rendering on %s; restarting on the NVIDIA GPU (PRIME offload, --no-prime to skip)\n",
                        vendor ? vendor : "unknown GPU");
            std::fflush(stdout);
            glfwDestroyWindow(win_);
            glfwTerminate();
            setenv("VV_PRIME_REEXEC", "1", 1);
            setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
            setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
            execv("/proc/self/exe", argv);
            std::perror("execv");
            return 1;
        }
    }
    std::printf("OpenGL %s on %s (%s)\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    if (msaa_ > 0) glEnable(GL_MULTISAMPLE);
    if (!renderer_.init()) return 1;

    glfwSetWindowUserPointer(win_, this);
    glfwSetKeyCallback(win_, [](GLFWwindow* w, int key, int, int action, int mods) {
        if (action == GLFW_PRESS || (action == GLFW_REPEAT && (key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN)))
            static_cast<App*>(glfwGetWindowUserPointer(w))->onKey(key, mods);
    });
    glfwSetMouseButtonCallback(win_, [](GLFWwindow* w, int button, int action, int) {
        App* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        bool down = action == GLFW_PRESS;
        if (button == GLFW_MOUSE_BUTTON_LEFT) a->rotating_ = down;
        if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) a->panning_ = down;
        glfwGetCursorPos(w, &a->lastX_, &a->lastY_);
    });
    glfwSetCursorPosCallback(win_, [](GLFWwindow* w, double x, double y) {
        App* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        float dx = float(x - a->lastX_), dy = float(y - a->lastY_);
        a->lastX_ = x;
        a->lastY_ = y;
        if (a->rotating_) {
            if (a->camera_.mode() == Camera::Mode::Fly) a->camera_.look(dx, dy);
            else a->camera_.orbit(dx, dy);
        } else if (a->panning_) {
            int fw, fh;
            glfwGetWindowSize(w, &fw, &fh);
            a->camera_.pan(dx, dy, fh);
        }
    });
    glfwSetScrollCallback(win_, [](GLFWwindow* w, double, double dy) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->camera_.zoom(float(dy));
    });
    glfwSetDropCallback(win_, [](GLFWwindow* w, int count, const char** paths) {
        App* a = static_cast<App*>(glfwGetWindowUserPointer(w));
        if (count == 1) {
            a->setPlaylistFromFile(paths[0]);
        } else {
            a->playlist_.assign(paths, paths + count);
        }
        a->openFile(paths[0]);
    });

    if (!files_.empty()) {
        if (files_.size() == 1 && !headless) setPlaylistFromFile(files_[0]);
        else playlist_ = files_;
        openFile(files_[0]);
    } else if (headless) {
        std::fprintf(stderr, "--screenshot requires a file\n");
        return 1;
    } else {
        std::printf("No file given. Drag & drop a file onto the window or press Ctrl+O. Press H for help.\n");
    }
    updateTitle();

    if (headless) {
        applyLoaded(pending_.get());
        if (!model_) return 1;
        int fw, fh;
        glfwGetFramebufferSize(win_, &fw, &fh);
        renderer_.render(camera_, fw, fh, settings_);
        glFinish();
        saveScreenshot(screenshotPath_);
        renderer_.shutdown();
        glfwDestroyWindow(win_);
        glfwTerminate();
        return 0;
    }

    auto last = std::chrono::steady_clock::now();
    auto titleTimer = last;
    int frames = 0;
    while (!glfwWindowShouldClose(win_)) {
        bool busy = pending_.valid() || dialog_.valid();
        if (busy) glfwWaitEventsTimeout(0.05);
        else glfwPollEvents();
        pollLoad();

        auto now = std::chrono::steady_clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
        last = now;

        if (camera_.mode() == Camera::Mode::Fly) {
            Vec3 d;
            auto key = [&](int k) { return glfwGetKey(win_, k) == GLFW_PRESS; };
            if (key(GLFW_KEY_W)) d.z += 1;
            if (key(GLFW_KEY_S)) d.z -= 1;
            if (key(GLFW_KEY_D)) d.x += 1;
            if (key(GLFW_KEY_A)) d.x -= 1;
            if (key(GLFW_KEY_E) || key(GLFW_KEY_SPACE)) d.y += 1;
            if (key(GLFW_KEY_Q) || key(GLFW_KEY_LEFT_CONTROL)) d.y -= 1;
            if (d.x != 0 || d.y != 0 || d.z != 0)
                camera_.move(d, dt, key(GLFW_KEY_LEFT_SHIFT) || key(GLFW_KEY_RIGHT_SHIFT));
        }

        int fw, fh;
        glfwGetFramebufferSize(win_, &fw, &fh);
        if (fw > 0 && fh > 0) renderer_.render(camera_, fw, fh, settings_);
        glfwSwapBuffers(win_);

        ++frames;
        float since = std::chrono::duration<float>(now - titleTimer).count();
        if (since >= 0.5f) {
            fps_ = float(frames) / since;
            frames = 0;
            titleTimer = now;
            updateTitle();
        }
    }
    if (pending_.valid()) pending_.wait();
    renderer_.shutdown();
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    App app;
    return app.run(argc, argv);
}
