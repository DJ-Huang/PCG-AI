#pragma once

#include "data/pcg_geometry.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace pcg::internal::data {

enum class HeightFieldOrientation {
    ZX,
    XY,
    YZ,
};

enum class HeightFieldSampling {
    Center,
    Corner,
};

enum class HeightFieldBorderType {
    Constant,
    Repeat,
    Streak,
};

struct PcgHeightFieldLayer {
    int tuple_size = 1;
    HeightFieldBorderType border_type = HeightFieldBorderType::Streak;
    float border_value = 0.0f;
    std::vector<float> values;

    bool valid_for(int resolution_x, int resolution_z) const;
};

/**
 * Typed 2D volume transport for Houdini-style height fields.
 *
 * Layers share one index-to-world transform. Scalar terrain layers use
 * tuple_size=1; tuple_size is retained in the day-one contract so vector
 * layers such as flowdir do not require a later transport rewrite.
 */
class PcgHeightField {
public:
    PcgHeightField() = default;
    PcgHeightField(int resolution_x,
                   int resolution_z,
                   double size_x,
                   double size_z,
                   PcgVec3 center,
                   HeightFieldSampling sampling = HeightFieldSampling::Corner,
                   HeightFieldOrientation orientation = HeightFieldOrientation::ZX);

    int resolution_x() const { return resolution_x_; }
    int resolution_z() const { return resolution_z_; }
    double size_x() const { return size_x_; }
    double size_z() const { return size_z_; }
    const PcgVec3& center() const { return center_; }
    HeightFieldSampling sampling() const { return sampling_; }
    HeightFieldOrientation orientation() const { return orientation_; }

    double spacing_x() const;
    double spacing_z() const;
    std::size_t sample_count() const;
    bool valid() const;

    const std::map<std::string, PcgHeightFieldLayer>& layers() const { return layers_; }
    std::map<std::string, PcgHeightFieldLayer>& layers_mut() { return layers_; }
    const PcgHeightFieldLayer* find_layer(const std::string& name) const;
    PcgHeightFieldLayer* find_layer_mut(const std::string& name);
    PcgHeightFieldLayer& create_layer(const std::string& name,
                                      int tuple_size,
                                      float initial_value = 0.0f);

    /** World position of one sample. Height is displaced along the orientation normal. */
    PcgVec3 sample_position(int x, int z, double height) const;

    /** Bilinear sample in world space. Returns false for invalid/missing/non-scalar layers. */
    bool sample_scalar_world(const std::string& layer_name,
                             double world_x,
                             double world_y,
                             double world_z,
                             double& out_value) const;

    /** Bilinear sample of one tuple component in world space. */
    bool sample_component_world(const std::string& layer_name,
                                int component,
                                double world_x,
                                double world_y,
                                double world_z,
                                double& out_value) const;

private:
    bool world_to_grid(double world_x,
                       double world_y,
                       double world_z,
                       double& grid_x,
                       double& grid_z) const;

    int resolution_x_ = 0;
    int resolution_z_ = 0;
    double size_x_ = 0.0;
    double size_z_ = 0.0;
    PcgVec3 center_{};
    HeightFieldSampling sampling_ = HeightFieldSampling::Corner;
    HeightFieldOrientation orientation_ = HeightFieldOrientation::ZX;
    std::map<std::string, PcgHeightFieldLayer> layers_;
};

} // namespace pcg::internal::data
