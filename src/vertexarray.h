#ifndef VERTEXARRAY_H_
#define VERTEXARRAY_H_

#include <algorithm>
#include <cassert>
#include <concepts>
#include <initializer_list>
#include <utility>
#include <vector>
#include "common.h"
#include "opengl.h"

// Common vertex layouts (coords, texture coords, colors, normals).
class VertexLayout {
public:
  // Generic attribute indices (for binding in shaders).
  static constexpr size_t coordsAttribIndex = 0;
  static constexpr size_t texCoordsAttribIndex = 1;
  static constexpr size_t colorsAttribIndex = 2;
  static constexpr size_t normalsAttribIndex = 3;

  OpenGL::Primitive primitive;
  size_t numCoords;
  size_t numTexCoords;
  size_t numColors;
  size_t numNormals;

  VertexLayout(
    OpenGL::Primitive primitive,
    size_t numCoords,
    size_t numTexCoords = 0,
    size_t numColors = 0,
    size_t numNormals = 0
  ):
      primitive(primitive),
      numCoords(numCoords),
      numTexCoords(numTexCoords),
      numColors(numColors),
      numNormals(numNormals) {}

  size_t total() const { return numTexCoords + numColors + numNormals + numCoords; }
};

static_assert(std::copy_constructible<VertexLayout>);
static_assert(std::assignable_from<VertexLayout&, VertexLayout&>);

// A vertex array stores vertices on the CPU.
// Also stores the most-recently specified vertex attributes for convenience of use
// (simulates the old-style `glBegin`.)
class VertexArray {
public:
  explicit VertexArray(VertexLayout const& layout):
      mLayout(layout) {
    mAttributes.resize(mLayout.total());
  }

  VertexLayout const& layout() const { return mLayout; }
  size_t size() const { return mData.size(); }
  float const* data() const { return mData.data(); }

  VertexArray& texCoords(size_t length, float const* data) {
    assert(length == mLayout.numTexCoords);
    std::copy(data, data + length, mAttributes.data());
    return *this;
  }

  VertexArray& color(size_t length, const float* data) {
    assert(length == mLayout.numColors);
    std::copy(data, data + length, mAttributes.data() + mLayout.numTexCoords);
    return *this;
  }

  VertexArray& normal(size_t length, const float* data) {
    assert(length == mLayout.numNormals);
    std::copy(data, data + length, mAttributes.data() + mLayout.numTexCoords + mLayout.numColors);
    return *this;
  }

  VertexArray& vertex(size_t length, const float* data) {
    assert(length == mLayout.numCoords);
    std::copy(data, data + length, mAttributes.data() + mLayout.numTexCoords + mLayout.numColors + mLayout.numNormals);
    mData.insert(mData.end(), mAttributes.begin(), mAttributes.end());
    return *this;
  }

  VertexArray& vertices(size_t length, const float* data) {
    assert(length % mLayout.total() == 0);
    mData.insert(mData.end(), data, data + length);
    return *this;
  }

  VertexArray& texCoords(std::initializer_list<float> values) { return texCoords(values.size(), values.begin()); }
  VertexArray& color(std::initializer_list<float> values) { return color(values.size(), values.begin()); }
  VertexArray& normal(std::initializer_list<float> values) { return normal(values.size(), values.begin()); }
  VertexArray& vertex(std::initializer_list<float> values) { return vertex(values.size(), values.begin()); }
  VertexArray& vertices(std::initializer_list<float> values) { return vertices(values.size(), values.begin()); }

private:
  VertexLayout mLayout;
  std::vector<float> mData;
  std::vector<float> mAttributes;
};

static_assert(std::move_constructible<VertexArray>);
static_assert(std::assignable_from<VertexArray&, VertexArray&&>);
static_assert(std::copy_constructible<VertexArray>);
static_assert(std::assignable_from<VertexArray&, VertexArray&>);

// A vertex buffer stores vertices on the GPU. Can be constructed from vertex arrays.
// Partial uploading and (persistent) mapping of buffers are not implemented yet.
class VertexBuffer {
public:
  explicit VertexBuffer(VertexArray const& va, bool willReuse = false);
  ~VertexBuffer() noexcept;

  VertexBuffer(VertexBuffer&& r) noexcept:
      mLayout(r.mLayout),
      mNumVertices(r.mNumVertices),
      mVAO(std::exchange(r.mVAO, OpenGL::null)),
      mVBO(std::exchange(r.mVBO, OpenGL::null)) {}

  VertexBuffer& operator=(VertexBuffer&& r) noexcept {
    swap(*this, r);
    return *this;
  }

  friend void swap(VertexBuffer& l, VertexBuffer& r) noexcept {
    using std::swap;
    swap(l.mLayout, r.mLayout);
    swap(l.mNumVertices, r.mNumVertices);
    swap(l.mVAO, r.mVAO);
    swap(l.mVBO, r.mVBO);
  }

  void draw() const;

private:
  VertexLayout mLayout;
  size_t mNumVertices;
  OpenGL::Object mVAO = OpenGL::null;
  OpenGL::Object mVBO = OpenGL::null;
};

static_assert(std::move_constructible<VertexBuffer>);
static_assert(std::assignable_from<VertexBuffer&, VertexBuffer&&>);

#endif // VERTEXARRAY_H_
