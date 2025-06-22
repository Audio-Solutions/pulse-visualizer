#version 120

// Uniforms
uniform sampler2D texPrev;     // Previous frame texture
uniform float decay;           // Decay rate for persistence
uniform vec3 bgColor;          // Background color
uniform vec2 texSize;          // Texture size (unused in this shader)

// Varyings
varying vec2 uv;               // Texture coordinates from vertex shader

void main() {
  // Sample current pixel
  vec3 center = texture2D(texPrev, uv).rgb;
  
  // Apply temporal decay towards background color
  vec3 decayed = mix(bgColor * 0.8, center, decay);
  
  // Calculate final brightness and apply threshold
  const vec3 luminanceWeights = vec3(0.299, 0.587, 0.114);
  float brightness = dot(decayed, luminanceWeights);
  float bgBrightness = dot(bgColor, luminanceWeights);
  
  // If pixel is close to background brightness, snap to background
  // This prevents persistent dim artifacts
  gl_FragColor = (brightness <= bgBrightness) 
    ? vec4(bgColor, 1.0) 
    : vec4(decayed, 1.0);
} 