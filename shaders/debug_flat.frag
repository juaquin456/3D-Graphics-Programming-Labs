#version 330 core
out vec4 FragColor;

in vec3 FragPosEye;

void main() {
    // 1. Calcular la normal de la cara dinámicamente con derivadas parciales
    vec3 dX = dFdx(FragPosEye);
    vec3 dY = dFdy(FragPosEye);
    vec3 N = normalize(cross(dX, dY));

    // 2. Luz desde la dirección de la cámara (0, 0, 1)
    float intensity = max(dot(N, vec3(0.0, 0.0, 1.0)), 0.0);

    // 3. Tinte gris/azulado con sombra difusa y luz ambiental para resaltar relieve
    vec3 baseColor = vec3(0.65, 0.70, 0.75);
    vec3 color = baseColor * (0.25 + 0.75 * intensity);

    FragColor = vec4(color, 1.0);
}
