#ifndef _SDL3_GLTF_HPP_
#define _SDL3_GLTF_HPP_

#include "sdl3-expected.hpp" // Result, fail, sdl_fail, ns::in_place
#include <SDL3/SDL.h>        // SDL_LoadFile, SDL_free
#include <tiny_gltf_v3.h>    // tg3_model, tg3_parse_auto, tg3_model_free
#include <algorithm>         // std::min, std::max, std::clamp
#include <cmath>             // std::sin, std::cos, std::acos, std::sqrt, fmod
#include <cstddef>           // std::byte
#include <cstring>           // std::memcpy
#include <filesystem>        // std::filesystem::path
#include <span>              // std::span
#include <string>            // std::string
#include <utility>           // std::move
#include <vector>            // std::vector

namespace zod
{

// A 4x4 matrix, held by column as GLSL holds one: m[0..3] is the first column,
// not the first row.  Writing it this way means the sixteen floats can be
// pushed to a shader as they stand.
struct Mat4
{
  float m[16];

  static Mat4 identity()
  {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
};

// Matrix product, in the usual order: a * b applies b first, then a
inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
  Mat4 r{};
  for (int col = 0; col < 4; ++col)
  {
    for (int row = 0; row < 4; ++row)
    {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k)
      {
        sum += a.m[k * 4 + row] * b.m[col * 4 + k];
      }
      r.m[col * 4 + row] = sum;
    }
  }
  return r;
}

// One joint's pose: where it is, which way it is turned, and how big it is.
// glTF keeps these three apart for exactly this reason - they are what an
// animation interpolates, and interpolating a finished matrix would not work.
struct Trs
{
  float t[3] = {0.0f, 0.0f, 0.0f};       // translation
  float r[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // rotation, as a quaternion xyzw
  float s[3] = {1.0f, 1.0f, 1.0f};       // scale

  // The matrix that does all three: scale first, then turn, then move
  Mat4 matrix() const
  {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    Mat4 o{};
    o.m[0]  = (1 - 2 * (y * y + z * z)) * s[0];
    o.m[1]  = (    2 * (x * y + z * w)) * s[0];
    o.m[2]  = (    2 * (x * z - y * w)) * s[0];
    o.m[4]  = (    2 * (x * y - z * w)) * s[1];
    o.m[5]  = (1 - 2 * (x * x + z * z)) * s[1];
    o.m[6]  = (    2 * (y * z + x * w)) * s[1];
    o.m[8]  = (    2 * (x * z + y * w)) * s[2];
    o.m[9]  = (    2 * (y * z - x * w)) * s[2];
    o.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    o.m[12] = t[0];
    o.m[13] = t[1];
    o.m[14] = t[2];
    o.m[15] = 1.0f;
    return o;
  }
};

// A glTF 2.0 model, parsed and ready to draw, with its animations evaluated on
// demand.
//
// This owns the parse, and nothing else.  Everything that belongs on the GPU is
// made from what it hands back, using the classes that are already here:
// vertices() and indices() go to GpuBuffer::create, images() to
// GpuTextureBank::load.  That keeps one job in one place, and makes a model as
// cheap to throw away as anything else - tinygltf keeps a whole parse in one
// arena, so the destructor is a single call.
//
// Every mesh in the file is read, and every primitive of every mesh, into one
// vertex buffer and one index buffer; parts() says which stretch of them came
// from where, and which image colours it.  The skeleton is the first skin,
// which is all a character needs - a file with two skeletons is refused rather
// than drawn wrongly.  A fuller loader would walk the scene graph for the
// transform of each node that is not skinned, and would handle the STEP and
// CUBICSPLINE interpolations as well as the LINEAR one below.
class GltfModel
{
  struct Key {};   // private tag: only GltfModel can name it

public:
  // One vertex, laid out as the vertex shader expects it.  The joint indices
  // are kept as floats rather than integers: glTF stores them as unsigned
  // shorts, but widening them once here means the shader can take an ordinary
  // vec4 and the pipeline needs no integer attribute format.
  struct Vertex
  {
    float x, y, z;   // position, in the model's own space
    float nx, ny, nz;// which way the surface faces here
    float u, v;      // where this corner sits in the texture
    float joints[4]; // which four joints move it
    float weights[4];// and how much each of them has a say
  };

  // One run of triangles that can be drawn in one go.
  //
  // A glTF holds a list of meshes, and each mesh a list of what it calls
  // primitives: some triangles, and the one material that colours them.  This
  // model is eight of them - a body, a shell, two sai and so on.  All eight go
  // into the one vertex buffer and the one index buffer that vertices() and
  // indices() hand over, and a Part is what is left of the seams: where in
  // those buffers a primitive's triangles are, and which image colours them.
  struct Part
  {
    uint32_t    first_index; // the first of this part's indices
    uint32_t    index_count; // and how many of them there are
    uint32_t    base_vertex; // which vertex this part's index 0 counts from
    std::size_t image;       // which of images() colours it
  };

  // One image the model is coloured with, as the bytes of the file it came
  // from.  A glTF may keep an image in a file beside itself, or inside its own
  // buffer, which is how a .glb carries one; either way the bytes arrive here
  // the same, and decoding them is left to whoever knows how - which is
  // GpuTextureBank, and nothing else.
  struct Image
  {
    std::string            name;  // where it came from, for the error message
    std::vector<std::byte> bytes; // and what was there
  };

  // A ball holding every vertex, measured when the model is read.
  //
  // This is what lets one camera suit any model.  A fox is eighty units long
  // and a turtle is half that tall once its skeleton has shrunk it, and
  // neither number need appear anywhere: a camera far enough back to see a
  // ball of radius r, through a field of view of angle a, sits r / sin(a / 2)
  // away from the middle of it.
  struct Sphere
  {
    float x = 0.0f, y = 0.0f, z = 0.0f; // the middle of it
    float radius = 1.0f;                // and how far the furthest vertex is
  };

private:
  // One thing an animation changes, over time: where a node is, how it is
  // turned, or how big it is.  glTF calls these channels, and gives each a
  // list of times and a list of values to go with them.
  struct Channel
  {
    // What this channel changes.  glTF allows a fourth - the weights of a
    // mesh's morph targets - which this does not read.
    enum Changes { position, rotation, scale };

    std::size_t        node;
    Changes            changes;
    std::vector<float> times;
    std::vector<float> values; // four per key for a rotation, otherwise three
  };

  // One animation: a name, the stretch of time it occupies, and everything it
  // changes while it runs.
  //
  // It is a stretch rather than a length because a file need not start each
  // animation at zero.  This model's five run one after another along a single
  // timeline - Idle from 3.3 to 6.7 seconds, Walk from 13.3 to 14.9, and so on
  // to Die, which runs from 50.0 to 52.3 - which is what a tool that exported
  // them from one FBX take leaves behind.  Playing Walk from zero would read
  // before its first key and carry the pose off to infinity.
  struct Animation
  {
    std::string          name;
    float                start = 0.0f; // the first key of any of its channels
    float                end   = 0.0f; // and the last
    std::vector<Channel> channels;
  };

  // Everything read out of the file, gathered before the object is built.
  // It is one struct so that it can be moved in with a single step, which is
  // what lets GltfModel itself stay unmovable.
  struct Data
  {
    std::vector<Vertex>    vertices;
    std::vector<uint32_t>  indices;      // which corners make each triangle
    std::vector<Part>      parts;        // and which of them belong together
    std::vector<Image>     images;       // the images parts name, in order
    Sphere                 bounds;       // where the model is, and how big
    std::vector<int32_t>   joints;       // which nodes are joints
    std::vector<Mat4>      inverse_bind; // one per joint
    std::vector<Trs>       nodes;        // every node's rest pose
    std::vector<Mat4>      fixed;        // or a matrix, for the nodes that
    std::vector<char>      has_fixed;    // gave one instead of a rest pose
    std::vector<int>       parent;       // -1 for a root
    std::vector<Animation> animations;
  };

public:
  static Result<GltfModel> create(const std::filesystem::path& gltf_path)
  {
    const std::string name = gltf_path.string();
    const std::string dir  = gltf_path.parent_path().string();

    // The file is read with SDL, not stdio: that is what the rest of this
    // project uses, and it is what works on a PS5.  The parser is handed the
    // bytes, and a callback of the same kind for the .bin file beside them.
    size_t size = 0;
    void*  data = SDL_LoadFile(name.c_str(), &size);
    if (!data)
    {
      return sdl_fail("SDL_LoadFile: " + name);
    }

    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.fs.read_file = &read_with_sdl;
    opts.fs.free_file = &free_from_sdl;
    opts.images_as_is = 1; // leave the images alone; the bank decodes them

    tg3_error_stack errors;
    tg3_error_stack_init(&errors);

    tg3_model model{};
    const tg3_error_code rc =
      tg3_parse_auto(&model, &errors, static_cast<const uint8_t*>(data), size,
                     dir.c_str(), static_cast<uint32_t>(dir.size()), &opts);
    SDL_free(data); // the parser has copied what it needs into its arena

    if (rc != TG3_OK)
    {
      const std::string why =
        (errors.count && errors.entries[0].message) ? errors.entries[0].message
                                                    : "parse failed";
      tg3_error_stack_free(&errors);
      return fail("tg3_parse_auto: " + name + ": " + why);
    }
    tg3_error_stack_free(&errors);

    // Everything is read before the object exists, because the object cannot
    // be moved: it is built in place, once, out of what is gathered here.
    Data              gathered;
    const std::string problem = read(model, gltf_path, gathered);
    if (!problem.empty())
    {
      tg3_model_free(&model); // the parse succeeded even though this did not
      return fail(problem + ": " + name);
    }

    return Result<GltfModel>(ns::in_place, Key{}, model, std::move(gathered));
  }

  GltfModel(Key, const tg3_model& model, Data&& data)
    : model_{model}, data_{std::move(data)}, posed_{data_.nodes},
      world_(data_.nodes.size()), matrices_(data_.joints.size())
  {
    // How big the model is cannot be answered by the vertices alone: they are
    // where the skeleton's bind pose left them, and a joint that scales what
    // it moves - as this model's outermost ones do - draws something of quite
    // another size.  So the skeleton is posed once, at rest, and the model is
    // measured where it is actually drawn.
    data_.bounds = measure(data_.vertices, skeleton());
  }

  GltfModel(const GltfModel&)            = delete;
  GltfModel& operator=(const GltfModel&) = delete;

  ~GltfModel() { tg3_model_free(&model_); } // the whole arena, in one call

  // The mesh, ready for GpuBuffer::create
  std::span<const Vertex> vertices() const { return data_.vertices; }

  // Which corners make each triangle, three at a time.  There is always a
  // list, even for a file that has none of its own: see read().
  std::span<const uint32_t> indices() const { return data_.indices; }

  // Which stretch of those two buffers to draw, and with which image.  There
  // is always at least one part: a file whose meshes held no triangles would
  // not have loaded.
  std::span<const Part> parts() const { return data_.parts; }

  // The images the parts name, each the bytes of a file, ready for
  // GpuTextureBank::load.  Only the images that something is coloured with are
  // here: this model carries eight, of which two are base colours and the rest
  // are normal and roughness maps that nothing here reads.
  std::span<const Image> images() const { return data_.images; }

  // Where the model is and how big it is, measured from its vertices
  const Sphere& bounds() const { return data_.bounds; }

  // The animations, in the order the file lists them
  std::size_t animation_count() const { return data_.animations.size(); }
  const std::string& animation_name(std::size_t i) const
  {
    return data_.animations[i].name;
  }
  float animation_duration(std::size_t i) const
  {
    return data_.animations[i].end - data_.animations[i].start;
  }

  // How many joints move this model.  A vertex shader has an array of a fixed
  // length for these, so a model with more joints than it expects is a
  // mismatch worth catching before anything is drawn.
  std::size_t joint_count() const { return data_.joints.size(); }

  // The pose this animation has reached at this time, one matrix per joint,
  // ready to be pushed to the shader.  Time is wrapped, so the cycle repeats.
  std::span<const Mat4> pose(std::size_t animation, float time)
  {
    // Every node starts in the pose the file gives it, and the animation
    // overwrites only what it has something to say about
    posed_ = data_.nodes;

    // Time counts from zero whoever is asking, and is folded into whatever
    // stretch of the file's timeline this animation happens to occupy.
    const Animation& a    = data_.animations[animation];
    const float      span = a.end - a.start;
    const float      t = (span > 0.0f) ? a.start + std::fmod(time, span)
                                       : a.start;
    for (const Channel& c : a.channels)
    {
      sample(c, t);
    }

    return skeleton();
  }

private:
  // The joint matrices for whatever pose the nodes are in, which is the rest
  // pose until an animation has had its say.  This is the half of pose() that
  // the constructor needs too, to see how big the model is.
  std::span<const Mat4> skeleton()
  {
    // A joint's place in the world is its parent's, and then its own - so a
    // parent has to be worked out first.  glTF requires a node to come after
    // its parent in the array, so one pass in order is enough.
    for (std::size_t n = 0; n < posed_.size(); ++n)
    {
      world_[n] = data_.has_fixed[n] ? data_.fixed[n] : posed_[n].matrix();
      if (data_.parent[n] >= 0)
      {
        world_[n] = world_[static_cast<std::size_t>(data_.parent[n])] *
                    world_[n];
      }
    }

    // The inverse bind matrix undoes the joint's rest pose, taking a vertex
    // from the model's space into that joint's; the world matrix then puts it
    // back wherever the joint has since moved to.
    for (std::size_t j = 0; j < data_.joints.size(); ++j)
    {
      matrices_[j] = world_[static_cast<std::size_t>(data_.joints[j])] *
                     data_.inverse_bind[j];
    }
    return matrices_;
  }

  // Where an accessor's data begins, and how far apart its elements are
  static const uint8_t* accessor_bytes(const tg3_model& model, int index,
                                       std::size_t& stride, std::size_t& count)
  {
    if (index < 0 || static_cast<uint32_t>(index) >= model.accessors_count)
    {
      return nullptr;
    }
    const tg3_accessor& a = model.accessors[index];
    if (a.buffer_view < 0)
    {
      return nullptr;
    }
    const tg3_buffer_view& v = model.buffer_views[a.buffer_view];
    const tg3_buffer&      b = model.buffers[v.buffer];
    const int packed =
      tg3_component_size(a.component_type) * tg3_num_components(a.type);
    stride = v.byte_stride ? v.byte_stride : static_cast<std::size_t>(packed);
    count  = static_cast<std::size_t>(a.count);
    return b.data.data + v.byte_offset + a.byte_offset;
  }

  // Read an accessor of whole numbers into a flat array.  glTF allows indices
  // to be bytes, shorts or ints, whichever is wide enough for the model; they
  // are widened to a common type here, so that nothing downstream has to ask.
  static std::vector<uint32_t> read_indices(const tg3_model& model, int index)
  {
    std::size_t           stride = 0, count = 0;
    const uint8_t*        p = accessor_bytes(model, index, stride, count);
    std::vector<uint32_t> out;
    if (!p)
    {
      return out;
    }

    const int32_t type = model.accessors[index].component_type;
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i)
    {
      switch (type)
      {
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
          out[i] = *(p + i * stride);
          break;
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
          uint16_t v = 0;
          std::memcpy(&v, p + i * stride, sizeof(v));
          out[i] = v;
          break;
        }
        default: // UNSIGNED_INT
          std::memcpy(&out[i], p + i * stride, sizeof(uint32_t));
          break;
      }
    }
    return out;
  }

  // Read an accessor of floats into a flat array, however it is strided
  static std::vector<float> read_floats(const tg3_model& model, int index,
                                        int components)
  {
    std::size_t        stride = 0, count = 0;
    const uint8_t*     p = accessor_bytes(model, index, stride, count);
    std::vector<float> out;
    if (!p)
    {
      return out;
    }
    const std::size_t n = static_cast<std::size_t>(components);
    out.resize(count * n);
    for (std::size_t i = 0; i < count; ++i)
    {
      std::memcpy(&out[i * n], p + i * stride, sizeof(float) * n);
    }
    return out;
  }

  // One primitive, appended to the vertices and indices gathered so far, with
  // a Part to say where it landed.  Returns why it could not be read, or an
  // empty string when all was well; "which" names the primitive, so that a
  // file with eight of them says which one it fell down on.
  static std::string read_primitive(const tg3_model&             model,
                                    const tg3_primitive&         prim,
                                    const std::filesystem::path& path,
                                    const std::string&           which,
                                    std::vector<int>&            taken,
                                    Data&                        out)
  {
    // A primitive may be points, lines, strips or fans as well as triangles;
    // mode is what says which, and nothing here draws any of the others.
    if (prim.mode != TG3_MODE_TRIANGLES)
    {
      return which + " is not a list of triangles";
    }

    int pos = -1, normal = -1, uv = -1, joints = -1, weights = -1;
    for (uint32_t i = 0; i < prim.attributes_count; ++i)
    {
      const tg3_str_int_pair& at = prim.attributes[i];
      const std::string       nm(at.key.data, at.key.len);
      if      (nm == "POSITION")   pos     = at.value;
      else if (nm == "NORMAL")     normal  = at.value;
      else if (nm == "TEXCOORD_0") uv      = at.value;
      else if (nm == "JOINTS_0")   joints  = at.value;
      else if (nm == "WEIGHTS_0")  weights = at.value;
    }
    if (pos < 0 || uv < 0 || joints < 0 || weights < 0)
    {
      return which + " lacks POSITION, TEXCOORD_0, JOINTS_0 or WEIGHTS_0";
    }

    const std::vector<float> p = read_floats(model, pos, 3);
    const std::vector<float> t = read_floats(model, uv, 2);
    const std::vector<float> w = read_floats(model, weights, 4);

    // NORMAL is optional, and a file that leaves it out is not unusual - the
    // Khronos fox has none - so one is worked out from the triangles further
    // down when it is missing.  A file's own normals are the better thing to
    // use where there are any: they say what the artist meant, which is not
    // always what the triangles say.
    const std::vector<float> n =
      (normal >= 0) ? read_floats(model, normal, 3) : std::vector<float>{};

    // Joint indices are unsigned shorts rather than floats, so they are read
    // on their own and widened as they go
    std::size_t    jstride = 0, jcount = 0;
    const uint8_t* jp = accessor_bytes(model, joints, jstride, jcount);
    if (!jp || jcount == 0 || p.size() != jcount * 3 ||
        t.size() != jcount * 2 || w.size() != jcount * 4)
    {
      return which + "'s attributes disagree about how many vertices it has";
    }

    // Where this primitive's own vertices and indices begin.  Its indices
    // count from its first vertex rather than from the start of the buffer:
    // the GPU is told the difference when the part is drawn, which is what
    // SDL_DrawGPUIndexedPrimitives takes a vertex offset for.
    const auto base_vertex = static_cast<uint32_t>(out.vertices.size());
    const auto first_index = static_cast<uint32_t>(out.indices.size());

    const bool given = (n.size() == jcount * 3);

    out.vertices.resize(base_vertex + jcount);
    for (std::size_t i = 0; i < jcount; ++i)
    {
      Vertex& v = out.vertices[base_vertex + i];
      v.x = p[i * 3 + 0]; v.y = p[i * 3 + 1]; v.z = p[i * 3 + 2];
      v.nx = given ? n[i * 3 + 0] : 0.0f;
      v.ny = given ? n[i * 3 + 1] : 0.0f;
      v.nz = given ? n[i * 3 + 2] : 0.0f;
      v.u = t[i * 2 + 0]; v.v = t[i * 2 + 1];
      for (std::size_t k = 0; k < 4; ++k)
      {
        uint16_t joint = 0;
        std::memcpy(&joint, jp + i * jstride + sizeof(uint16_t) * k,
                    sizeof(uint16_t));
        v.joints[k]  = static_cast<float>(joint);
        v.weights[k] = w[i * 4 + k];
      }
    }

    // A primitive may name an accessor of indices, saying which corners make
    // each triangle, or it may leave it out and mean "take them in order".
    // The second is the same as the list 0, 1, 2, 3 and so on, so that list is
    // made here and everything after this point has one way to draw.
    if (prim.indices >= 0)
    {
      const std::vector<uint32_t> list = read_indices(model, prim.indices);
      for (uint32_t i : list)
      {
        if (i >= jcount)
        {
          return which + " indexes a corner it does not have";
        }
      }
      out.indices.insert(out.indices.end(), list.begin(), list.end());
    }
    else
    {
      for (std::size_t i = 0; i < jcount; ++i)
      {
        out.indices.push_back(static_cast<uint32_t>(i));
      }
    }

    const auto index_count =
      static_cast<uint32_t>(out.indices.size()) - first_index;
    if (index_count == 0 || index_count % 3 != 0)
    {
      return which + "'s corners do not divide into triangles";
    }

    if (!given)
    {
      make_normals(out, base_vertex, first_index);
    }

    std::size_t       image   = 0;
    const std::string problem = read_image(model, prim.material, path, which,
                                           taken, image, out);
    if (!problem.empty())
    {
      return problem;
    }

    out.parts.push_back(Part{first_index, index_count, base_vertex, image});
    return {};
  }

  // Normals for a primitive that came without them, out of its triangles.
  //
  // A triangle's own normal is the cross product of two of its edges: that is
  // a vector at right angles to both, and so to the whole flat triangle.  Each
  // is added to all three of its corners and the sum normalised, so a corner
  // shared by several triangles ends up facing the average of them - which is
  // what rounds a shape off.  The cross product's length is twice the
  // triangle's area, so adding them unnormalised weights each one by how much
  // surface it speaks for, which is what you want.
  //
  // A corner that no triangle shares - and the fox's corners are all like that
  // - simply gets its own triangle's normal, which is flat shading, and right
  // for a model drawn in flat panels.
  static void make_normals(Data& out, uint32_t base_vertex,
                           uint32_t first_index)
  {
    for (std::size_t i = first_index; i + 2 < out.indices.size(); i += 3)
    {
      Vertex& a = out.vertices[base_vertex + out.indices[i + 0]];
      Vertex& b = out.vertices[base_vertex + out.indices[i + 1]];
      Vertex& c = out.vertices[base_vertex + out.indices[i + 2]];

      const float u[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
      const float v[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
      const float cross[3] = {u[1] * v[2] - u[2] * v[1],
                              u[2] * v[0] - u[0] * v[2],
                              u[0] * v[1] - u[1] * v[0]};

      for (Vertex* at : {&a, &b, &c})
      {
        at->nx += cross[0];
        at->ny += cross[1];
        at->nz += cross[2];
      }
    }

    for (std::size_t i = base_vertex; i < out.vertices.size(); ++i)
    {
      Vertex&     v      = out.vertices[i];
      const float length = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
      if (length > 0.0f)
      {
        v.nx /= length;
        v.ny /= length;
        v.nz /= length;
      }
      else
      {
        v.ny = 1.0f; // a corner no triangle uses: up will do
      }
    }
  }

  // Which of out.images colours this primitive, read the first time it is
  // asked for and remembered in taken after that.
  //
  // A glTF names it the long way round, and for good reason: the primitive
  // names a material, the material's base colour names a texture, the texture
  // names an image and a sampler.  That is three things that can be shared -
  // six of this model's eight primitives name the same material - and an image
  // read twice is an image uploaded twice.
  static std::string read_image(const tg3_model& model, int material,
                                const std::filesystem::path& path,
                                const std::string& which,
                                std::vector<int>& taken, std::size_t& index,
                                Data& out)
  {
    if (material < 0 || static_cast<uint32_t>(material) >= model.materials_count)
    {
      return which + " names no material to colour it with";
    }

    const tg3_texture_info& base =
      model.materials[material].pbr_metallic_roughness.base_color_texture;
    if (base.index < 0 ||
        static_cast<uint32_t>(base.index) >= model.textures_count)
    {
      // A base colour is the one thing every glTF material has - unless it
      // describes itself with an extension instead, as this model's .glb does
      // with the long-retired KHR_materials_pbrSpecularGlossiness.  Naming
      // what the file uses turns a flat refusal into something to go on.
      return which + "'s material has no base colour texture" +
             extensions_in(model);
    }

    const int32_t source = model.textures[base.index].source;
    if (source < 0 || static_cast<uint32_t>(source) >= model.images_count)
    {
      return which + "'s base colour texture names no image";
    }

    if (taken[source] >= 0) // already read, for an earlier primitive
    {
      index = static_cast<std::size_t>(taken[source]);
      return {};
    }

    Image             image;
    const std::string problem =
      load_image(model, model.images[source], source, path, image);
    if (!problem.empty())
    {
      return problem;
    }

    index = out.images.size();
    out.images.push_back(std::move(image));
    taken[source] = static_cast<int>(index);
    return {};
  }

  // What the file says it uses beyond plain glTF, as a clause to add to an
  // error, or nothing at all when it uses nothing
  static std::string extensions_in(const tg3_model& model)
  {
    if (model.extensions_used_count == 0)
    {
      return {};
    }

    std::string list;
    for (uint32_t e = 0; e < model.extensions_used_count; ++e)
    {
      list += (e == 0) ? ", though the file uses " : ", ";
      list.append(model.extensions_used[e].data, model.extensions_used[e].len);
    }
    return list + ", which this does not read";
  }

  // One image's bytes, however the file chose to keep them.
  //
  // glTF allows three ways, and this reads two of them: a file beside the
  // glTF, named by a URI, and a stretch of the glTF's own buffer, which is how
  // a .glb carries an image.  The third is the bytes written into the JSON
  // itself as base64 - which is what assimp produces, and what the script
  // beside the model undoes.
  //
  // Nothing is decoded here.  A JPEG is turned away because SDL cannot read
  // one, and that is worth saying plainly: the alternative is SDL_LoadPNG
  // failing later on bytes that are not a PNG, which says nothing about why.
  static std::string load_image(const tg3_model& model, const tg3_image& img,
                                int32_t number,
                                const std::filesystem::path& path, Image& out)
  {
    const std::string uri(img.uri.data ? img.uri.data : "", img.uri.len);
    const std::string mime(img.mime_type.data ? img.mime_type.data : "",
                           img.mime_type.len);
    const std::string what = "image " + std::to_string(number);
    const std::string convert =
      ": run to-gltf.sh beside the model, which converts its images";

    if (mime == "image/jpeg" || uri.ends_with(".jpg") || uri.ends_with(".jpeg"))
    {
      return what + " is a JPEG, and SDL reads only PNG and BMP" + convert;
    }

    if (!uri.empty())
    {
      if (tg3_is_data_uri(uri.data(), static_cast<uint32_t>(uri.size())))
      {
        return what + " is written into the glTF itself, as a data URI" +
               convert;
      }

      // A URI may be escaped - a space spelled %20 - which nothing here
      // undoes, so an image whose name needs escaping will not be found.
      const std::filesystem::path file = path.parent_path() / uri;
      const std::string           name = file.string();

      size_t size = 0;
      void*  data = SDL_LoadFile(name.c_str(), &size);
      if (!data)
      {
        return what + ": SDL_LoadFile: " + name + ": " + SDL_GetError();
      }

      const auto* bytes = static_cast<const std::byte*>(data);
      out.name  = name;
      out.bytes.assign(bytes, bytes + size);
      SDL_free(data);
      return {};
    }

    if (img.buffer_view >= 0 &&
        static_cast<uint32_t>(img.buffer_view) < model.buffer_views_count)
    {
      const tg3_buffer_view& view = model.buffer_views[img.buffer_view];
      const tg3_buffer&      buf  = model.buffers[view.buffer];
      const auto*            bytes =
        reinterpret_cast<const std::byte*>(buf.data.data + view.byte_offset);

      out.name = path.filename().string() + "#" + what;
      out.bytes.assign(bytes, bytes + view.byte_length);
      return {};
    }

    return what + " is nowhere in the file, and names no file of its own";
  }

  // The ball that holds every vertex, once the skeleton has had its way with
  // it: the middle of the box around them, and the distance from there to the
  // furthest one.
  //
  // The file declares a minimum and a maximum of its own for every accessor,
  // which would save some of this trouble - but they are not to be trusted.
  // assimp, which is how a .glb becomes a glTF this can read, writes a minimum
  // of zero for nearly every accessor it exports, which is only right for a
  // model that happens to touch the origin.
  static Sphere measure(const std::vector<Vertex>& vertices,
                        std::span<const Mat4>      joints)
  {
    float lo[3] = {0.0f, 0.0f, 0.0f}, hi[3] = {0.0f, 0.0f, 0.0f};
    bool  first = true;

    for (const Vertex& v : vertices)
    {
      const float at[3] = {skinned(v, joints, 0), skinned(v, joints, 1),
                           skinned(v, joints, 2)};
      for (int k = 0; k < 3; ++k)
      {
        lo[k] = first ? at[k] : std::min(lo[k], at[k]);
        hi[k] = first ? at[k] : std::max(hi[k], at[k]);
      }
      first = false;
    }

    Sphere ball;
    ball.x = (lo[0] + hi[0]) / 2.0f;
    ball.y = (lo[1] + hi[1]) / 2.0f;
    ball.z = (lo[2] + hi[2]) / 2.0f;

    // The corner of the box is further from the middle than any vertex need
    // be, so the radius is measured rather than taken from the box
    const float middle[3] = {ball.x, ball.y, ball.z};
    float       furthest  = 0.0f;
    for (const Vertex& v : vertices)
    {
      float away = 0.0f;
      for (int k = 0; k < 3; ++k)
      {
        const float d = skinned(v, joints, k) - middle[k];
        away += d * d;
      }
      furthest = std::max(furthest, away);
    }
    ball.radius = std::sqrt(furthest);
    return ball;
  }

  // One row of what the vertex shader works out for every vertex: the four
  // joints that move it, blended by weight, applied to where it started.
  static float skinned(const Vertex& v, std::span<const Mat4> joints, int row)
  {
    const float at[4] = {v.x, v.y, v.z, 1.0f};
    float       out   = 0.0f;
    for (int k = 0; k < 4; ++k)
    {
      const Mat4& j = joints[static_cast<std::size_t>(v.joints[k])];
      for (int col = 0; col < 4; ++col)
      {
        out += v.weights[k] * j.m[col * 4 + row] * at[col];
      }
    }
    return out;
  }

  // Everything create() needs after a successful parse.  Returns why it could
  // not be read, or an empty string when all was well.
  static std::string read(const tg3_model&             model,
                          const std::filesystem::path& path, Data& out)
  {
    if (model.meshes_count == 0 || model.skins_count == 0)
    {
      return "the file has no mesh, or no skin";
    }
    if (model.skins_count > 1)
    {
      // One array of joint matrices is pushed to the shader, so one skeleton
      // is what there can be.  A file with two would draw with the wrong one
      // for half of itself, which is worse than not drawing at all.
      return "the file has more than one skeleton, and this reads one";
    }

    // --- the meshes -------------------------------------------------------
    // Every primitive of every mesh is gathered into the one vertex buffer and
    // the one index buffer, with a Part left behind to say which stretch of
    // them is which.  A model of any size is then two buffers to make and two
    // to bind, and the only thing drawn separately is what is coloured
    // separately.
    std::vector<int> taken(model.images_count, -1); // glTF's image -> ours

    for (uint32_t m = 0; m < model.meshes_count; ++m)
    {
      for (uint32_t p = 0; p < model.meshes[m].primitives_count; ++p)
      {
        const std::string which = "mesh " + std::to_string(m) +
                                  ", primitive " + std::to_string(p);
        const std::string problem =
          read_primitive(model, model.meshes[m].primitives[p], path, which,
                         taken, out);
        if (!problem.empty())
        {
          return problem;
        }
      }
    }

    if (out.parts.empty())
    {
      return "the file's meshes hold no triangles";
    }

    // --- the skeleton -----------------------------------------------------
    const tg3_skin& skin = model.skins[0];
    out.joints.assign(skin.joints, skin.joints + skin.joints_count);

    const std::vector<float> ibm =
      read_floats(model, skin.inverse_bind_matrices, 16);
    if (ibm.size() != out.joints.size() * 16)
    {
      return "the skin has no inverse bind matrices";
    }
    out.inverse_bind.resize(out.joints.size());
    std::memcpy(out.inverse_bind.data(), ibm.data(),
                ibm.size() * sizeof(float));

    // Every vertex named four joints, and nothing until now has been able to
    // say whether those joints exist - the skeleton is only read here.  A
    // vertex naming one that does not would have the shader reading past the
    // end of its array, so they are all checked once, now that there is
    // something to check them against.
    for (const Vertex& v : out.vertices)
    {
      for (const float joint : v.joints)
      {
        if (joint < 0.0f ||
            static_cast<std::size_t>(joint) >= out.joints.size())
        {
          return "a vertex is moved by a joint the skeleton does not have";
        }
      }
    }

    // Every node's rest pose, and who its parent is.  glTF gives each node its
    // children, so the parents are worked out by turning that around, once.
    //
    // A node may give its place the other way, as one matrix rather than a
    // translation, a rotation and a scale, and this model's outermost node
    // does: a quarter turn, to stand a model made z-up in a format that is
    // y-up.  Such a node may not be animated - glTF says so, since there would
    // be no telling what to interpolate - so its matrix is kept to one side,
    // and used in place of the pose when the skeleton is walked.  Dropping it,
    // as an earlier version of this did, leaves every joint a quarter turn
    // away from the vertices its inverse bind matrix was made for, and the
    // model is drawn as a spray of splinters.
    out.nodes.resize(model.nodes_count);
    out.fixed.assign(model.nodes_count, Mat4::identity());
    out.has_fixed.assign(model.nodes_count, 0);
    out.parent.assign(model.nodes_count, -1);
    for (uint32_t n = 0; n < model.nodes_count; ++n)
    {
      const tg3_node& node = model.nodes[n];
      for (int k = 0; k < 3; ++k)
        out.nodes[n].t[k] = static_cast<float>(node.translation[k]);
      for (int k = 0; k < 4; ++k)
        out.nodes[n].r[k] = static_cast<float>(node.rotation[k]);
      for (int k = 0; k < 3; ++k)
        out.nodes[n].s[k] = static_cast<float>(node.scale[k]);
      if (node.has_matrix)
      {
        // Column by column, which is how glTF writes one and how Mat4 holds it
        for (int k = 0; k < 16; ++k)
          out.fixed[n].m[k] = static_cast<float>(node.matrix[k]);
        out.has_fixed[n] = 1;
      }
      for (uint32_t c = 0; c < node.children_count; ++c)
      {
        out.parent[static_cast<std::size_t>(node.children[c])] =
          static_cast<int>(n);
      }
    }

    // --- the animations ---------------------------------------------------
    for (uint32_t a = 0; a < model.animations_count; ++a)
    {
      const tg3_animation& src = model.animations[a];
      Animation            anim;
      bool                 timed = false; // has a channel said when yet?
      anim.name.assign(src.name.data, src.name.len);
      if (anim.name.empty())
      {
        anim.name = "animation " + std::to_string(a);
      }

      for (uint32_t c = 0; c < src.channels_count; ++c)
      {
        const tg3_animation_channel& ch = src.channels[c];
        const std::string what(ch.target.path.data, ch.target.path.len);

        Channel::Changes changes = Channel::position;
        if      (what == "translation") changes = Channel::position;
        else if (what == "rotation")    changes = Channel::rotation;
        else if (what == "scale")       changes = Channel::scale;
        else                            continue; // morph target weights

        if (ch.target.node < 0)
        {
          continue;
        }

        const bool turning = (changes == Channel::rotation);

        const tg3_animation_sampler& sampler = src.samplers[ch.sampler];
        Channel                      out_ch;
        out_ch.node    = static_cast<std::size_t>(ch.target.node);
        out_ch.changes = changes;
        out_ch.times   = read_floats(model, sampler.input, 1);
        out_ch.values  = read_floats(model, sampler.output, turning ? 4 : 3);
        if (out_ch.times.empty())
        {
          continue;
        }
        // What stretch of the timeline this animation occupies is measured
        // from the channels that actually change over it.  A channel with a
        // single key says only "this node stays here", and this model has
        // several of those sitting at time zero, far in front of the keys that
        // do the work - counting them would make Attack 1 run from 0 to 28.7
        // seconds, of which the first 26.7 would be a frozen pose.
        if (out_ch.times.size() > 1)
        {
          if (!timed)
          {
            anim.start = out_ch.times.front();
            anim.end   = out_ch.times.back();
            timed      = true;
          }
          else
          {
            anim.start = std::min(anim.start, out_ch.times.front());
            anim.end   = std::max(anim.end, out_ch.times.back());
          }
        }
        anim.channels.push_back(std::move(out_ch));
      }

      if (!anim.channels.empty())
      {
        out.animations.push_back(std::move(anim));
      }
    }

    if (out.animations.empty())
    {
      return "the file holds no animations this loader can use";
    }
    return {};
  }

  // Where this channel has got to at time t, written into the posed node.
  // Only LINEAR interpolation is handled, which is what this model uses; STEP
  // and CUBICSPLINE would each want a case of their own here.
  void sample(const Channel& c, float t)
  {
    const std::size_t keys = c.times.size();

    std::size_t i = 0;
    while (i + 1 < keys && c.times[i + 1] < t)
    {
      ++i;
    }
    const std::size_t j = (i + 1 < keys) ? i + 1 : i;

    // Clamped, because a channel need not span the whole of its animation:
    // one that stops early holds its last key rather than carrying on past it.
    const float span = c.times[j] - c.times[i];
    const float f =
      (span > 0.0f) ? std::clamp((t - c.times[i]) / span, 0.0f, 1.0f) : 0.0f;

    Trs& into = posed_[c.node];
    if (c.changes == Channel::rotation)
    {
      slerp(&c.values[i * 4], &c.values[j * 4], f, into.r);
      return;
    }

    // Where it is and how big it is are both three numbers, interpolated the
    // same way; all that differs is which three they are written into.
    float* out = (c.changes == Channel::position) ? into.t : into.s;
    for (std::size_t k = 0; k < 3; ++k)
    {
      out[k] = c.values[i * 3 + k] * (1.0f - f) + c.values[j * 3 + k] * f;
    }
  }

  // The shortest turn from one orientation to another.  A quaternion and its
  // negative mean the same orientation, so if the two point opposite ways one
  // is flipped first, or the joint would take the long way round.
  static void slerp(const float* a, const float* b, float f, float* out)
  {
    float dot  = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float sign = 1.0f;
    if (dot < 0.0f)
    {
      dot  = -dot;
      sign = -1.0f;
    }

    float wa = 1.0f - f, wb = f;
    if (dot < 0.9995f) // far enough apart to be worth the trigonometry
    {
      const float angle = std::acos(dot);
      const float sin_a = std::sin(angle);
      wa = std::sin((1.0f - f) * angle) / sin_a;
      wb = std::sin(f * angle) / sin_a;
    }

    float len = 0.0f;
    for (int k = 0; k < 4; ++k)
    {
      out[k] = a[k] * wa + b[k] * wb * sign;
      len += out[k] * out[k];
    }
    len = std::sqrt(len);
    if (len > 0.0f)
    {
      for (int k = 0; k < 4; ++k)
      {
        out[k] /= len;
      }
    }
  }

  // tinygltf asks for files through these, and gets them from SDL
  static int32_t read_with_sdl(uint8_t** out, uint64_t* out_size,
                               const char* path, uint32_t len, void*)
  {
    const std::string name(path, len);
    size_t            size = 0;
    void*             data = SDL_LoadFile(name.c_str(), &size);
    if (!data)
    {
      return 0;
    }
    *out      = static_cast<uint8_t*>(data);
    *out_size = size;
    return 1;
  }

  static void free_from_sdl(uint8_t* data, uint64_t, void*) { SDL_free(data); }

  tg3_model model_; // freed, arena and all, by the destructor
  Data      data_;

  std::vector<Trs>  posed_;    // this frame's pose, before the hierarchy
  std::vector<Mat4> world_;    // and after it
  std::vector<Mat4> matrices_; // what the shader is given
};

} // namespace zod

#endif // _SDL3_GLTF_HPP_
