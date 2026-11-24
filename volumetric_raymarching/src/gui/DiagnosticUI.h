#pragma once

#include <imgui.h>
#include "../scenes/IScene.h"
#include "../core/Camera.h"

namespace vrm {

class DiagnosticUI {
public:
    DiagnosticUI();

    void render(SceneSettings& settings, Camera& camera, float fps, float frameTimeMs);

private:
    void renderSceneSelector(SceneSettings& settings, Camera& camera);
    void renderLightControls(SceneSettings& settings);
    void renderOpticalParameters(SceneSettings& settings);
    void renderRayMarchingOptimizations(SceneSettings& settings);
    void renderPlaybackControls(SceneSettings& settings);
    void renderDebugModes(SceneSettings& settings);
    void renderPerformanceMetrics(float fps, float frameTimeMs, const SceneSettings& settings);
};

} // namespace vrm
