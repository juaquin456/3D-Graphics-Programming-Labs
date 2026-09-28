#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform bool u_IsHeatmapMode = false;

vec3 ColorMapHeatmap(float val) {
    val = clamp(val, 0.0, 1.0);
    return vec3(
        smoothstep(0.5, 0.8, val),
        1.0 - abs(val - 0.5) * 2.0,
        1.0 - smoothstep(0.2, 0.5, val)
    );
}

void main() {
    if (u_IsHeatmapMode) {
        vec3 heatColor = ColorMapHeatmap(TexCoords.x);
        FragColor = vec4(heatColor, 1.0);
    } else {
        FragColor = vec4(TexCoords.x, TexCoords.y, 0.0, 1.0);
    }
}
