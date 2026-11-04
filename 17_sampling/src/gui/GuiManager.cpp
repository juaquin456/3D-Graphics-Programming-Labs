#include "GuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <string>

void GuiManager::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // Style adjustments
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void GuiManager::render(EngineState& state, DirectionalLight& light, Camera& cam) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    SamplingConfig& cfg = state.samplingConfig;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480, 700), ImGuiCond_FirstUseEver);

    ImGui::Begin("Lab 17: Halton, Sobol & Random Sequences", nullptr);

    if (ImGui::CollapsingHeader("1. Sequence Generator Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* genModes[] = {
            "0: Pseudo-Random (std::mt19937)",
            "1: Halton Sequence (Quasi-Random)",
            "2: Sobol Sequence (Dyadic Net)",
            "3: Side-by-Side Comparison (All 3)"
        };
        int currentGen = static_cast<int>(cfg.generatorType);
        if (ImGui::Combo("Generator", &currentGen, genModes, IM_ARRAYSIZE(genModes))) {
            cfg.generatorType = static_cast<GeneratorType>(currentGen);
            state.sceneNeedsRebuild = true;
        }

        ImGui::Separator();
        ImGui::Text("Sample Count (N): %d", cfg.sampleCount);

        bool isPow2 = (cfg.sampleCount > 0) && ((cfg.sampleCount & (cfg.sampleCount - 1)) == 0);
        if (cfg.generatorType == GeneratorType::Sobol || cfg.generatorType == GeneratorType::SideBySide) {
            if (isPow2) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[Power of 2: Ideal Dyadic Net]");
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[Non-Pow2: Partial Stratification]");
            }
        }

        ImGui::Text("Presets (2^m):");
        const int pow2Presets[] = {64, 128, 256, 512, 1024, 2048, 4096};
        for (int p : pow2Presets) {
            std::string btnLabel = std::to_string(p);
            if (p == cfg.sampleCount) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.25f, 0.3f, 1.0f));
            }
            if (ImGui::Button(btnLabel.c_str())) {
                cfg.sampleCount = p;
                state.sceneNeedsRebuild = true;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        ImGui::NewLine();

        if (ImGui::SliderInt("Adjust N", &cfg.sampleCount, 16, 4096)) {
            state.sceneNeedsRebuild = true;
        }

        ImGui::Separator();

        if (cfg.generatorType == GeneratorType::Halton || cfg.generatorType == GeneratorType::SideBySide) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "--- Halton Sequence Options ---");

            const char* primePresets[] = {
                "Preset: (2, 3, 5) - Standard Low Discrepancy",
                "Preset: (2, 3, 7)",
                "Preset: (7, 11, 13) - Intermediate Primes",
                "Preset: (11, 13, 17) - High Primes (Observe Striping!)"
            };

            if (ImGui::Combo("Prime Preset", &cfg.haltonPreset, primePresets, IM_ARRAYSIZE(primePresets))) {
                if (cfg.haltonPreset == 0) { cfg.haltonBaseX = 2;  cfg.haltonBaseY = 3;  cfg.haltonBaseZ = 5; }
                if (cfg.haltonPreset == 1) { cfg.haltonBaseX = 2;  cfg.haltonBaseY = 3;  cfg.haltonBaseZ = 7; }
                if (cfg.haltonPreset == 2) { cfg.haltonBaseX = 7;  cfg.haltonBaseY = 11; cfg.haltonBaseZ = 13; }
                if (cfg.haltonPreset == 3) { cfg.haltonBaseX = 11; cfg.haltonBaseY = 13; cfg.haltonBaseZ = 17; }
                state.sceneNeedsRebuild = true;
            }

            ImGui::Text("Current Bases: (%d, %d, %d)", cfg.haltonBaseX, cfg.haltonBaseY, cfg.haltonBaseZ);

            if (ImGui::Checkbox("Scramble Halton (Faure Permutation)", &cfg.haltonScramble)) {
                state.sceneNeedsRebuild = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Digit scrambling permutes digits to shatter linear correlation stripes!");
            }
        }


        if (cfg.generatorType == GeneratorType::Sobol || cfg.generatorType == GeneratorType::SideBySide) {
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "--- Sobol Sequence Options ---");
            if (ImGui::Checkbox("Digital Shift (Cranley-Patterson / XOR Shift)", &cfg.sobolDigitalShift)) {
                state.sceneNeedsRebuild = true;
            }
            if (cfg.sobolDigitalShift) {
                int seed = static_cast<int>(cfg.sobolSeed);
                if (ImGui::SliderInt("Shift Seed", &seed, 1, 9999)) {
                    cfg.sobolSeed = static_cast<uint32_t>(seed);
                    state.sceneNeedsRebuild = true;
                }
            }
        }


        if (cfg.generatorType == GeneratorType::PseudoRandom || cfg.generatorType == GeneratorType::SideBySide) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "--- Pseudo-Random Options ---");
            int rSeed = static_cast<int>(cfg.randomSeed);
            if (ImGui::SliderInt("RNG Seed", &rSeed, 1, 9999)) {
                cfg.randomSeed = static_cast<uint32_t>(rSeed);
                state.sceneNeedsRebuild = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reseed")) {
                cfg.randomSeed = static_cast<uint32_t>(rand() % 10000 + 1);
                state.sceneNeedsRebuild = true;
            }
        }
    }


    if (ImGui::CollapsingHeader("2. Miniball Visualization & Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* colorModes[] = {
            "Uniform Theme Color",
            "3D Coordinates (RGB = XYZ)",
            "Sample Sequence Order (Rainbow/Turbo)",
            "Nearest-Neighbor Density Heatmap (Blue=Sparse, Red=Clumped)"
        };
        int currentCol = static_cast<int>(cfg.colorMode);
        if (ImGui::Combo("Ball Coloring", &currentCol, colorModes, IM_ARRAYSIZE(colorModes))) {
            cfg.colorMode = static_cast<ColorMode>(currentCol);
            state.sceneNeedsRebuild = true;
        }

        if (ImGui::SliderFloat("Ball Radius", &cfg.ballRadius, 0.005f, 0.045f, "%.3f")) {
            state.sceneNeedsRebuild = true;
        }

        const char* sphereQualities[] = { "Low (6x4)", "Medium (8x6)", "High (12x8)" };
        int qualityIdx = (cfg.sphereSlices == 6) ? 0 : (cfg.sphereSlices == 8 ? 1 : 2);
        if (ImGui::Combo("Ball Tessellation", &qualityIdx, sphereQualities, IM_ARRAYSIZE(sphereQualities))) {
            if (qualityIdx == 0) { cfg.sphereSlices = 6;  cfg.sphereStacks = 4; }
            if (qualityIdx == 1) { cfg.sphereSlices = 8;  cfg.sphereStacks = 6; }
            if (qualityIdx == 2) { cfg.sphereSlices = 12; cfg.sphereStacks = 8; }
            state.sceneNeedsRebuild = true;
        }

        if (ImGui::Checkbox("Show 1x1x1 Bounding Box", &cfg.showBoundingBox)) {
            state.sceneNeedsRebuild = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Center at Origin", &cfg.centerAtOrigin)) {
            state.sceneNeedsRebuild = true;
        }

        if (ImGui::Checkbox("Flatten Points to 2D (Z=0.5)", &cfg.flattenZ)) {
            state.sceneNeedsRebuild = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Projects all 3D points onto a 2D plane to clearly reveal correlation lines!");
        }

        ImGui::Separator();
        ImGui::Text("Camera Presets (Orthographic Projection Look):");

        if (ImGui::Button("3D Perspective (Orbit)")) {
            cam.position = glm::vec3(0.0f, 1.2f, 2.5f);
            cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Top View (XY)")) {
            cam.position = glm::vec3(0.0f, 0.0f, 2.8f);
            cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Front View (XZ)")) {
            cam.position = glm::vec3(0.0f, 2.8f, 0.001f);
            cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Side (YZ)")) {
            cam.position = glm::vec3(2.8f, 0.0f, 0.0f);
            cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
        }
    }


    if (ImGui::CollapsingHeader("3. Uniformity & Discrepancy Metrics", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (cfg.generatorType == GeneratorType::SideBySide) {
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Comparative Metrics (N = %d):", cfg.sampleCount);

            if (ImGui::BeginTable("MetricsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Metric");
                ImGui::TableSetupColumn("std::random");
                ImGui::TableSetupColumn("Halton");
                ImGui::TableSetupColumn("Sobol (3D)");
                ImGui::TableHeadersRow();

                // Row 1: Min Distance
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Min Distance (d_min)");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%.4f", state.metricsRandom.minDist);
                ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%.4f", state.metricsHalton.minDist);
                ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%.4f", state.metricsSobol.minDist);

                // Row 2: Mean NN Distance
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Mean NN Spacing");
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.4f", state.metricsRandom.meanNearestDist);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.4f", state.metricsHalton.meanNearestDist);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", state.metricsSobol.meanNearestDist);

                // Row 3: Std NN Distance (Uniformity)
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Spacing StdDev (Lower=Better)");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%.4f", state.metricsRandom.stdNearestDist);
                ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%.4f", state.metricsHalton.stdNearestDist);
                ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%.4f", state.metricsSobol.stdNearestDist);

                // Row 4: Chi-Square
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Chi^2 (Grid Uniformity)");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%.2f", state.metricsRandom.chiSquare);
                ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%.2f", state.metricsHalton.chiSquare);
                ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "%.2f", state.metricsSobol.chiSquare);

                // Row 5: Empty Voxel Cells
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Empty Voxels (4x4x4 Voids)");
                ImGui::TableSetColumnIndex(1); ImGui::Text("%d / 64", state.metricsRandom.emptyVoxels);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%d / 64", state.metricsHalton.emptyVoxels);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d / 64", state.metricsSobol.emptyVoxels);

                ImGui::EndTable();
            }
        } else {
            const auto& m = state.metricsSingle;
            ImGui::BulletText("Minimum Pairwise Distance: %.5f", m.minDist);
            ImGui::BulletText("Average Nearest-Neighbor Distance: %.5f", m.meanNearestDist);
            ImGui::BulletText("Spacing Standard Deviation: %.5f", m.stdNearestDist);
            ImGui::BulletText("Chi-Square Voxel Uniformity: %.2f", m.chiSquare);
            ImGui::BulletText("Empty Voxel Voids: %d of %d", m.emptyVoxels, m.totalVoxels);
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Key Mathematical Insights:\n"
                           "- std::random exhibits Poisson clumping: min distance drops close to 0, producing tight clusters and large empty voids.\n"
                           "- Halton and Sobol achieve blue-noise-like repulsion: points stay well-separated with larger min distance and tight spacing variance.\n"
                           "- When N is a power of 2, Sobol has Chi^2 = 0.00 across 4x4x4 dyadic elementary intervals (perfect dyadic stratification).");
    }

    // =========================================================================
    // Header 4: Theory, Mathematical Formulations & Unwanted Patterns
    // =========================================================================
    if (ImGui::CollapsingHeader("4. Theory & Unwanted Patterns")) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Halton Sequence Formulation:");
        ImGui::TextWrapped("Every integer i in prime base b has digit expansion i = sum a_j(i) b^j.\n"
                           "Radical inverse: Phi_b(i) = sum a_j(i) b^{-(j+1)}.\n"
                           "3D point: P_i = (Phi_p1(i), Phi_p2(i), Phi_p3(i)).");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Halton Unwanted Patterns:");
        ImGui::TextWrapped("When using higher primes (e.g. 11, 13, 17) or large N, strong linear correlation lines and stripes appear in 2D projections because digit cycles are long and unpermuted.\n"
                           "Solution: Digit scrambling (Faure permutation) shuffles digits to shatter correlation lines.");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Sobol Sequence Formulation:");
        ImGui::TextWrapped("Base-2 (t, s)-sequence using primitive polynomials over GF(2) and direction numbers.\n"
                           "Uses Antonov-Saleev Gray code g(i) = i ^ (i >> 1) for O(1) bitwise XOR generation:\n"
                           "X(i) = XOR_j [ g(i)_j * V_j ].");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Sobol Unwanted Patterns:");
        ImGui::TextWrapped("- Non-power-of-2 sample counts: Dyadic elementary intervals are partially filled, breaking stratification balance.\n"
                           "- Deterministic origin alignment: i=0 is pinned to (0,0,0).\n"
                           "Solution: Digital Cranley-Patterson shift randomizes origin while strictly preserving (t,s) net properties.");
    }


    if (ImGui::CollapsingHeader("5. Performance & Engine Controls")) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        int totalBallCount = (state.samplingConfig.generatorType == GeneratorType::SideBySide) ? 3 * cfg.sampleCount : cfg.sampleCount;
        ImGui::Text("Miniball Render Objects: %d", totalBallCount);

        if (ImGui::Checkbox("Show Wireframe", &state.showWireframe)) {
            glPolygonMode(GL_FRONT_AND_BACK, state.showWireframe ? GL_LINE : GL_FILL);
        }

        ImGui::Checkbox("Shadow Mapping", &state.useShadowMap);
        ImGui::Checkbox("Animate Light", &state.animateLight);
        ImGui::DragFloat3("Light Position", glm::value_ptr(light.position), 0.1f);
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GuiManager::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
