#version 330 core
in vec2 TexCoord;
in vec4 LightColor;
in vec4 DarkColor;
out vec4 FragColor;
uniform sampler2D image;
void main() {
    vec4 texColor = texture(image, TexCoord);
    float alpha = texColor.a * LightColor.a;
    vec3 tinted = ((texColor.a - 1.0) * DarkColor.a + 1.0 - texColor.rgb)
        * DarkColor.rgb + texColor.rgb * LightColor.rgb;
    FragColor = vec4(tinted, alpha);
}
