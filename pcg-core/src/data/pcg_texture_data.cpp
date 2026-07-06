#include "data/pcg_texture_data.hpp"

#include <algorithm>
#include <cmath>

namespace pcg::internal::data {
namespace {

double positive_fract(double v)
{
    v = std::fmod(v, 1.0);
    if (v < 0.0)
        v += 1.0;
    return v;
}

} // namespace

PcgTextureData PcgTextureData::from_rgba(int width, int height, const float* rgba, int rgba_float_count)
{
    PcgTextureData out;
    if (width <= 0 || height <= 0 || !rgba)
        return out;

    const int required = width * height * 4;
    if (rgba_float_count < required)
        return out;

    out.width_ = width;
    out.height_ = height;
    out.rgba_.assign(rgba, rgba + required);
    return out;
}

double PcgTextureData::sample_bilinear(double u, double v) const
{
    if (empty())
        return 0.5;

    u = positive_fract(u);
    v = positive_fract(v);

    const double fx = u * static_cast<double>(width_ - 1);
    const double fy = v * static_cast<double>(height_ - 1);
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, width_ - 1);
    const int y1 = std::min(y0 + 1, height_ - 1);
    const double tx = fx - static_cast<double>(x0);
    const double ty = fy - static_cast<double>(y0);

    const auto lum_at = [&](int x, int y) -> double {
        const size_t idx = static_cast<size_t>((y * width_ + x) * 4);
        const float r = rgba_[idx];
        const float g = rgba_[idx + 1];
        const float b = rgba_[idx + 2];
        return static_cast<double>((r + g + b) / 3.0f);
    };

    const double c00 = lum_at(x0, y0);
    const double c10 = lum_at(x1, y0);
    const double c01 = lum_at(x0, y1);
    const double c11 = lum_at(x1, y1);
    const double cx0 = c00 + (c10 - c00) * tx;
    const double cx1 = c01 + (c11 - c01) * tx;
    return cx0 + (cx1 - cx0) * ty;
}

double PcgTextureData::sample_grayscale_local(double x, double y, double z, double scale,
                                              double repeat_x, double repeat_y) const
{
    (void)z;
    const double safe_scale = std::max(scale, 1e-6);
    const double safe_rx = std::max(repeat_x, 1e-6);
    const double safe_ry = std::max(repeat_y, 1e-6);
    const double u = positive_fract(x * safe_scale * safe_rx);
    const double v = positive_fract(y * safe_scale * safe_ry);
    return sample_bilinear(u, v);
}

} // namespace pcg::internal::data
