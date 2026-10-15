#undef NDEBUG
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include "../src/comic_viewer.c"
#include "../src/clipboard.c"

static char sandbox[64];
static char first_path[PATH_MAX];
static char second_path[PATH_MAX];
static float last_progress;
static int scan_updates;

static void record_progress(float progress, const char *message) {
    assert(progress >= last_progress && progress <= 1.0f);
    last_progress = progress;
    if (strstr(message, "Scanning archive (") || strstr(message, "Scanning directory (")) scan_updates++;
}

static void make_image(const char *path) {
    SDL_Surface *surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_BGR24);
    assert(surface);
    assert(SDL_FillSurfaceRect(surface, NULL, 0x2468ac));
    assert(SDL_SaveBMP(surface, path));
    SDL_DestroySurface(surface);
}

static void make_archive(const char *path, int count, const char *first_name) {
    int error = 0;
    zip_t *archive = zip_open(path, ZIP_CREATE | ZIP_EXCL, &error);
    assert(archive);
    static const char payload[] = "image payload";
    for (int index = 0; index < count; index++) {
        char name[64];
        snprintf(name, sizeof(name), "nested/page_%04d.png", index);
        zip_source_t *source = zip_source_buffer(archive, payload, sizeof(payload), 0);
        assert(source);
        assert(zip_file_add(archive, index == 0 && first_name ? first_name : name,
                            source, 0) >= 0);
    }
    assert(zip_close(archive) == 0);
}

static int try_archive_lock(void *data) {
    SDL_Mutex *mutex = data;
    bool acquired = SDL_TryLockMutex(mutex);
    if (acquired) SDL_UnlockMutex(mutex);
    return acquired;
}

static void close_archive(ArchiveHandle *handle) {
    assert(rmdir(handle->temp_dir) == 0);
    archive_close(handle);
}

static void test_zip(void) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/traversal.cbz", sandbox);
    make_archive(path, 2, "../escaped.png");
    int count = 0;
    ArchiveHandle *handle = archive_open(path, ARCHIVE_TYPE_CBZ, &count, NULL);
    assert(handle && count == 2);
    assert(rmdir(handle->temp_dir) == 0);
    char *output = NULL;
    assert(!archive_get_image(handle, 0, &output));
    int acquired = 0;
    SDL_Thread *thread = SDL_CreateThread(try_archive_lock, "lock-test", handle->archive_mutex);
    assert(thread);
    SDL_WaitThread(thread, &acquired);
    assert(acquired);
    assert(mkdir(handle->temp_dir, 0700) == 0);
    for (int index = 0; index < count; index++) {
        assert(archive_get_image(handle, index, &output));
        assert(strncmp(output, handle->temp_dir, strlen(handle->temp_dir)) == 0);
        assert(strstr(output, "..") == NULL);
        assert(access(output, F_OK) == 0);
        assert(remove(output) == 0);
        free(output);
    }
    close_archive(handle);
    assert(remove(path) == 0);

    char long_name[280];
    memset(long_name, 'a', sizeof(long_name));
    memcpy(long_name + sizeof(long_name) - 5, ".png", 5);
    snprintf(path, sizeof(path), "%s/long-name.cbz", sandbox);
    make_archive(path, 1, long_name);
    handle = archive_open(path, ARCHIVE_TYPE_CBZ, &count, NULL);
    assert(handle && archive_get_image(handle, 0, &output));
    assert(remove(output) == 0);
    free(output);
    close_archive(handle);
    assert(remove(path) == 0);

    snprintf(path, sizeof(path), "%s/limit.cbz", sandbox);
    make_archive(path, MAX_IMAGES, NULL);
    last_progress = 0;
    scan_updates = 0;
    handle = archive_open(path, ARCHIVE_TYPE_CBZ, &count, record_progress);
    assert(handle && count == MAX_IMAGES);
    assert(scan_updates == MAX_IMAGES && last_progress == 1.0f);
    close_archive(handle);
    assert(remove(path) == 0);
    make_archive(path, MAX_IMAGES + 1, NULL);
    assert(!archive_open(path, ARCHIVE_TYPE_CBZ, &count, NULL));
    assert(count == 0);
    assert(remove(path) == 0);
}

static void reset_comic(void) {
    comic_viewer_finish_loading();
    for (ImageView *view = viewer.first_view; view; view = view->next) unload_view(view);
    free_all_views();
    viewer.first_view = viewer.current_view_node = NULL;
    viewer.view_count = viewer.current_view_index = 0;
    for (int index = 0; index < viewer.image_count; index++) {
        SDL_DestroySurface(viewer.images[index].surface);
        free(viewer.images[index].path);
    }
    memset(viewer.images, 0, sizeof(viewer.images));
    viewer.image_count = 0;
    free(viewer.source_path);
    viewer.source_path = NULL;
}

static void setup_comic(void) {
    make_image(first_path);
    make_image(second_path);
    viewer.image_count = 2;
    viewer.images[0].path = strdup(first_path);
    viewer.images[1].path = strdup(second_path);
    assert(viewer.images[0].path && viewer.images[1].path);
    viewer.type = SOURCE_DIRECTORY;
    generate_default_views();
    assert(viewer.view_count == 2);
    initial_view_pending = true;
    load_view(viewer.current_view_node);
    assert(initial_view_pending && !viewer.current_view_node->texture);
    comic_viewer_finish_loading();
    assert(viewer.current_view_node->texture);
    assert(!initial_view_pending);
}

static void press_key(SDL_Keycode key, bool repeat) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.repeat = repeat;
    assert(SDL_PushEvent(&event));
    handle_events();
}

static void test_crop_geometry(void) {
    ImageView view = {0};
    SDL_Surface *surface = SDL_CreateSurface(1000, 1000, SDL_PIXELFORMAT_BGR24);
    assert(surface);
    view.texture = SDL_CreateTextureFromSurface(viewer.renderer, surface);
    SDL_DestroySurface(surface);
    assert(view.texture);
    viewer.current_view_node = &view;
    viewer.window_width = 640;
    viewer.window_height = 480;
    for (int density = 1; density <= 3; density++) {
        viewer.drawable_width = 640 * density;
        viewer.drawable_height = 480 * density;
        view.crop_rect = (SDL_FRect){100, 200, 400, 600};
        viewer.zoomed = false;
        SDL_FRect source, destination;
        assert(viewer_get_view_rects(&view, &source, &destination));
        assert(fabsf(destination.x - 160) < 0.001f && fabsf(destination.w - 320) < 0.001f);
        viewer.crop_start_x = 240;
        viewer.crop_start_y = 120;
        viewer.crop_current_x = 400;
        viewer.crop_current_y = 360;
        viewer.cropping_mode = true;
        press_key(SDLK_RETURN, false);
        assert(!viewer.cropping_mode);
        assert(fabsf(view.crop_rect.x - 200) < 0.001f && fabsf(view.crop_rect.y - 350) < 0.001f);
        assert(fabsf(view.crop_rect.w - 200) < 0.001f && fabsf(view.crop_rect.h - 300) < 0.001f);

        view.crop_rect = (SDL_FRect){100, 200, 400, 600};
        viewer.zoomed = true;
        viewer.zoom_level = 2;
        viewer.zoom_center_x = 320;
        viewer.zoom_center_y = 240;
        viewer.pan_offset_x = 40;
        viewer.pan_offset_y = 60;
        assert(viewer_get_view_rects(&view, &source, &destination));
        viewer.crop_start_x = destination.x + destination.w * 0.25f;
        viewer.crop_start_y = destination.y + destination.h * 0.25f;
        viewer.crop_current_x = destination.x + destination.w * 0.5f;
        viewer.crop_current_y = destination.y + destination.h * 0.5f;
        assert(viewer_apply_crop_selection());
        assert(fabsf(view.crop_rect.x - 200) < 0.001f && fabsf(view.crop_rect.y - 350) < 0.001f);
        assert(fabsf(view.crop_rect.w - 100) < 0.001f && fabsf(view.crop_rect.h - 150) < 0.001f);
    }
    viewer.zoomed = false;
    viewer.pan_offset_x = viewer.pan_offset_y = 0;
    viewer.current_view_node = NULL;
    SDL_DestroyTexture(view.texture);
}

static void test_hidpi_rendering(void) {
    SDL_Renderer *original_renderer = viewer.renderer;
    SDL_Surface *image = SDL_CreateSurface(1000, 1000, SDL_PIXELFORMAT_RGB24);
    assert(image && SDL_FillSurfaceRect(image, NULL, 0xffffff));
    for (int density = 1; density <= 3; density++) {
        SDL_Surface *canvas = SDL_CreateSurface(640 * density, 480 * density, SDL_PIXELFORMAT_RGB24);
        assert(canvas);
        viewer.renderer = SDL_CreateSoftwareRenderer(canvas);
        assert(viewer.renderer);
        assert(SDL_SetRenderLogicalPresentation(viewer.renderer, 640, 480,
                                                SDL_LOGICAL_PRESENTATION_LETTERBOX));
        viewer.window_width = 640;
        viewer.window_height = 480;
        viewer.drawable_width = canvas->w;
        viewer.drawable_height = canvas->h;
        ImageView view = {0};
        view.texture = SDL_CreateTextureFromSurface(viewer.renderer, image);
        assert(view.texture);
        view.crop_rect = (SDL_FRect){100, 200, 400, 600};
        viewer.current_view_node = &view;
        viewer_render_current_view();
        Uint8 *row = (Uint8 *)canvas->pixels + (canvas->h / 2) * canvas->pitch;
        assert(row[(160 * density - 1) * 3] < 128);
        assert(row[160 * density * 3] == 255);
        assert(row[(480 * density - 1) * 3] == 255);
        assert(row[480 * density * 3] < 128);
        assert(progress_bar_init(viewer.renderer));
        progress_bar_update(1.0f, NULL);
        row = (Uint8 *)canvas->pixels + (canvas->h / 2) * canvas->pitch;
        assert(row[(50 * density - 1) * 3 + 1] == 0);
        assert(row[50 * density * 3 + 1] == 150 && row[50 * density * 3 + 2] == 255);
        progress_bar_update(0.0f, NULL);
        progress_bar_update(0.6f, NULL);
        progress_bar_update(0.3f, NULL);
        progress_bar_render();
        row = (Uint8 *)canvas->pixels + (canvas->h / 2) * canvas->pitch;
        assert(row[350 * density * 3 + 1] == 150);
        assert(row[400 * density * 3 + 1] == 50);
        progress_bar_update(0.0f, NULL);
        row = (Uint8 *)canvas->pixels + (canvas->h / 2) * canvas->pitch;
        assert(row[100 * density * 3 + 1] == 50);
        progress_bar_cleanup();
        SDL_DestroyTexture(view.texture);
        SDL_DestroyRenderer(viewer.renderer);
        SDL_DestroySurface(canvas);
    }
    SDL_DestroySurface(image);
    viewer.renderer = original_renderer;
    viewer.current_view_node = NULL;
}

static void test_directory_progress(void) {
    char directory[PATH_MAX];
    snprintf(directory, sizeof(directory), "%s/progress-images", sandbox);
    assert(mkdir(directory, 0700) == 0);
    char path[PATH_MAX];
    for (int index = 0; index < 24; index++) {
        snprintf(path, sizeof(path), "%s/progress-images/%02d.bmp", sandbox, index);
        make_image(path);
    }
    ImageEntry images[24] = {0};
    int count = 0;
    last_progress = 0;
    scan_updates = 0;
    assert(load_directory(directory, images, &count, 24, record_progress));
    assert(count == 24 && scan_updates >= 24 && last_progress == 1.0f);
    for (int index = 0; index < count; index++) {
        assert(remove(images[index].path) == 0);
        free(images[index].path);
    }
    assert(rmdir(directory) == 0);
}

static void test_gamma(void) {
    SDL_Surface *surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGB24);
    assert(surface);
    assert(SDL_FillSurfaceRect(surface, NULL, 0));
    adjust_gamma_brightness_contrast(surface, 1.1, -5.0, 0.0);
    Uint8 *pixels = surface->pixels;
    assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
    assert(SDL_FillSurfaceRect(surface, NULL, 0xffffff));
    adjust_gamma_brightness_contrast(surface, 0.9, 20.0, 100.0);
    assert(pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 255);
    SDL_DestroySurface(surface);
}

static void test_monitors(void) {
    int count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    assert(displays && count > 0);
    SDL_Rect bounds;
    assert(SDL_GetDisplayBounds(displays[0], &bounds));
    int x = -1, y = -1;
    assert(select_monitor(0, &x, &y));
    assert(x == bounds.x && y == bounds.y);
    assert(SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds));
    assert(select_monitor(-1, &x, &y));
    assert(x == bounds.x && y == bounds.y);
    assert(!select_monitor(count, &x, &y));
    SDL_free(displays);
}

static void test_lifecycle(void) {
    setup_comic();
    ImageView *view = viewer.current_view_node;
    assert(view->surface != viewer.images[0].surface);
    for (int index = 0; index < 40; index++) {
        PreloadResult *result = malloc(sizeof(*result));
        assert(result);
        *result = (PreloadResult){view, SDL_GetAtomicInt(&viewer.load_generation)};
        SDL_Event event = {0};
        event.type = viewer.preload_event_type;
        event.user.data1 = result;
        assert(SDL_PushEvent(&event));
    }
    comic_viewer_finish_loading();
    assert(!SDL_HasEvent(viewer.preload_event_type));

    load_view(view->next);
    assert(viewer.load_thread);
    comic_viewer_reload_current_view();
    assert(view->next->texture);
    comic_viewer_finish_loading();
    assert(view->texture && view->surface != viewer.images[0].surface);

    for (int index = 0; index < 5; index++) {
        press_key(SDLK_E, false);
        press_key(SDLK_R, false);
        press_key(SDLK_P, false);
        press_key(SDLK_B, false);
    }
    comic_viewer_finish_loading();
    press_key(SDLK_2, false);
    comic_viewer_finish_loading();
    assert(viewer.current_view_node->count == 2 && viewer.current_view_node->surface);
    press_key(SDLK_1, false);
    comic_viewer_finish_loading();
    assert(viewer.view_count == 2 && viewer.current_view_node->count == 1);
    assert(!prepare_image(MAX_IMAGES));
    viewer.type = SOURCE_CBZ;
    load_view(viewer.current_view_node->next);
    remove_current_view();
    assert(!viewer.load_thread && viewer.view_count == 1);
    reset_comic();
}

static void test_navigation_loading(void) {
    make_image(first_path);
    viewer.image_count = 3;
    for (int index = 0; index < viewer.image_count; index++) {
        viewer.images[index].path = strdup(first_path);
        assert(viewer.images[index].path);
    }
    generate_default_views();
    ImageView *middle = viewer.first_view->next;
    view_changed(viewer.first_view, middle);
    assert(viewer.current_view_node == middle && viewer.load_thread && !middle->texture);
    preload_adjacent_view();
    assert(viewer.load_thread && !middle->texture && !middle->next->texture);
    comic_viewer_finish_loading();
    assert(middle->texture && !middle->next->texture);
    preload_adjacent_view();
    assert(viewer.load_thread && pending_preload_direction == 0);
    comic_viewer_finish_loading();
    assert(middle->next->texture);
    view_changed(middle, middle->next);
    view_changed(middle->next, middle);
    assert(pending_preload_direction == -1 && !viewer.first_view->texture);
    assert(viewer.load_thread && !middle->texture);
    preload_adjacent_view();
    assert(pending_preload_direction == -1);
    comic_viewer_finish_loading();
    assert(middle->texture);
    preload_adjacent_view();
    assert(viewer.load_thread && pending_preload_direction == 0);
    comic_viewer_finish_loading();
    assert(viewer.first_view->texture);
    reset_comic();
}

static void test_merge_and_empty_navigation(void) {
    make_image(first_path);
    viewer.image_count = 3;
    for (int index = 0; index < viewer.image_count; index++) {
        viewer.images[index].path = strdup(first_path);
        assert(viewer.images[index].path);
    }
    generate_default_views();
    viewer.current_view_node = viewer.first_view->next;
    viewer.current_view_index = 1;
    press_key(SDLK_2, false);
    comic_viewer_finish_loading();
    assert(viewer.view_count == 2 && viewer.current_view_node->count == 2);
    press_key(SDLK_HOME, false);
    comic_viewer_finish_loading();
    press_key(SDLK_2, false);
    comic_viewer_finish_loading();
    ImageView *view = viewer.current_view_node;
    assert(view == viewer.first_view && view->count == 2);
    assert(view->image_indices[0] == 0 && view->image_indices[1] == 1);
    assert(view->next && view->next->prev == view && view->next->count == 1);
    assert(view->next->image_indices[0] == 2 && !view->next->next);
    press_key(SDLK_1, false);
    comic_viewer_finish_loading();
    assert(viewer.view_count == 3);
    reset_comic();
    press_key(SDLK_1, false);
    press_key(SDLK_2, false);
    press_key(SDLK_END, false);
    press_key(SDLK_HOME, false);
    press_key(SDLK_RIGHT, false);
    press_key(SDLK_LEFT, false);
    assert(!viewer.current_view_node && viewer.view_count == 0);
}

static void test_reading_position(void) {
    setup_comic();
    viewer.source_path = strdup(first_path);
    viewer.current_view_index = 1;
    viewer.current_view_node = viewer.first_view->next;
    viewer.zoomed = true;
    viewer.max_zoom = 4;
    viewer.zoom_level = 2.5f;
    viewer.zoom_center_x = 123;
    viewer.zoom_center_y = 234;
    viewer.pan_offset_x = 17;
    viewer.pan_offset_y = -12;
    comic_viewer_save_reading_position();
    reset_comic();
    setup_comic();
    viewer.source_path = strdup(second_path);
    viewer.zoomed = false;
    viewer.zoom_level = 1;
    comic_viewer_save_reading_position();
    reset_comic();
    setup_comic();
    viewer.source_path = strdup(first_path);
    viewer_state_restore_for_source();
    assert(viewer.current_view_index == 1 && viewer.current_view_node == viewer.first_view->next);
    assert(viewer.zoomed && viewer.zoom_level == 2.5f);
    assert(viewer.zoom_center_x == 123 && viewer.zoom_center_y == 234);
    assert(viewer.pan_offset_x == 17 && viewer.pan_offset_y == -12);
    reset_comic();
    setup_comic();
    viewer.source_path = strdup(second_path);
    viewer_state_restore_for_source();
    assert(viewer.current_view_index == 0 && !viewer.zoomed && viewer.zoom_level == 1);
    reset_comic();
    char *state_path = viewer_state_file_path();
    assert(state_path && remove(state_path) == 0);
    free(state_path);
}

static void test_clipboard(void) {
    setup_comic();
    size_t size = 42;
    assert(!get_clipboard_data(NULL, NULL, &size) && size == 0);
    copy_current_view_to_clipboard();
    copy_current_view_to_clipboard();
    assert(SDL_HasClipboardData("image/png"));
    reset_comic();
    void *data = SDL_GetClipboardData("image/png", &size);
    assert(data && size > 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0);
    SDL_free(data);
    assert(SDL_ClearClipboardData());
}

static void test_delete(void) {
    setup_comic();
    press_key(SDLK_DELETE, false);
    for (int index = 0; index < 20; index++) press_key(SDLK_DELETE, true);
    assert(access(first_path, F_OK) == 0 && viewer.view_count == 2);
    press_key(SDLK_DELETE, false);
    comic_viewer_finish_loading();
    assert(access(first_path, F_OK) != 0 && access(second_path, F_OK) == 0);
    assert(viewer.view_count == 1 && viewer.image_count == 1);
    reset_comic();
}

int main(void) {
    char template[] = "/tmp/ic-safety-data-XXXXXX";
    assert(mkdtemp(template));
    SDL_strlcpy(sandbox, template, sizeof(sandbox));
    assert(setenv("XDG_DATA_HOME", sandbox, 1) == 0);
    assert(SDL_Init(SDL_INIT_VIDEO));
    assert(image_loader_init());
    viewer.preload_event_type = SDL_RegisterEvents(1);
    assert(viewer.preload_event_type != (Uint32)-1);
    viewer.window = SDL_CreateWindow("safety", 640, 480, 0);
    assert(viewer.window);
    viewer.renderer = SDL_CreateRenderer(viewer.window, NULL);
    assert(viewer.renderer);
    options = get_default_processing_options();
    assert(options);
    snprintf(first_path, sizeof(first_path), "%s/first.bmp", sandbox);
    snprintf(second_path, sizeof(second_path), "%s/second.bmp", sandbox);
    test_zip();
    test_directory_progress();
    test_monitors();
    test_gamma();
    test_crop_geometry();
    test_hidpi_rendering();
    test_lifecycle();
    test_navigation_loading();
    test_merge_and_empty_navigation();
    test_reading_position();
    test_clipboard();
    test_delete();
    setup_comic();
    load_view(viewer.current_view_node->next);
    comic_viewer_cleanup();
    assert(!viewer.load_thread && !viewer.first_view);
    free(options);
    options = NULL;
    for (int index = 0; index < viewer.image_count; index++) free(viewer.images[index].path);
    remove(first_path);
    remove(second_path);
    char temp_path[PATH_MAX];
    assert(temp_utils_get_root(temp_path, sizeof(temp_path)));
    DIR *directory = opendir(temp_path);
    assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        assert(unlinkat(dirfd(directory), entry->d_name, AT_REMOVEDIR) == 0);
    }
    closedir(directory);
    assert(rmdir(temp_path) == 0);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/mozvip/ic", sandbox); rmdir(path);
    snprintf(path, sizeof(path), "%s/mozvip", sandbox); rmdir(path);
    rmdir(sandbox);
    puts("PASS: loading, rendering, navigation, processing, clipboard, and persistence regressions");
    return 0;
}