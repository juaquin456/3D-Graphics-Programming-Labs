#version 330 core

layout (triangles) in;
layout (triangle_strip, max_vertices = 243) out;

struct Triangle {
    vec4 v1;
    vec4 v2;
    vec4 v3;
};

void main() {
    const int TARGET_LEVEL = 4;

    Triangle queue[81];

    queue[0] = Triangle(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_in[2].gl_Position);
    int count = 1;

    for (int level = 0; level < TARGET_LEVEL; ++level) {
        int current_count = count;
        count = 0;

        Triangle next_queue[81];

        for (int i = 0; i < current_count; ++i) {
            vec4 v1 = queue[i].v1;
            vec4 v2 = queue[i].v2;
            vec4 v3 = queue[i].v3;

            vec4 m1 = (v1 + v2) * 0.5;
            vec4 m2 = (v2 + v3) * 0.5;
            vec4 m3 = (v3 + v1) * 0.5;

            next_queue[count++] = Triangle(v1, m1, m3);
            next_queue[count++] = Triangle(m1, v2, m2);
            next_queue[count++] = Triangle(m3, m2, v3);
        }

        for (int i = 0; i < count; ++i) {
            queue[i] = next_queue[i];
        }
    }

    for (int i = 0; i < count; ++i) {
        gl_Position = queue[i].v1;
        EmitVertex();
        gl_Position = queue[i].v2;
        EmitVertex();
        gl_Position = queue[i].v3;
        EmitVertex();
        EndPrimitive();
    }
}