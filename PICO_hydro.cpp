#include "app/hydro_controller.h"

int main() {
    hydro::HydroController controller;
    controller.init();
    controller.run_forever();
}
