#include "Application.h"

int main(int, char**) {
    Application app;
    app.onInit();

    app.onCompute();

    //app.onFinish();
    return 0;
}