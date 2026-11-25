#pragma once

#include <cmath>
#include <iostream>

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(double s) { x /= s; y /= s; return *this; }

    constexpr double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    constexpr double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    constexpr double distSq(const Vec2& o) const {
        double dx = x - o.x;
        double dy = y - o.y;
        return dx * dx + dy * dy;
    }
    double dist(const Vec2& o) const { return std::sqrt(distSq(o)); }
    constexpr double lengthSq() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSq()); }

    Vec2 normalized() const {
        double len = length();
        return len > 0.0 ? *this / len : Vec2{0.0, 0.0};
    }
};

inline Vec2 operator*(double s, const Vec2& v) { return v * s; }
