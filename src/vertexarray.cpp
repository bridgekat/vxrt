#include "vertexarray.h"

VertexBuffer::~VertexBuffer() {
  if (mVAO != OpenGL::null) glDeleteVertexArrays(1, &mVAO);
  if (mVBO != OpenGL::null) glDeleteBuffers(1, &mVBO);
}

VertexBuffer::VertexBuffer(VertexArray const& va, bool willReuse):
  mLayout(va.layout()), mNumVertices(va.size() / mLayout.total()) {

  // Create and populate vertex buffers.
  GLenum usage = willReuse ? GL_STATIC_DRAW : GL_STREAM_DRAW;
  glGenBuffers(1, &mVBO);
  glBindBuffer(GL_ARRAY_BUFFER, mVBO);
  glBufferData(GL_ARRAY_BUFFER, va.size() * sizeof(float), va.data(), usage);

  // Specify vertex attribute arrays.
  glGenVertexArrays(1, &mVAO);
  glBindVertexArray(mVAO);
  size_t stride = mLayout.total() * sizeof(float);
  float const* p = nullptr;

  if (mLayout.numTexCoords > 0) {
    glVertexAttribPointer(VertexLayout::texCoordsAttribIndex, mLayout.numTexCoords, GL_FLOAT, GL_FALSE, stride, p);
    glEnableVertexAttribArray(VertexLayout::texCoordsAttribIndex);
    p += mLayout.numTexCoords;
  }
  if (mLayout.numColors > 0) {
    glVertexAttribPointer(VertexLayout::colorsAttribIndex, mLayout.numColors, GL_FLOAT, GL_FALSE, stride, p);
    glEnableVertexAttribArray(VertexLayout::colorsAttribIndex);
    p += mLayout.numColors;
  }
  if (mLayout.numNormals > 0) {
    glVertexAttribPointer(VertexLayout::normalsAttribIndex, mLayout.numNormals, GL_FLOAT, GL_FALSE, stride, p);
    glEnableVertexAttribArray(VertexLayout::normalsAttribIndex);
    p += mLayout.numNormals;
  }
  if (mLayout.numCoords > 0) {
    glVertexAttribPointer(VertexLayout::coordsAttribIndex, mLayout.numCoords, GL_FLOAT, GL_FALSE, stride, p);
    glEnableVertexAttribArray(VertexLayout::coordsAttribIndex);
    p += mLayout.numCoords;
  }
}

void VertexBuffer::draw() const {
  glBindVertexArray(mVAO);
  glDrawArrays(mLayout.primitive, 0, mNumVertices);
}
