#include "src/core/Application.h"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n"
              << " Real-Time Volumetric Ray Marching - OpenGL 4.5 Demo   \n"
              << " Mode: Cinematic High-Fidelity Volumetric Rendering    \n"
              << "========================================================" << std::endl;

    vrm::Application app(1280, 720, "Real-Time Volumetric Ray Marching (OpenGL 4.5)");

    if (!app.init()) {
        std::cerr << "[Fatal Error] Failed to initialize Volumetric Ray Marching Application." << std::endl;
        return 1;
    }

    app.run();
    return 0;
}
