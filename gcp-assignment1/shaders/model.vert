#version 450

// https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf
// https://github.khronos.org/Vulkan-Site/glsl/latest

// The vertex shader for this assignment, and the one idea in it that the
// lectures have not covered: *skinning*.
//
// The cube in last week's lab had corners that were fixed, and the whole of it
// was moved by one matrix.  A character has to bend.  So each corner is tied
// to as many as four joints of a skeleton, with a weight saying how much each
// of them has a say, and the corner ends up at the weighted average of where
// those four joints would each have put it.  Four joints is a glTF convention,
// not a law; it is simply enough for a character and fits in one vec4.
//
// The joint matrices are worked out in the C++, in zod::GltfModel::pose, and
// arrive here ready to use.  That is unavoidable: a joint's position depends on
// its parent's, so the skeleton has to be walked in order, and a vertex shader
// sees one vertex at a time with no idea of any other.
//
// The camera is not built here, unlike the cube's shader in the lab: it
// arrives as one matrix, worked out by zod::Camera on the CPU.  A camera is the same for
// every vertex in the frame, so rebuilding it from angles in each of a hundred
// thousand invocations is work done a hundred thousand times for one answer.
//
// That matrix is sixty-four bytes, which is four whole sixteen-byte rows, so
// the array that follows it lands where std140 expects it with nothing added
// in between.
//
// That array is the skeleton, and it is not an array of matrices, which is
// what it looks like it should be.  Everything pushed as a uniform has to fit
// in four kilobytes - SDL's Vulkan backend shows a shader the first 4096 bytes
// of what was pushed and no more - and a mat4 is sixty-four of them, so a
// skeleton sent as matrices would run out at sixty-three joints.  A joint
// matrix moves, turns and scales, and its fourth row is therefore always
// (0, 0, 0, 1); sending the other three rows and putting that one back here
// costs forty-eight bytes a joint instead of sixty-four, and leaves room for
// eighty-four.  Past that, SDL's own advice is a storage buffer rather than a
// uniform.

layout(set = 1, binding = 0) uniform Transform
{
  // Where the camera is and how it sees, as one matrix.  A zod::Camera in the
  // C++ works it out - see sdl3-camera.hpp - because a camera is a thing the
  // whole program has an opinion about: it is moved by the sticks, framed on
  // the model with look_at, and the same matrix is wanted by more than one
  // shader.  Build it here instead and every shader needs its own copy of the
  // camera, and its own uniform block to feed it.
  mat4 view_projection;

  // Three rows for each of up to eighty-four joints, in the pose this frame
  // has reached.  joint() below turns three of these back into a matrix.
  vec4  joints[3 * 84];
};

layout(location = 0) in vec3 position;    // Vertex::x,y,z
layout(location = 1) in vec3 normal;      // Vertex::nx,ny,nz
layout(location = 2) in vec2 texcoord;    // Vertex::u,v
layout(location = 3) in vec4 joint_index; // Vertex::joints, as floats
layout(location = 4) in vec4 joint_weight;// Vertex::weights

layout(location = 0) smooth out vec2 uv;        // this corner in the image
layout(location = 1) smooth out vec3 world;     // and in the world
layout(location = 2) smooth out vec3 world_normal; // which way it faces

// A mat4 is built from its *columns*, so each line below is one column.

// The matrix for one joint, out of the three rows it was sent as.  Each column
// takes one number from each row, which is what makes this a transpose; the
// fourth column is where the joint moves the vertex to, and the fourth row,
// the one that is always (0, 0, 0, 1), is put back here rather than sent.
mat4 joint(int j)
{
  vec4 a = joints[j * 3 + 0]; // the matrix's first row
  vec4 b = joints[j * 3 + 1]; // its second
  vec4 c = joints[j * 3 + 2]; // and its third

  return mat4(vec4(a.x, b.x, c.x, 0.0),
              vec4(a.y, b.y, c.y, 0.0),
              vec4(a.z, b.z, c.z, 0.0),
              vec4(a.w, b.w, c.w, 1.0));
}

void main()
{
  // The skin.  Each of the four joints is asked where it would put this
  // corner, and the four answers are blended by weight.  Adding the matrices
  // first and transforming once is the same arithmetic as transforming four
  // times and adding, and is three matrix-by-vector products cheaper.
  mat4 skin = joint(int(joint_index.x)) * joint_weight.x
            + joint(int(joint_index.y)) * joint_weight.y
            + joint(int(joint_index.z)) * joint_weight.z
            + joint(int(joint_index.w)) * joint_weight.w;

  // glTF is right-handed, with +z coming towards the viewer; the projection
  // below is for a left-handed clip space, where +z goes away from it.  Going
  // from one to the other means negating z - and without that the model is
  // mirrored, which also reverses the winding of every triangle, so that
  // back-face culling throws away exactly the faces it should keep.
  mat4 handedness = mat4(vec4(1.0, 0.0,  0.0, 0.0),
                         vec4(0.0, 1.0,  0.0, 0.0),
                         vec4(0.0, 0.0, -1.0, 0.0),
                         vec4(0.0, 0.0,  0.0, 1.0));

  mat4 to_world = handedness * skin;
  vec4 in_world = to_world * vec4(position, 1.0);

  // The normal goes the same way, minus the translation - a direction has no
  // place to be moved from, which is what dropping the fourth row and column
  // amounts to.  Strictly a normal wants the inverse transpose rather than the
  // matrix itself, and the two differ as soon as a joint scales unevenly;
  // these joints all scale evenly, where they scale at all, so this is the
  // same answer for less work.  It is normalised in the fragment shader, after
  // the rasteriser has interpolated it.
  vec3 in_normal = mat3(to_world) * normal;

  // The camera, and the lens, already multiplied together in the C++.  Read
  // the line below right to left, as matrices always are: the corner is put
  // where its joints want it, flipped into a left-handed world, then seen
  // from wherever the camera happens to be.
  gl_Position  = view_projection * in_world;
  uv           = texcoord;
  world        = in_world.xyz;
  world_normal = in_normal;
}
