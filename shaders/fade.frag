#version 120

// Uniforms
uniform sampler2D texPrev;     // Previous frame texture
uniform float decay;           // Decay rate for persistence
uniform vec3 bgColor;          // Background color
uniform vec3 targetRGB;        // Target color (unused in current implementation)
uniform vec2 texSize;          // Texture size for texel calculations

// Varyings
varying vec2 uv;               // Texture coordinates from vertex shader

void main() {
  // Calculate texel size for neighboring pixel sampling
  vec2 texel = 1.0 / texSize;
  
  // Sample current pixel and its 4-connected neighbors
  vec3 center = texture2D(texPrev, uv).rgb;
  vec3 left   = texture2D(texPrev, uv + vec2(-texel.x, 0.0)).rgb;
  vec3 right  = texture2D(texPrev, uv + vec2( texel.x, 0.0)).rgb;
  vec3 up     = texture2D(texPrev, uv + vec2(0.0,  texel.y)).rgb;
  vec3 down   = texture2D(texPrev, uv + vec2(0.0, -texel.y)).rgb;
  
  // Calculate brightness using standard luminance weights (NTSC)
  const vec3 luminanceWeights = vec3(0.299, 0.587, 0.114);
  float centerBright = dot(center, luminanceWeights);
  float leftBright   = dot(left,   luminanceWeights);
  float rightBright  = dot(right,  luminanceWeights);
  float upBright     = dot(up,     luminanceWeights);
  float downBright   = dot(down,   luminanceWeights);
  
  // Simulate phosphor light dispersion/bloom effect
  const float dispersionRate = 0.15;
  vec3 dispersed = center;
  
  // Add neighboring brightness that exceeds center brightness
  if (leftBright > centerBright) 
    dispersed += left * (leftBright - centerBright) * dispersionRate;
  if (rightBright > centerBright) 
    dispersed += right * (rightBright - centerBright) * dispersionRate;
  if (upBright > centerBright) 
    dispersed += up * (upBright - centerBright) * dispersionRate;
  if (downBright > centerBright) 
    dispersed += down * (downBright - centerBright) * dispersionRate;
  
  // Clamp to prevent excessive bloom
  dispersed = clamp(dispersed, vec3(0.0), vec3(2.0));
  
  // Apply temporal decay towards background color
  vec3 decayed = mix(bgColor * 0.8, dispersed, decay);
  
  // Calculate final brightness and apply threshold
  float brightness = dot(decayed, luminanceWeights);
  float bgBrightness = dot(bgColor, luminanceWeights);
  
  // If pixel is close to background brightness, snap to background
  // This prevents persistent dim artifacts
  gl_FragColor = (brightness <= bgBrightness * 1.1) 
    ? vec4(bgColor, 1.0) 
    : vec4(decayed, 1.0);
} 