#pragma once

#include <vector>

namespace pcg::internal::geometry {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Frame3 {
    Vec3 origin;
    Vec3 tangent;
    Vec3 normal;
    Vec3 binormal;
};

Vec3 add(const Vec3& a, const Vec3& b);
Vec3 sub(const Vec3& a, const Vec3& b);
Vec3 scale(const Vec3& v, double s);
double dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
double length(const Vec3& v);
Vec3 normalize(const Vec3& v);

Vec3 catmull_rom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t);

/** Dense polyline from control points (line / polyline / Catmull-Rom). */
std::vector<Vec3> build_spline_polyline(const std::vector<Vec3>& control_points,
                                        const char* mode,
                                        bool closed,
                                        int subdivisions_per_segment);

/** Arc-length resample to approximately uniform spacing. */
std::vector<Vec3> resample_polyline_by_spacing(const std::vector<Vec3>& polyline, double spacing);

/** Resample to a fixed point count along arc length. */
std::vector<Vec3> resample_polyline_by_count(const std::vector<Vec3>& polyline, int point_count);

double polyline_length(const std::vector<Vec3>& polyline);

/** Rotation-minimizing style frames using a stable up vector. */
std::vector<Frame3> build_frames(const std::vector<Vec3>& polyline, const Vec3& up_hint = {0.0, 1.0, 0.0});

Vec3 transform_local_to_world(const Frame3& frame, const Vec3& local);

} // namespace pcg::internal::geometry
