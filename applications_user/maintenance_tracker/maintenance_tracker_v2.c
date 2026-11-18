#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAG "MaintTracker"
#define ASSET_LIMIT 24
#define ASSET_NAME_SIZE 32
#define DB_PATH APP_DATA_PATH("assets.txt")

typedef struct {
    uint32_t id;
    char name[ASSET_NAME_SIZE];
} Asset;

typedef struct {
    ViewDispatcher* dispatcher;
    Submenu* main_menu;
    TextInput* text_input;
    View* list_view;
    Storage* storage;

    Asset assets[ASSET_LIMIT];
    uint8_t asset_count;
    int8_t selected;
    char input_buf[ASSET_NAME_SIZE];
} App;

// ---------- Database ----------

static bool save_assets(App* app) {
    File* file = storage_file_alloc(app->storage);
    bool ok = false;

    if(storage_file_open(file, DB_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = true;
        for(uint8_t i = 0; i < app->asset_count; i++) {
            char line[64];
            int len = snprintf(line, sizeof(line), "%lu|%s\n",
                               (unsigned long)app->assets[i].id,
                               app->assets[i].name);
            if(len <= 0 || storage_file_write(file, line, len) != (uint16_t)len) {
                ok = false;
                break;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    return ok;
}

static void load_assets(App* app) {
    app->asset_count = 0;
    memset(app->assets, 0, sizeof(app->assets));

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, DB_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return;
    }

    char line[64];
    char chunk[32];
    uint16_t bytes;
    size_t pos = 0;

    while((bytes = storage_file_read(file, chunk, sizeof(chunk))) > 0) {
        for(uint16_t i = 0; i < bytes; i++) {
            if(chunk[i] == '\n' || chunk[i] == '\r') {
                if(pos > 0) {
                    line[pos] = '\0';

                    // Parse "id|name"
                    char* bar = strchr(line, '|');
                    if(bar && app->asset_count < ASSET_LIMIT) {
                        *bar = '\0';
                        bar++;

                        Asset* a = &app->assets[app->asset_count];
                        a->id = (uint32_t)strtoul(line, NULL, 10);
                        strncpy(a->name, bar, ASSET_NAME_SIZE - 1);
                        a->name[ASSET_NAME_SIZE - 1] = '\0';
                        app->asset_count++;
                    }
                    pos = 0;
                }
            } else if(pos < sizeof(line) - 1) {
                line[pos++] = chunk[i];
            }
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static bool add_asset(App* app, const char* name) {
    if(app->asset_count >= ASSET_LIMIT || !name || !name[0]) return false;

    Asset* a = &app->assets[app->asset_count];
    memset(a, 0, sizeof(Asset));

    uint32_t next_id = 1;
    for(uint8_t i = 0; i < app->asset_count; i++) {
        if(app->assets[i].id >= next_id) next_id = app->assets[i].id + 1;
    }

    a->id = next_id;
    strncpy(a->name, name, ASSET_NAME_SIZE - 1);
    a->name[ASSET_NAME_SIZE - 1] = '\0';

    app->asset_count++;
    if(!save_assets(app)) {
        app->asset_count--;
        return false;
    }
    return true;
}

// ---------- Views ----------

static void draw_list(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Equipment");

    if(app->asset_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, 30, "No equipment yet");
        canvas_draw_str(canvas, 4, 42, "Back = Add one");
        return;
    }

    if(app->selected < 0) app->selected = 0;
    if(app->selected >= app->asset_count) app->selected = app->asset_count - 1;

    uint8_t start = (app->selected > 0) ? app->selected - 1 : 0;

    for(uint8_t row = 0; row < 3; row++) {
        uint8_t idx = start + row;
        if(idx >= app->asset_count) break;

        uint8_t y = 22 + row * 13;
        bool sel = (idx == (uint8_t)app->selected);

        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 9, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, y, app->assets[idx].name);
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 62, "Up/Down  OK  Back");
}

static bool list_input(InputEvent* event, void* ctx) {
    App* app = ctx;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyUp) {
        if(app->selected > 0) app->selected--;
        view_commit_model(app->list_view, true);
        return true;
    }
    if(event->key == InputKeyDown) {
        if(app->selected + 1 < app->asset_count) app->selected++;
        view_commit_model(app->list_view, true);
        return true;
    }
    if(event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->dispatcher, 0); // main menu
        return true;
    }
    return false;
}

// ---------- Callbacks ----------

static void text_callback(void* ctx) {
    App* app = ctx;
    if(app->input_buf[0] == '\0') return;

    if(add_asset(app, app->input_buf)) {
        app->selected = app->asset_count - 1;
    }
    memset(app->input_buf, 0, sizeof(app->input_buf));
    view_dispatcher_switch_to_view(app->dispatcher, 0);
}

static void main_callback(void* ctx, uint32_t index) {
    App* app = ctx;

    if(index == 0) { // Show Equipment
        load_assets(app);
        app->selected = (app->asset_count > 0) ? 0 : -1;
        view_commit_model(app->list_view, true);
        view_dispatcher_switch_to_view(app->dispatcher, 1);
    }
    else if(index == 1) { // Add Equipment
        memset(app->input_buf, 0, sizeof(app->input_buf));
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Equipment name");
        text_input_set_result_callback(app->text_input, text_callback, app,
                                       app->input_buf, sizeof(app->input_buf), true);
        view_dispatcher_switch_to_view(app->dispatcher, 2);
    }
}

// ---------- App life cycle ----------

static App* app_alloc() {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->selected = -1;

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, "/ext/apps_data/maintenance_tracker");

    load_assets(app);

    Gui* gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);

    // Main menu
    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "Maint Tracker");
    submenu_add_item(app->main_menu, "Show Equipment", 0, main_callback, app);
    submenu_add_item(app->main_menu, "Add Equipment", 1, main_callback, app);
    view_dispatcher_add_view(app->dispatcher, 0, submenu_get_view(app->main_menu));

    // List view
    app->list_view = view_alloc();
    view_set_context(app->list_view, app);
    view_set_draw_callback(app->list_view, draw_list);
    view_set_input_callback(app->list_view, list_input);
    view_allocate_model(app->list_view, ViewModelTypeLockFree, 1);
    view_dispatcher_add_view(app->dispatcher, 1, app->list_view);

    // Text input
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->dispatcher, 2, text_input_get_view(app->text_input));

    furi_record_close(RECORD_GUI);
    return app;
}

static void app_free(App* app) {
    view_dispatcher_remove_view(app->dispatcher, 0);
    view_dispatcher_remove_view(app->dispatcher, 1);
    view_dispatcher_remove_view(app->dispatcher, 2);
    submenu_free(app->main_menu);
    text_input_free(app->text_input);
    view_free(app->list_view);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t maintenance_tracker_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    view_dispatcher_switch_to_view(app->dispatcher, 0);
    view_dispatcher_run(app->dispatcher);
    app_free(app);
    return 0;
}
This version is intentionally tiny and defensive.

