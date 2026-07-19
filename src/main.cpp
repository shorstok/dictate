#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <curl/curl.h>

#include "app.hpp"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd) {
    curl_global_init(CURL_GLOBAL_ALL);

    App app;
    int result = app.run(instance, show_cmd);

    curl_global_cleanup();
    return result;
}
