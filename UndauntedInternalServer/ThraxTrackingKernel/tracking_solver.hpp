#pragma once
// Clean-room float implementation of the repository's JS semantic control.
// Not a claim of native SIMD equivalence. No engine types, callbacks or global state.
#include <algorithm>
#include <array>
#include <cmath>
#include "source_math.hpp"

namespace feareater_tracking {
using Vec = std::array<float, 3>;
using Quat = std::array<float, 4>;
constexpr Quat identity{0, 0, 0, 1};
constexpr float pi = 3.14159265358979323846f;
constexpr float small = 1.e-8f;
struct Transform {
    Quat q = identity;
    Vec t{0, 0, 0};
    Vec s{1, 1, 1};
};
struct Input {
    Transform frame;
    Vec target_world{};
    // Caller supplies matching original local/component transforms in parent order.
    // locals[0] is validated but unused because components[0] anchors the chain.
    std::array<Transform, 7> components{}, locals{};
    Quat previous = identity;
    // Caller owns accumulation, consume-once timing, relevance and reset.
    float delta = 0, speed = 3;
};
enum class Status {
    ok,
    nonfinite_input,
    nonunit_rotation,
    unsupported_negative_scale,
    numerical_failure,
    unqualified_yaw_seam
};
struct Result {
    Status status = Status::numerical_failure;
    // Outputs are valid only on ok. Failure never returns a partial modified pose.
    Quat offset = identity;
    std::array<Transform, 7> poses{};
    float pitch = 0, yaw = 0;
};
template <std::size_t N> inline bool finite(const std::array<float, N> &v) {
    for (float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}
template <std::size_t N>
inline float dot(const std::array<float, N> &a, const std::array<float, N> &b) {
    float r = 0;
    for (std::size_t i = 0; i < N; ++i)
        r += a[i] * b[i];
    return r;
}
inline Vec add(Vec a, Vec b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec scale(Vec a, float x) { return {a[0] * x, a[1] * x, a[2] * x}; }
inline Vec multiply(Vec a, Vec b) { return {a[0] * b[0], a[1] * b[1], a[2] * b[2]}; }
inline Vec cross(Vec a, Vec b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Quat conjugate(Quat q) { return {-q[0], -q[1], -q[2], q[3]}; }
inline Quat product(Quat a, Quat b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
inline Vec rotate(Quat q, Vec v) {
    Vec axis{q[0], q[1], q[2]}, t = scale(cross(axis, v), 2);
    return add(v, add(scale(t, q[3]), cross(axis, t)));
}
inline Vec normal(Vec v) {
    const float n = dot(v, v);
    if (n == 1)
        return v;
    return n >= small ? scale(v, source_math::inverse_sqrt(n)) : Vec{0, 0, 0};
}
inline bool unit(Quat q) { return finite(q) && std::fabs(dot(q, q) - 1) < 2.e-4f; }
inline Status validate(const Transform &t) {
    if (!finite(t.q) || !finite(t.t) || !finite(t.s))
        return Status::nonfinite_input;
    if (!unit(t.q))
        return Status::nonunit_rotation;
    return Status::ok;
}
inline Transform compose(const Transform &p, const Transform &l);
inline Vec inverse_position(const Transform &f, Vec world) {
    Vec v = rotate(conjugate(f.q), sub(world, f.t));
    for (int i = 0; i < 3; ++i)
        v[i] = std::fabs(f.s[i]) <= small ? 0 : v[i] / f.s[i];
    return v;
}
inline Quat rotator(float pitch, float yaw) { return source_math::rotator(pitch, yaw); }
inline Quat basis(const std::array<Vec, 3> &rows) {
    for (const Vec &row : rows)
        if (std::fabs(row[0]) <= 1.e-4f && std::fabs(row[1]) <= 1.e-4f &&
            std::fabs(row[2]) <= 1.e-4f)
            return identity;
    float m[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i][j] = rows[j][i];
    float trace = m[0][0] + m[1][1] + m[2][2];
    Quat q{};
    if (trace > 0) {
        float inverse = source_math::inverse_sqrt(trace + 1), factor = inverse * .5f;
        q[3] = .5f / inverse;
        q[0] = (m[2][1] - m[1][2]) * factor;
        q[1] = (m[0][2] - m[2][0]) * factor;
        q[2] = (m[1][0] - m[0][1]) * factor;
    } else {
        int i = 0;
        if (m[1][1] > m[0][0])
            i = 1;
        if (m[2][2] > m[i][i])
            i = 2;
        int j = (i + 1) % 3, k = (j + 1) % 3;
        float inverse = source_math::inverse_sqrt(m[i][i] - m[j][j] - m[k][k] + 1),
              factor = inverse * .5f;
        q[i] = .5f / inverse;
        q[3] = (m[k][j] - m[j][k]) * factor;
        q[j] = (m[j][i] + m[i][j]) * factor;
        q[k] = (m[k][i] + m[i][k]) * factor;
    }
    return q;
}
inline bool negative(Vec s) { return s[0] < 0 || s[1] < 0 || s[2] < 0; }
struct Matrix {
    std::array<Vec, 3> rows{};
    Vec translation{};
};
inline Matrix matrix(const Transform &t) {
    return {{{scale(rotate(t.q, {1, 0, 0}), t.s[0]), scale(rotate(t.q, {0, 1, 0}), t.s[1]),
              scale(rotate(t.q, {0, 0, 1}), t.s[2])}},
            t.t};
}
inline Vec row_product(Vec v, const std::array<Vec, 3> &rows) {
    return add(add(scale(rows[0], v[0]), scale(rows[1], v[1])), scale(rows[2], v[2]));
}
inline Matrix matrix_product(const Matrix &a, const Matrix &b) {
    Matrix m;
    for (int i = 0; i < 3; ++i)
        m.rows[i] = row_product(a.rows[i], b.rows);
    m.translation = add(row_product(a.translation, b.rows), b.translation);
    return m;
}
inline Quat normalized(Quat q) {
    const float n = (q[0] * q[0] + q[2] * q[2]) + (q[1] * q[1] + q[3] * q[3]);
    if (n < small)
        return identity;
    const float inverse = source_math::inverse_sqrt(n);
    for (float &x : q)
        x *= inverse;
    return q;
}
inline Transform reconstruct(Matrix m, Vec desired_scale) {
    for (int i = 0; i < 3; ++i) {
        const float length = dot(m.rows[i], m.rows[i]);
        if (length >= small)
            m.rows[i] = scale(m.rows[i], source_math::inverse_sqrt(length));
        if (desired_scale[i] < 0)
            m.rows[i] = scale(m.rows[i], -1);
    }
    return {normalized(basis(m.rows)), m.translation, desired_scale};
}
inline Transform compose(const Transform &p, const Transform &l) {
    if (negative(p.s) || negative(l.s))
        return reconstruct(matrix_product(matrix(l), matrix(p)), multiply(p.s, l.s));
    return {product(p.q, l.q), add(p.t, rotate(p.q, multiply(p.s, l.t))), multiply(p.s, l.s)};
}
inline Matrix inverse_matrix(const Matrix &m) {
    Matrix result{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {0, 0, 0}};
    const Vec a = cross(m.rows[1], m.rows[2]), b = cross(m.rows[2], m.rows[0]),
              c = cross(m.rows[0], m.rows[1]);
    const float determinant = dot(m.rows[0], a);
    if (determinant == 0)
        return result;
    for (int i = 0; i < 3; ++i)
        result.rows[i] = {a[i] / determinant, b[i] / determinant, c[i] / determinant};
    result.translation = scale(row_product(m.translation, result.rows), -1);
    return result;
}
inline Transform relative(const Transform &child, const Transform &parent) {
    Vec inverse{};
    for (int i = 0; i < 3; ++i)
        inverse[i] = std::fabs(parent.s[i]) <= small ? 0 : 1 / parent.s[i];
    const Vec desired = multiply(child.s, inverse);
    if (negative(child.s) || negative(parent.s))
        return reconstruct(matrix_product(matrix(child), inverse_matrix(matrix(parent))), desired);
    return {product(conjugate(parent.q), child.q),
            multiply(rotate(conjugate(parent.q), sub(child.t, parent.t)), inverse), desired};
}
inline Quat interpolate(Quat previous, Quat desired, float delta, float speed) {
    if (delta <= 0)
        return previous;
    bool same = true, opposite = true;
    for (int i = 0; i < 4; ++i) {
        same &= std::fabs(previous[i] - desired[i]) <= 1.e-4f;
        opposite &= std::fabs(previous[i] + desired[i]) <= 1.e-4f;
    }
    if (speed <= 0 || same || opposite)
        return desired;
    float alpha = std::clamp(delta * speed, 0.f, 1.f), d = dot(previous, desired),
          sign = d < 0 ? -1.f : 1.f;
    d = std::fabs(d);
    float a = 1 - alpha, b = alpha;
    if (d < .9999f) {
        float angle = std::acos(std::clamp(d, -1.f, 1.f));
        a = std::sin((1 - alpha) * angle) / std::sin(angle);
        b = std::sin(alpha * angle) / std::sin(angle);
    }
    Quat q{};
    for (int i = 0; i < 4; ++i)
        q[i] = a * previous[i] + b * sign * desired[i];
    float n = std::sqrt(dot(q, q));
    for (float &v : q)
        v /= n;
    return q;
}
inline Result solve(const Input &in) {
    Result failure;
    auto reject = [&](Status s) {
        failure.status = s;
        return failure;
    };
    if (!finite(in.target_world) || !finite(in.previous) || !std::isfinite(in.delta) ||
        !std::isfinite(in.speed))
        return reject(Status::nonfinite_input);
    if (!unit(in.previous))
        return reject(Status::nonunit_rotation);
    Status check = validate(in.frame);
    if (check != Status::ok)
        return reject(check);
    for (const auto *a : {&in.components, &in.locals})
        for (const auto &t : *a) {
            check = validate(t);
            if (check != Status::ok)
                return reject(check);
        }
    const Transform &head = in.components.back();
    Vec displacement = sub(inverse_position(in.frame, in.target_world), head.t);
    // Overflow must not masquerade as the source small-vector fallback.
    if (!finite(displacement) || !std::isfinite(dot(displacement, displacement)))
        return failure;
    Vec forward_axis = rotate(head.q, {head.s[0], 0, 0}),
        up_axis = rotate(head.q, {0, 0, head.s[2]});
    if (!std::isfinite(dot(forward_axis, forward_axis)) || !std::isfinite(dot(up_axis, up_axis)))
        return failure;
    Vec direction = normal(displacement), forward = normal(forward_axis), up = normal(up_axis);
    if (!finite(forward) || !finite(up))
        return failure;
    Quat head_basis = basis({forward, normal(cross(up, forward)), up});
    Quat desired_direction = source_math::direction(direction);
    Quat raw = product(desired_direction, conjugate(head_basis));
    if (!finite(raw) || !unit(head_basis))
        return failure;
    float singular = raw[0] * raw[2] - raw[1] * raw[3];
    float pitch = singular < -.4999995f ? -90.f
                  : singular > .4999995f
                      ? 90.f
                      : source_math::asin(2 * singular) * source_math::radians_to_degrees;
    float yaw = source_math::atan2(2 * (raw[3] * raw[2] + raw[0] * raw[1]),
                                   1 - 2 * (raw[1] * raw[1] + raw[2] * raw[2])) *
                source_math::radians_to_degrees;
    Result out;
    out.pitch = std::clamp(pitch, -45.f, 45.f);
    out.yaw = std::clamp(yaw, -75.f, 75.f);
    out.offset = interpolate(in.previous, rotator(out.pitch, out.yaw), in.delta, in.speed);
    if (!unit(out.offset))
        return failure;
    Vec axis{out.offset[0], out.offset[1], out.offset[2]};
    float axis_squared = dot(axis, axis);
    axis = axis_squared >= small ? scale(axis, 1 / std::sqrt(axis_squared)) : Vec{1, 0, 0};
    float half = std::acos(std::clamp(out.offset[3], -1.f, 1.f));
    for (int i = 0; i < 7; ++i) {
        Transform base = i == 0 ? in.components[0] : compose(out.poses[i - 1], in.locals[i]);
        // Fixed seven-bone source expansion: EaseOut2 incremental fractions.
        float h = half * (13.f - 2.f * float(i)) / 49.f;
        const auto sc = source_math::sincos(h);
        Quat step{axis[0] * sc.sin, axis[1] * sc.sin, axis[2] * sc.sin, sc.cos};
        base.q = product(step, base.q);
        if (validate(base) != Status::ok)
            return failure;
        out.poses[i] = base;
    }
    out.status = Status::ok;
    return out;
}
} // namespace feareater_tracking
