#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include <GLFW/glfw3.h>
#include <imgui.h>
#include "common/Camera.h"
#include "common/RenderObject.h"
#include "common/Scene.h"
#include "../sampling/PointCloudBuilder.h"

struct EngineState {
    SamplingConfig samplingConfig;
    bool sceneNeedsRebuild{true};

    // Rendering settings
    bool useShadowMap{true};
    bool animateLight{false};
    bool showWireframe{false};

    // Camera view presets
    int cameraViewPreset{0}; // 0: Free Orbit, 1: Top (XY), 2: Front (XZ), 3: Side (YZ)

    Sampling::SequenceMetrics metricsSingle;
    Sampling::SequenceMetrics metricsRandom;
    Sampling::SequenceMetrics metricsHalton;
    Sampling::SequenceMetrics metricsSobol;
};

class GuiManager {
public:
    GuiManager() = default;
    ~GuiManager() = default;

    void init(GLFWwindow* window);
    void render(EngineState& state, DirectionalLight& light, Camera& cam);
    void shutdown();
};

#endif // GUI_MANAGER_H
