#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>

#define TAG "TallyCounter"

#define SAVE_DIR       STORAGE_APP_DATA_PATH_PREFIX
#define SAVE_PATH      APP_DATA_PATH("count.txt")
#define COUNT_MAX      999999
#define SAVE_BUF_SIZE  32

static const uint32_t step_sizes[] = {1, 5, 10, 100};
#define STEP_COUNT (sizeof(step_sizes) / sizeof(step_sizes[0]))

typedef struct {
    uint32_t count;
    uint8_t step_index;
} TallyModel;

typedef struct {
    TallyModel model;
    FuriMutex* mutex;
    FuriMessageQueue* input_queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notifications;
    Storage* storage;
} TallyApp;

static void tally_load(TallyApp* app) {
    File* file = storage_file_alloc(app->storage);
    char buf[SAVE_BUF_SIZE] = {0};

    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_read(file, buf, sizeof(buf) - 1);
        unsigned long count = 0;
        unsigned int step_index = 0;
        if(sscanf(buf, "%lu %u", &count, &step_index) == 2) {
            app->model.count = count > COUNT_MAX ? COUNT_MAX : count;
            app->model.step_index = step_index < STEP_COUNT ? step_index : 0;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
}

static void tally_save(TallyApp* app) {
    storage_simply_mkdir(app->storage, SAVE_DIR);

    File* file = storage_file_alloc(app->storage);
    char buf[SAVE_BUF_SIZE];
    int len = snprintf(
        buf,
        sizeof(buf),
        "%lu %u\n",
        (unsigned long)app->model.count,
        (unsigned int)app->model.step_index);

    if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, buf, len);
    } else {
        FURI_LOG_E(TAG, "Failed to save count");
    }

    storage_file_close(file);
    storage_file_free(file);
}

static void tally_draw_callback(Canvas* canvas, void* context) {
    TallyApp* app = context;
    char count_str[12];
    char step_str[16];

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(count_str, sizeof(count_str), "%lu", (unsigned long)app->model.count);
    snprintf(
        step_str, sizeof(step_str), "Step: %lu", (unsigned long)step_sizes[app->model.step_index]);
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Tally Counter");

    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, count_str);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignTop, step_str);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Hold OK: reset");
}

static void tally_input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* queue = context;
    furi_message_queue_put(queue, input_event, FuriWaitForever);
}

static void tally_increment(TallyModel* model) {
    uint32_t step = step_sizes[model->step_index];
    model->count = (COUNT_MAX - model->count < step) ? COUNT_MAX : model->count + step;
}

static void tally_decrement(TallyModel* model) {
    uint32_t step = step_sizes[model->step_index];
    model->count = (model->count < step) ? 0 : model->count - step;
}

static TallyApp* tally_app_alloc(void) {
    TallyApp* app = malloc(sizeof(TallyApp));
    app->model.count = 0;
    app->model.step_index = 0;

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tally_draw_callback, app);
    view_port_input_callback_set(app->view_port, tally_input_callback, app->input_queue);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

static void tally_app_free(TallyApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);
    TallyApp* app = tally_app_alloc();
    tally_load(app);
    view_port_update(app->view_port);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) != FuriStatusOk) {
            continue;
        }

        bool is_press = event.type == InputTypeShort || event.type == InputTypeRepeat;
        bool vibrate = false;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        switch(event.key) {
        case InputKeyUp:
            if(is_press) {
                tally_increment(&app->model);
                vibrate = true;
            }
            break;
        case InputKeyOk:
            // OK only counts on a short press so holding it to reset
            // doesn't keep counting via repeat events afterwards.
            if(event.type == InputTypeLong) {
                app->model.count = 0;
                notification_message(app->notifications, &sequence_double_vibro);
            } else if(event.type == InputTypeShort) {
                tally_increment(&app->model);
                vibrate = true;
            }
            break;
        case InputKeyDown:
            if(is_press) {
                tally_decrement(&app->model);
                vibrate = true;
            }
            break;
        case InputKeyRight:
            if(event.type == InputTypeShort) {
                app->model.step_index = (app->model.step_index + 1) % STEP_COUNT;
            }
            break;
        case InputKeyLeft:
            if(event.type == InputTypeShort) {
                app->model.step_index = (app->model.step_index + STEP_COUNT - 1) % STEP_COUNT;
            }
            break;
        case InputKeyBack:
            if(event.type == InputTypeShort) {
                running = false;
            }
            break;
        default:
            break;
        }
        furi_mutex_release(app->mutex);

        if(vibrate) {
            notification_message(app->notifications, &sequence_single_vibro);
        }
        view_port_update(app->view_port);
    }

    tally_save(app);
    tally_app_free(app);
    return 0;
}
