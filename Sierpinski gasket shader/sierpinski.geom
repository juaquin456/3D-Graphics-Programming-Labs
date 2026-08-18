#version 450

layout (triangles) in;
layout (triangle_strip) out;

void generate_triangle(vec4 v1, vec4 v2, vec4 v3, int level) {
    if (level > 2) {
        gl_Position = v1;
        EmitVertex();
        gl_Position = v2;
        EmitVertex();
        gl_Position = v3;
        EmitVertex();
        EndPrimitive();
        return;
    }

    vec4 m1 = (v1 + v2) / 2.;

    vec4 m2 = (v2 + v3) / 2.;

    vec4 m3 = (v3 + v1) / 2.;


    generate_triangle(v1, m1, m3, level + 1);
    generate_triangle(m1, v2, m2, level + 1);
    generate_triangle(m3, m2, v3, level + 1);
}

void main(){
    generate_triangle(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_in[2].gl_Position, 0);
}