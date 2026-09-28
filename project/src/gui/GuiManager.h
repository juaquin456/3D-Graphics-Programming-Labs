#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include <atomic>
#include <future>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <common/Camera.h>
#include <common/RenderObject.h>
#include <common/Scene.h>

struct EngineState {
    bool useShadowMap = true;
    bool animateLight = false;
    int debugMode = 0;
    bool isHeatmapMode = false;
    PickResult lastPickResult;
    bool hasSelection = false;
};

class GuiManager {
public:
    GuiManager() = default;
    ~GuiManager() = default;

    void init(GLFWwindow* window);
    void render(EngineState& state, DirectionalLight& light, const Camera& cam);
    void shutdown();
};

#endif // GUI_MANAGER_H
