#include "imgui_layer.h"

#include <SDL3/SDL.h>

#include "comic_viewer.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"

static bool g_imgui_initialized = false;
static bool g_imgui_visible = false;
static SDL_Window *g_window = nullptr;
static SDL_Renderer *g_renderer = nullptr;

bool imgui_layer_init(SDL_Window *window, SDL_Renderer *renderer) {
    if (g_imgui_initialized) return true;
    if (!window || !renderer) return false;

    g_window = window;
    g_renderer = renderer;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer)) {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(g_renderer)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_imgui_initialized = true;
    return true;
}

void imgui_layer_shutdown(void) {
    if (!g_imgui_initialized) return;

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_imgui_initialized = false;
    g_imgui_visible = false;
    g_window = nullptr;
    g_renderer = nullptr;
}

bool imgui_layer_is_initialized(void) {
    return g_imgui_initialized;
}

void imgui_layer_process_event(const SDL_Event *event) {
    if (!g_imgui_initialized || !event) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

void imgui_layer_new_frame(void) {
    if (!g_imgui_initialized) return;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

static void imgui_layer_build_ui(void) {
    if (!g_imgui_visible) return;

    ImGui::SetNextWindowSize(ImVec2(460.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ic", &g_imgui_visible)) {
        ImGui::TextUnformatted("Dear ImGui overlay (F1 to toggle)");
        ImGui::Separator();
        ImGui::Text("Renderer: %s", SDL_GetRendererName(g_renderer));

        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(g_window, &win_w, &win_h);
        ImGui::Text("Window: %dx%d", win_w, win_h);

        int pix_w = 0, pix_h = 0;
        SDL_GetWindowSizeInPixels(g_window, &pix_w, &pix_h);
        ImGui::Text("Drawable: %dx%d", pix_w, pix_h);

        ImGui::Spacing();

        ImGui::SeparatorText("Visual");
        {
            static const char *overlay_items[] = {"Gradient", "Stretched", "Ambilight"};
            int overlay_index = (int)viewer.overlay_mode;
            if (overlay_index < 0) overlay_index = 0;
            if (overlay_index > 2) overlay_index = 2;

            if (ImGui::Combo("Overlay Mode", &overlay_index, overlay_items, IM_ARRAYSIZE(overlay_items))) {
                viewer.overlay_mode = (OverlayMode)overlay_index;
                viewer.last_page_change_time = SDL_GetTicks();
            }
        }

        ImGui::SeparatorText("Zoom / Pan");
        ImGui::Text("Zoomed: %s", viewer.zoomed ? "ON" : "OFF");
        ImGui::Text("Zoom center: (%.0f, %.0f)", viewer.zoom_center_x, viewer.zoom_center_y);
        ImGui::Text("Zoom level: %.2f", viewer.zoom_level);
        ImGui::Text("Pan offset: (%.0f, %.0f)", viewer.pan_offset_x, viewer.pan_offset_y);
        ImGui::Text("Pan velocity: (%.0f, %.0f)", viewer.pan_velocity_x, viewer.pan_velocity_y);
    }
    ImGui::End();
}

void imgui_layer_render(void) {
    if (!g_imgui_initialized) return;

    imgui_layer_build_ui();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}

void imgui_layer_set_visible(bool visible) {
    g_imgui_visible = visible;
}

bool imgui_layer_is_visible(void) {
    return g_imgui_visible;
}

bool imgui_layer_wants_capture_keyboard(void) {
    if (!g_imgui_initialized) return false;
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

bool imgui_layer_wants_capture_mouse(void) {
    if (!g_imgui_initialized) return false;
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse;
}
