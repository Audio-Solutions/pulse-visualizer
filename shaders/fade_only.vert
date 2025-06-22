#version 120

uniform vec2 texSize;
varying vec2 uv;

void main() {
  vec2 pos = gl_Vertex.xy;
  uv = pos / texSize;
  vec2 clip = uv * 2.0 - 1.0;
  gl_Position = vec4(clip, 0.0, 1.0);
} 