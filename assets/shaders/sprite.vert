#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aLightColor;
layout (location = 3) in vec4 aDarkColor;
out vec2 TexCoord;
out vec4 LightColor;
out vec4 DarkColor;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
    LightColor = aLightColor;
    DarkColor = aDarkColor;
}
