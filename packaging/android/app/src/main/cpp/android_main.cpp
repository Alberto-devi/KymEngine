#include <android_native_app_glue.h>
#include <android/log.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "KymEngineAndroid", __VA_ARGS__))

static void handle_cmd(struct android_app* app, int32_t cmd) {
    (void)app;
    if (cmd == APP_CMD_INIT_WINDOW) {
        LOGI("KymEngine Android NativeActivity window initialized.");
    }
}

void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;
    int events = 0;
    struct android_poll_source* source = nullptr;
    while (!app->destroyRequested) {
        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                return;
            }
        }
    }
}

