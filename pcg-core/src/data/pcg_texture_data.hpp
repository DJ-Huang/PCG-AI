#pragma once

#include <cstddef>
#include <vector>

namespace pcg::internal::data {

/** RGBA image payload for mesh displacement sampling (Unity uploads pixels at execute time). */
class PcgTextureData {
public:
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<float>& rgba() const { return rgba_; }

    static PcgTextureData from_rgba(int width, int height, const float* rgba, int rgba_float_count);
    bool empty() const { return width_ <= 0 || height_ <= 0 || rgba_.empty(); }

    /** Local object-space position → grayscale [0,1] with repeat tiling (Blender Displace flat mapping). */
    double sample_grayscale_local(double x, double y, double z, double scale, double repeat_x,
                                  double repeat_y) const;

private:
    double sample_bilinear(double u, double v) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<float> rgba_;
};

} // namespace pcg::internal::data
