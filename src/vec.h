#ifndef VEC_H_
#define VEC_H_

#include <algorithm>
#include <cmath>
#include <concepts>
#include <variant>

// TODO: arrange
template <typename T>
class Vec3 {
public:
  T x, y, z;

  Vec3():
    x(0),
    y(0),
    z(0) {}
  Vec3(T x, T y, T z):
    x(x),
    y(y),
    z(z) {}
  Vec3(T value):
    x(value),
    y(value),
    z(value) {}

  template <typename U, std::enable_if_t<std::is_convertible<T, U>::value, std::monostate> = std::monostate()>
  Vec3(Vec3<U> const& r):
    x(T(r.x)),
    y(T(r.y)),
    z(T(r.z)) {}

  friend void swap(Vec3& l, Vec3& r) {
    std::swap(l.x, r.x);
    std::swap(l.y, r.y);
    std::swap(l.z, r.z);
  }

  T lengthSqr() const { return x * x + y * y + z * z; }
  T length() const { return std::sqrt(lengthSqr()); }
  Vec3 normalize() { return *this / length(); }

  bool inRange(T radius, Vec3 const& r) const {
    return r.x >= x - radius && r.x < x + radius && r.y >= y - radius && r.y < y + radius && r.z >= z - radius
        && r.z < z + radius;
  }

  Vec3& operator+=(Vec3 const& r) {
    x += r.x;
    y += r.y;
    z += r.z;
    return *this;
  }

  Vec3& operator-=(Vec3 const& r) {
    x -= r.x;
    y -= r.y;
    z -= r.z;
    return *this;
  }

  Vec3& operator*=(T value) {
    x *= value;
    y *= value;
    z *= value;
    return *this;
  }

  Vec3& operator/=(T value) {
    x /= value;
    y /= value;
    z /= value;
    return *this;
  }

  bool operator<(Vec3 const& r) const {
    if (x != r.x) return x < r.x;
    if (y != r.y) return y < r.y;
    if (z != r.z) return z < r.z;
    return false;
  }

  Vec3 operator+(Vec3 const& r) const { return Vec3(x + r.x, y + r.y, z + r.z); }
  Vec3 operator-(Vec3 const& r) const { return Vec3(x - r.x, y - r.y, z - r.z); }
  Vec3 operator*(T r) const { return Vec3(x * r, y * r, z * r); }
  Vec3 operator/(T r) const { return Vec3(x / r, y / r, z / r); }

  friend Vec3 operator-(Vec3 const& vec) { return Vec3(-vec.x, -vec.y, -vec.z); }
  bool operator==(Vec3 const& r) const { return x == r.x && y == r.y && z == r.z; }
  bool operator!=(Vec3 const& r) const { return !(r == *this); }

  Vec3 compMul(Vec3 const& r) const { return Vec3(x * r.x, y * r.y, z * r.z); }
  Vec3 compDiv(Vec3 const& r) const { return Vec3(x / r.x, y / r.y, z / r.z); }

  // For each component: begin ~ (end - 1)
  template <typename Func>
  static void range(T begin, T end, Func func) {
    Vec3 a;
    for (a.x = begin; a.x < end; a.x++)
      for (a.y = begin; a.y < end; a.y++)
        for (a.z = begin; a.z < end; a.z++) func(a);
  }

  // For each component: begin ~ (end - 1)
  template <typename Func>
  static void range(Vec3 const& begin, Vec3 const& end, Func func) {
    Vec3 a;
    for (a.x = begin.x; a.x < end.x; a.x++)
      for (a.y = begin.y; a.y < end.y; a.y++)
        for (a.z = begin.z; a.z < end.z; a.z++) func(a);
  }
};

using Vec3i = Vec3<int>;
using Vec3u = Vec3<unsigned int>;
using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;

static_assert(std::copy_constructible<Vec3f>);
static_assert(std::assignable_from<Vec3f&, Vec3f&>);

#endif // VEC_H_
