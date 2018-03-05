#ifndef VEC_H_
#define VEC_H_

#include <algorithm>
#include <cmath>

template <typename T>
class Vec3 {
public:
	T x, y, z;

	Vec3() : x(), y(), z() {}
	Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
	Vec3(T v) : x(v), y(v), z(v) {}

	template <typename U, std::enable_if_t<std::is_convertible<T, U>::value, int> = 0>
	Vec3(const Vec3<U>& rhs): x(T(rhs.x)), y(T(rhs.y)), z(T(rhs.z)) {}

	T lengthSqr() const { return x * x + y * y + z * z; }
	T length() const { return std::sqrt(lengthSqr()); }

	//T euclideanDistance(const Vec3& rhs) const { return (*this - rhs).length(); }
	//T chebyshevDistance(const Vec3& rhs) const { return Max(Max(Abs(x - rhs.x), Abs(y - rhs.y)), Abs(z - rhs.z)); }
	//T manhattanDistance(const Vec3& rhs) const { return Abs(x - rhs.x) + Abs(y - rhs.y) + Abs(z - rhs.z); }
	Vec3 normalize() { return (*this) / length(); }
	bool inRange(T radius, const Vec3& rhs) const {
		return rhs.x >= x - radius && rhs.x < x + radius &&
			   rhs.y >= y - radius && rhs.y < y + radius &&
			   rhs.z >= z - radius && rhs.z < z + radius;
	}

	Vec3& operator+= (const Vec3& rhs) {
		x += rhs.x;
		y += rhs.y;
		z += rhs.z;
		return *this;
	}

	Vec3& operator-= (const Vec3& rhs) {
		x -= rhs.x;
		y -= rhs.y;
		z -= rhs.z;
		return *this;
	}

	Vec3& operator*= (T value) {
		x *= value;
		y *= value;
		z *= value;
		return *this;
	}

	Vec3& operator/= (T value) {
		x /= value;
		y /= value;
		z /= value;
		return *this;
	}

	bool operator< (const Vec3& rhs) const {
		if (x != rhs.x) return x < rhs.x;
		if (y != rhs.y) return y < rhs.y;
		if (z != rhs.z) return z < rhs.z;
		return false;
	}

	Vec3 operator+ (const Vec3& r) const { return Vec3(x + r.x, y + r.y, z + r.z); }
	Vec3 operator- (const Vec3& r) const { return Vec3(x - r.x, y - r.y, z - r.z); }
	Vec3 operator* (T r) const { return Vec3(x * r, y * r, z * r); }
	Vec3 operator/ (T r) const { return Vec3(x / r, y / r, z / r); }

	friend Vec3 operator- (const Vec3& vec) { return Vec3(-vec.x, -vec.y, -vec.z); }
	bool operator== (const Vec3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
	bool operator!= (const Vec3& rhs) const { return !(rhs == *this); }

	void swap(Vec3& rhs) {
		std::swap(x, rhs.x);
		std::swap(y, rhs.y);
		std::swap(z, rhs.z);
	}

	Vec3 compMult(const Vec3& r) const { return Vec3(x * r.x, y * r.y, z * r.z); }
	Vec3 compDiv(const Vec3& r) const { return Vec3(x / r.x, y / r.y, z / r.z); }

	// For each component: begin ~ (end - 1)
	template <typename Func>
	static void range(T begin, T end, Func func) {
		Vec3 a;
		for (a.x = begin; a.x < end; a.x++)
			for (a.y = begin; a.y < end; a.y++)
				for (a.z = begin; a.z < end; a.z++)
					func(a);
	}

	// For each component: begin ~ (end - 1)
	template <typename Func>
	static void range(const Vec3& begin, const Vec3& end, Func func) {
		Vec3 a;
		for (a.x = begin.x; a.x < end.x; a.x++)
			for (a.y = begin.y; a.y < end.y; a.y++)
				for (a.z = begin.z; a.z < end.z; a.z++)
					func(a);
	}

private:
	// Solve strange problem caused by std::max & std::abs ...?
	//static T Max(T l, T r) { return l > r ? l : r; }
	//static T Abs(T x) { return x >= T(0) ? x : -x; }
};

using Vec3i = Vec3<int>;
using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;

namespace std {
	template <>
	struct hash<Vec3i> {
		size_t operator()(const Vec3i& s) const {
			// TODO: optimize 233
			size_t x = hash<int>()(s.x);
			size_t y = hash<int>()(s.y);
			size_t z = hash<int>()(s.z);
			return hash<long long>()(x * 233ull ^ y * 666ull ^ z * 19260817ull);
		}
	};
}

#endif // !VEC_H_

