#version 450

// https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf
// https://github.khronos.org/Vulkan-Site/glsl/latest

// The fragment shader for this assignment: the model's own image, with a light
// on it.  It runs once for every pixel the model covers.
//
// Two things are multiplied together here.  The *colour* comes from the
// model's own image, sampled at this pixel's place in it.  The *light* is an
// ambient term plus a point light, which is dimmer the further away it is and
// the more steeply it strikes.  Multiply the two and the result is a surface
// that is still its own colour, but is brighter where the light reaches it.
//
// There is no specular term - no shine.  On a character painted with an image,
// one shine for the whole surface is what makes a model look like plastic; a
// game says where a surface is shiny with a second image, a roughness map,
// which this model carries and nothing here reads.

layout(set = 2, binding = 0) uniform sampler2D base_colour;

// How many lights this shader is given.  Both sides have to agree: C++ pushes
// exactly this many, and reads nothing back, so a shader expecting more than
// it is sent reads whatever happens to follow in memory.  See light_count in
// src/game.cpp, and change the two together.
const int light_count = 1;

// zod::PointLight, as the shader sees it: the same bytes the C++ holds, which
// is what lets Game::render push the array as it stands.  A vec3 starts a
// fresh sixteen-byte row in a uniform block, so `intensity` rides in the gap
// after the position rather than being padding, and each light is two rows.
struct PointLight
{
  vec3  position;  // where the bulb hangs, in world space
  float intensity; // how bright it is at one unit away; the C++ works it out
  vec3  colour;    // what colour it shines
  float unused;    // where the C++ keeps its enabled_ flag; nothing reads it
};

layout(set = 3, binding = 0) uniform Lights
{
  PointLight lights[light_count];
};

layout(location = 0) smooth in vec2 uv;     // where this pixel is in the image
layout(location = 1) smooth in vec3 world;  // and in the world
layout(location = 2) smooth in vec3 normal; // which way its surface faces

layout(location = 0) out vec4 fragColour;

// How much light reaches a surface no light can see at all.  More than nature
// would allow, so that a face turned away stays visible rather than going
// black.
const float ambient = 0.35;

void main()
{
  // The rasteriser interpolates a normal between corners, and the result of
  // mixing two vectors of length 1 is shorter than 1, so it is normalised here
  // rather than in the vertex shader.  This is what makes the shading smooth
  // across a face rather than flat per corner.
  vec3 surface = normalize(normal);

  vec4 base = texture(base_colour, uv);

  // The paint, under the light that arrives from nowhere in particular
  vec3 lit = base.rgb * ambient;

  for (int i = 0; i < light_count; ++i)
  {
    // Not a direction yet: this vector has the distance in it as well, and
    // both are wanted - one for the cosine, the other for the falloff.
    vec3  towards  = lights[i].position - world;
    float distance = length(towards);
    vec3  to_light = towards / distance; // now a direction, of length 1

    // Lambert's cosine law: a surface square on to the light catches all of
    // what falls on it, one edge on catches none.  The max() is what stops a
    // surface turned away from the light being *darkened* by it.
    float lambert = max(dot(surface, to_light), 0.0);

    // ...and the inverse square law: light spreading out over a sphere of
    // radius d is spread over an area that grows with d squared.
    float fall_off = lights[i].intensity / (distance * distance);

    // (1 - ambient) is the headroom the ambient term left, so one light
    // arriving square on at its working distance just reaches full brightness.
    lit += base.rgb * lights[i].colour * (1.0 - ambient) * lambert * fall_off;
  }

  // Per channel, because a screen can only be so bright: lights add up, and
  // without this the sum would clip oddly rather than simply reaching white.
  fragColour = vec4(min(lit, vec3(1.0)), 1.0); // opaque
}
