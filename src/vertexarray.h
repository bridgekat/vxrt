#ifndef VERTEXARRAY_H_
#define VERTEXARRAY_H_

#include <cstring>
#include <initializer_list>
#include <algorithm>
#include "common.h"
#include "opengl.h"
#include "debug.h"

class VertexFormat {
public:
	// Vertex attribute count
	unsigned int textureCount, colorCount, normalCount, coordinateCount;
	// Vertex attributes count (sum of all)
	int vertexAttributeCount;

	VertexFormat(): textureCount(0), colorCount(0), normalCount(0), coordinateCount(0), vertexAttributeCount(0) {}

	VertexFormat(unsigned int textureElementCount, unsigned int colorElementCount, unsigned int normalElementCount, unsigned int coordinateElementCount):
		textureCount(textureElementCount), colorCount(colorElementCount), normalCount(normalElementCount), coordinateCount(coordinateElementCount),
		vertexAttributeCount(textureElementCount + colorElementCount + normalElementCount + coordinateElementCount) {
		Assert(textureCount <= 3);
		Assert(colorCount <= 4);
		Assert(normalCount == 0 || normalCount == 3);
		Assert(coordinateCount <= 4 && coordinateCount >= 1);
	}
};

class VertexArray {
public:
	VertexArray(unsigned int maxVertexes, const VertexFormat& format):
		mMaxVertexes(maxVertexes), mVertexes(0), mFormat(format),
		mData(new float[mMaxVertexes * format.vertexAttributeCount]),
		mVertexAttributes(new float[format.vertexAttributeCount]) {}

	~VertexArray() {
		delete[] mData;
		delete[] mVertexAttributes;
	}

	VertexArray(const VertexArray&) = delete;
	VertexArray& operator=(const VertexArray&) = delete;

	void clear() {
		memset(mData, 0, mMaxVertexes * mFormat.vertexAttributeCount * sizeof(float));
		memset(mVertexAttributes, 0, mFormat.vertexAttributeCount * sizeof(float));
		mVertexes = 0;
	}

	// Set texture coordinates
	void setTexture(size_t size, const float* texture) {
		Assert(size <= mFormat.textureCount);
		memcpy(mVertexAttributes, texture, size * sizeof(float));
	}

	void setTexture(std::initializer_list<float> texture) {
		setTexture(texture.size(), texture.begin());
	}

	// Set color value
	void setColor(size_t size, const float* color) {
		Assert(size <= mFormat.colorCount);
		memcpy(mVertexAttributes + mFormat.textureCount, color, size * sizeof(float));
	}

	void setColor(std::initializer_list<float> color) {
		setColor(color.size(), color.begin());
	}

	// Set normal vector
	void setNormal(size_t size, const float* normal) {
		Assert(size <= mFormat.normalCount);
		memcpy(mVertexAttributes + mFormat.textureCount + mFormat.colorCount, normal, size * sizeof(float));
	}

	void setNormal(std::initializer_list<float> normal) {
		setNormal(normal.size(), normal.begin());
	}

	// Add vertex
	void addVertex(const float* coords) {
		auto cnt = mFormat.textureCount + mFormat.colorCount + mFormat.normalCount;
		Assert(mVertexes * mFormat.vertexAttributeCount + cnt + mFormat.coordinateCount <= mMaxVertexes * mFormat.vertexAttributeCount);
		memcpy(mData + mVertexes * mFormat.vertexAttributeCount, mVertexAttributes, cnt * sizeof(float));
		memcpy(mData + mVertexes * mFormat.vertexAttributeCount + cnt, coords, mFormat.coordinateCount * sizeof(float));
		mVertexes++;
	}

	void addVertex(std::initializer_list<float> coords) {
		addVertex(coords.begin());
	}

	void addPrimitive(unsigned int size, std::initializer_list<float> d) {
		memcpy(mData + mVertexes * mFormat.vertexAttributeCount, d.begin(), size * mFormat.vertexAttributeCount * sizeof(float));
		mVertexes += size;
	}

	// Get current vertex format
	const VertexFormat& format() const { return mFormat; }
	// Get current vertex data
	const float* data() const { return mData; }
	// Get current vertex count
	int vertexCount() const { return mVertexes; }

private:
	// Max vertex count
	const unsigned int mMaxVertexes;
	// Vertex count
	int mVertexes;
	// Vertex array format
	VertexFormat mFormat;
	// Vertex array
	float* mData;
	// Current vertex attributes
	float* mVertexAttributes;
};

class VertexBuffer {
public:
	VertexBuffer(): id(0), vao(0), vertexes(0) {}
	VertexBuffer(VertexBuffer&& r): id(0), vao(0), vertexes(0) { swap(r); }
	/*VertexBuffer(VertexBufferID id_, int vertexes_, const VertexFormat& format_):
		id(id_), vertexes(vertexes_), format(format_) {}*/
	explicit VertexBuffer(const VertexArray& va, bool staticDraw = true);
	~VertexBuffer() { destroy(); }

	VertexBuffer& operator=(VertexBuffer&& r) {
		swap(r);
		return *this;
	}

	// Is empty
	bool empty() const {
		if (id == 0) {
			Assert(vertexes == 0);
			return true;
		}
		return false;
	}
	// Upload new data
	void update(const VertexArray& va, bool staticDraw = true);
	// Swap
	void swap(VertexBuffer& r) {
		std::swap(id, r.id);
		std::swap(vao, r.vao);
		std::swap(vertexes, r.vertexes);
		std::swap(format, r.format);
	}
	// Render vertex buffer
	void render() const;
	// Destroy vertex buffer
	void destroy() {
		format = VertexFormat();
		if (empty()) return;
		if (!OpenGL::coreProfile()) {
			glDeleteBuffersARB(1, &id);
		} else {
			glDeleteVertexArrays(1, &vao);
			glDeleteBuffers(1, &id);
		}
		vertexes = id = vao = 0;
	}

private:
	// Buffer ID
	VertexBufferID id, vao;
	// Vertex count
	int vertexes;
	// Buffer format
	VertexFormat format;
};

#endif // !VERTEXARRAY_H_

