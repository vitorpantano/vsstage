// VS Stage para Windows — ponto de entrada (SDL2 + OpenGL3 + Dear ImGui).
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "App.h"
#include "Ui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_impl_sdl2.h"
#include "imgui.h"

namespace fs = std::filesystem;

namespace {
fs::path findFont() {
    const char* cands[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf",
#endif
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
    for (auto c : cands) if (fs::exists(c)) return c;
    return {};
}

// Captura o framebuffer atual para um BMP (usado só no modo de captura automática de telas).
bool saveScreenshot(SDL_Window* w, SDL_Renderer* r, const std::string& path) {
    int ww = 0, hh = 0;
#ifdef VSS_TEST_RENDERER
    SDL_GetRendererOutputSize(r, &ww, &hh);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ww, hh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
#else
    (void)r;
    SDL_GL_GetDrawableSize(w, &ww, &hh);
    using ReadFn = void (*)(int, int, int, int, unsigned, unsigned, void*);
    auto readPixels = reinterpret_cast<ReadFn>(SDL_GL_GetProcAddress("glReadPixels"));
    if (!readPixels) return false;
    std::vector<unsigned char> px(static_cast<size_t>(ww) * hh * 4);
    readPixels(0, 0, ww, hh, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, ww, hh, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return false;
    for (int y = 0; y < hh; ++y) std::memcpy(static_cast<unsigned char*>(s->pixels) + static_cast<size_t>(y) * s->pitch, &px[static_cast<size_t>(hh - 1 - y) * ww * 4], static_cast<size_t>(ww) * 4);
#endif
    const int rc = SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
    return rc == 0;
}
}  // namespace

#ifdef VSS_TEST_RENDERER
#define RENDERER_ARG renderer
#else
#define RENDERER_ARG nullptr
#endif

int main(int argc, char** argv) {
    std::string shotDir;
    int shotW = 1366, shotH = 800;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shotDir = argv[++i];
        else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) { shotW = std::atoi(argv[++i]); shotH = std::atoi(argv[++i]); }
    }
    if (!shotDir.empty() && !std::getenv("VSS_AUDIO_BACKEND")) {
#if defined(_WIN32)
        _putenv_s("VSS_AUDIO_BACKEND", "null");
#else
        setenv("VSS_AUDIO_BACKEND", "null", 1);
#endif
    }

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);

    #ifdef VSS_TEST_RENDERER
    const Uint32 glFlag = 0;
#else
    const Uint32 glFlag = SDL_WINDOW_OPENGL;
#endif
    const Uint32 flags = glFlag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (shotDir.empty() ? SDL_WINDOW_MAXIMIZED : 0u);
    SDL_Window* window = SDL_CreateWindow("VS Stage", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, shotDir.empty() ? 1366 : shotW, shotDir.empty() ? 800 : shotH, flags);
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(window, 960, 600);
#ifdef VSS_TEST_RENDERER
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_GLContext gl = nullptr;
#else
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {   // alguns drivers antigos não têm 3.2 core: tenta perfil de compatibilidade
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        gl = SDL_GL_CreateContext(window);
    }
    if (!gl) { std::fprintf(stderr, "OpenGL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
#endif

    float dpi = 1.f;
    {
        float ddpi = 96.f;
        if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), &ddpi, nullptr, nullptr) == 0) dpi = std::max(1.f, ddpi / 96.f);
    }
    if (!shotDir.empty()) dpi = 1.f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef VSS_TEST_RENDERER
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
#else
    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 150");
#endif
    const fs::path font = findFont();
    if (!font.empty()) io.Fonts->AddFontFromFileTTF(font.string().c_str(), 17.0f * dpi);
    vss::ui::applyTheme(dpi);

    int exitCode = 0;
    {
        vss::App app(window, vss::Library::defaultRoot());
        app.setUiScale(dpi);
        ImGui::GetStyle().FontScaleDpi = 1.0f;

        int shotStep = -1, frames = 0;
        if (!shotDir.empty()) { fs::create_directories(shotDir); app.seedDemo(); shotStep = 0; }

        bool running = true;
        while (running && !app.wantQuit()) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                ImGui_ImplSDL2_ProcessEvent(&ev);
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE && ev.window.windowID == SDL_GetWindowID(window)) running = false;
                if (ev.type == SDL_DROPFILE) { app.onDrop(ev.drop.file); SDL_free(ev.drop.file); }
            }
#ifdef VSS_TEST_RENDERER
            ImGui_ImplSDLRenderer2_NewFrame();
#else
            ImGui_ImplOpenGL3_NewFrame();
#endif
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();
#ifdef VSS_TEST_RENDERER
            SDL_SetRenderDrawColor(renderer, 13, 13, 15, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
#else
            int dw, dh;
            SDL_GL_GetDrawableSize(window, &dw, &dh);
            using ViewportFn = void (*)(int, int, int, int);
            using ClearColorFn = void (*)(float, float, float, float);
            using ClearFn = void (*)(unsigned);
            static auto glViewportP = reinterpret_cast<ViewportFn>(SDL_GL_GetProcAddress("glViewport"));
            static auto glClearColorP = reinterpret_cast<ClearColorFn>(SDL_GL_GetProcAddress("glClearColor"));
            static auto glClearP = reinterpret_cast<ClearFn>(SDL_GL_GetProcAddress("glClear"));
            if (glViewportP) glViewportP(0, 0, dw, dh);
            if (glClearColorP) glClearColorP(0.05f, 0.05f, 0.06f, 1.f);
            if (glClearP) glClearP(0x4000 /*GL_COLOR_BUFFER_BIT*/);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

            if (shotStep >= 0) {
                ++frames;
                // sequência: a cada 40 quadros captura uma tela e passa para a próxima
                struct Step { vss::Tab tab; const char* name; int modal; };
                static const Step steps[] = {{vss::Tab::Stage, "palco", -1}, {vss::Tab::Stage, "secoes", 0}, {vss::Tab::Library, "biblioteca", -1},
                                             {vss::Tab::Library, "importar", 1}, {vss::Tab::Setlists, "setlists", -1}, {vss::Tab::Output, "saida", -1},
                                             {vss::Tab::Pedals, "pedais", -1}};
                constexpr int kSteps = 7, per = 40;
                const int idx = frames / per;
                if (frames % per == 1 && idx < kSteps) { app.setTab(steps[idx].tab); app.debugShowModal(steps[idx].modal); }
                if (frames % per == per - 1 && idx < kSteps) saveScreenshot(window, RENDERER_ARG, shotDir + "/" + steps[idx].name + ".bmp");
                if (frames == 30 && app.player().isLoaded()) {   // põe o palco para tocar antes da 1ª captura
                    const auto& secs = app.player().map.sections;
                    app.player().seekFrame(app.player().map.sectionStart(secs.size() > 1 ? secs[1] : secs[0]));
                    app.player().play();
                }
                if (idx >= kSteps) running = false;
            }
            #ifdef VSS_TEST_RENDERER
            SDL_RenderPresent(renderer);
#else
            SDL_GL_SwapWindow(window);
#endif
        }
    }
#ifdef VSS_TEST_RENDERER
    ImGui_ImplSDLRenderer2_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
#ifdef VSS_TEST_RENDERER
    SDL_DestroyRenderer(renderer);
#else
    SDL_GL_DeleteContext(gl);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
