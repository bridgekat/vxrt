#ifndef MAT_H_
#define MAT_H_

#include <array>
#include <cassert>
#include <concepts>
#include <utility>
#include "common.h"
#include "vec.h"

template <typename T, size_t N>
class BasicMatrixRow {
public:
  T* p;
  T& operator[](size_t index) const noexcept {
    assert(index < N);
    return p[index];
  }
};

template <typename T, size_t M, size_t N>
class BasicMatrix {
public:
  BasicMatrix() noexcept { std::fill(mData.begin(), mData.end(), 0); }
  BasicMatrix(BasicMatrix const& r) noexcept { std::copy(r.mData.begin(), r.mData.end(), mData.begin()); }

  T* data() noexcept { return mData.data(); }
  T const* data() const noexcept { return mData.data(); }

  BasicMatrixRow<T, N> operator[](size_t index) noexcept {
    assert(index < M);
    return BasicMatrixRow<T, N>{data() + index * N};
  }

  BasicMatrixRow<T const, N> operator[](size_t index) const noexcept {
    assert(index < M);
    return BasicMatrixRow<T const, N>{data() + index * N};
  }

  BasicMatrix operator+(BasicMatrix const& r) const {
    auto res = *this;
    res += r;
    return res;
  }

  BasicMatrix& operator+=(BasicMatrix const& r) {
    for (size_t i = 0; i < M; i++) {
      for (size_t j = 0; j < N; j++) (*this)[i][i] += r[i][i];
    }
    return *this;
  }

  BasicMatrix& operator*=(BasicMatrix<T, N, N> const& r) {
    *this = *this * r;
    return *this;
  }

  void swapRows(size_t i, size_t j) {
    assert(i < M && j < M);
    for (size_t k = 0; k < N; k++) std::swap((*this)[i][k], (*this)[j][k]);
  }

  void mulRow(size_t i, T value) {
    assert(i < M);
    for (size_t k = 0; k < N; k++) (*this)[i][k] *= value;
  }

  void mulAddRow(size_t src, size_t dst, T value) {
    assert(dst < M && src < M);
    for (size_t k = 0; k < N; k++) (*this)[dst][k] += (*this)[src][k] * value;
  }

private:
  std::array<T, M * N> mData;
};

template <typename T, size_t M, size_t K, size_t N>
BasicMatrix<T, M, N> operator*(BasicMatrix<T, M, K> const& l, BasicMatrix<T, K, N> const& r) {
  BasicMatrix<T, M, N> res;
  for (size_t i = 0; i < M; i++) {
    for (size_t k = 0; k < K; k++) {
      for (size_t j = 0; j < N; j++) res[i][j] += l[i][k] * r[k][j];
    }
  }
  return res;
}

// Specialization for square matrices.
template <typename T, size_t N>
class SquareMatrix: public BasicMatrix<T, N, N> {
public:
  using BasicMatrix<T, N, N>::BasicMatrix;

  // Identity matrix constructor.
  SquareMatrix(T value) noexcept {
    for (size_t i = 0; i < N; i++) (*this)[i][i] = value;
  }

  SquareMatrix& transpose() {
    for (size_t i = 0; i < N; i++) {
      for (size_t j = i + 1; j < N; j++) std::swap((*this)[i][j], (*this)[j][i]);
    }
    return *this;
  }

  SquareMatrix transposed() const {
    auto res = *this;
    res.transpose();
    return res;
  }

  SquareMatrix& invert() {
    *this = inverted();
    return *this;
  }

  SquareMatrix inverted() const {
    // Gauss-Jordan with partial pivoting.
    auto t = *this, res = SquareMatrix(1);
    for (size_t i = 0; i < N; i++) {
      size_t p = i;
      for (size_t j = i + 1; j < N; j++) {
        if (abs(t[j][i]) > abs(t[p][i])) p = j;
      }
      t.swapRows(i, p);
      res.swapRows(i, p);
      T k = T(1) / t[i][i];
      t.mulRow(i, k);
      res.mulRow(i, k);
      for (size_t j = 0; j < N; j++) {
        if (j != i) {
          k = -t[j][i];
          t.mulAddRow(i, j, k);
          res.mulAddRow(i, j, k);
        }
      }
    }
    return res;
  }
};

// Specialization for 4x4 matrices.
template <typename T>
class Matrix4: public SquareMatrix<T, 4> {
public:
  static constexpr double Pi = 3.1415926535897932;
  using SquareMatrix<T, 4>::SquareMatrix;

  // Returns a translation matrix.
  static Matrix4 translation(const Vec3<T>& delta) {
    Matrix4 res(1);
    res[0][3] = delta.x;
    res[1][3] = delta.y;
    res[2][3] = delta.z;
    return res;
  }

  // Returns a rotation matrix.
  static Matrix4 rotation(T degrees, Vec3<T> vec) {
    Matrix4 res;
    vec.normalize();
    T alpha = degrees * T(Pi) / T(180), s = sin(alpha), c = cos(alpha), t = T(1) - c;
    res[0][0] = t * vec.x * vec.x + c;
    res[0][1] = t * vec.x * vec.y - s * vec.z;
    res[0][2] = t * vec.x * vec.z + s * vec.y;
    res[1][0] = t * vec.x * vec.y + s * vec.z;
    res[1][1] = t * vec.y * vec.y + c;
    res[1][2] = t * vec.y * vec.z - s * vec.x;
    res[2][0] = t * vec.x * vec.z - s * vec.y;
    res[2][1] = t * vec.y * vec.z + s * vec.x;
    res[2][2] = t * vec.z * vec.z + c;
    res[3][3] = T(1);
    return res;
  }

  // Returns a perspective projection matrix.
  static Matrix4 perspective(T fov, T aspect, T zNear, T zFar) {
    Matrix4 res;
    T f = T(1) / tan(fov * T(Pi) / T(180) / T(2));
    T a = zNear - zFar;
    res[0][0] = f / aspect;
    res[1][1] = f;
    res[2][2] = (zFar + zNear) / a;
    res[2][3] = T(2) * zFar * zNear / a;
    res[3][2] = T(-1);
    return res;
  }

  // Returns an orthogonal projection matrix.
  static Matrix4 ortho(T left, T right, T top, T bottom, T zNear, T zFar) {
    T a = right - left;
    T b = top - bottom;
    T c = zFar - zNear;
    Matrix4 res;
    res[0][0] = T(2) / a;
    res[1][1] = T(2) / b;
    res[2][2] = T(-2) / c;
    res[0][3] = -(right + left) / a;
    res[1][3] = -(top + bottom) / b;
    res[2][3] = -(zFar + zNear) / c;
    res[3][3] = T(1);
    return res;
  }

  // Returns product with Vec4(vec, w).
  std::pair<Vec3<T>, T> transform(const Vec3<T>& vec, T w) const {
    auto const& self = *this;
    Vec3<T> res(
      self[0][0] * vec.x + self[0][1] * vec.y + self[0][2] * vec.z + self[0][3] * w,
      self[1][0] * vec.x + self[1][1] * vec.y + self[1][2] * vec.z + self[1][3] * w,
      self[2][0] * vec.x + self[2][1] * vec.y + self[2][2] * vec.z + self[2][3] * w
    );
    T rw = self[3][0] * vec.x + self[3][1] * vec.y + self[3][2] * vec.z + self[3][3] * w;
    return std::make_pair(res, rw);
  }
};

typedef Matrix4<float> Mat4f;
typedef Matrix4<double> Mat4d;

static_assert(std::copy_constructible<Mat4f>);
static_assert(std::assignable_from<Mat4f&, Mat4f&>);

#endif // MAT_H_
