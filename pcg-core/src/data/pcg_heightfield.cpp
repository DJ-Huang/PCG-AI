#include "data/pcg_heightfield.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pcg::internal::data {
namespace {

int wrap_index(int value, int count)
{
    if (count <= 0)
        return 0;
    const int wrapped = value % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

float fetch_component(const PcgHeightFieldLayer& layer,
                      int resolution_x,
                      int resolution_z,
                      int x,
                      int z,
                      int component)
{
    if (layer.border_type == HeightFieldBorderType::Repeat) {
        x = wrap_index(x, resolution_x);
        z = wrap_index(z, resolution_z);
    } else if (layer.border_type == HeightFieldBorderType::Streak) {
        x = std::clamp(x, 0, resolution_x - 1);
        z = std::clamp(z, 0, resolution_z - 1);
    } else if (x < 0 || x >= resolution_x || z < 0 || z >= resolution_z) {
        return layer.border_value;
    }

    const std::size_t index =
        (static_cast<std::size_t>(z) * static_cast<std::size_t>(resolution_x) +
         static_cast<std::size_t>(x)) * static_cast<std::size_t>(layer.tuple_size) +
        static_cast<std::size_t>(component);
    return index < layer.values.size() ? layer.values[index] : layer.border_value;
}

} // namespace

bool PcgHeightFieldLayer::valid_for(int resolution_x, int resolution_z) const
{
    if (tuple_size <= 0 || resolution_x <= 0 || resolution_z <= 0)
        return false;
    const std::size_t expected = static_cast<std::size_t>(resolution_x) *
                                 static_cast<std::size_t>(resolution_z) *
                                 static_cast<std::size_t>(tuple_size);
    return values.size() == expected;
}

PcgHeightField::PcgHeightField(int resolution_x,
                               int resolution_z,
                               double size_x,
                               double size_z,
                               PcgVec3 center,
                               HeightFieldSampling sampling,
                               HeightFieldOrientation orientation)
    : resolution_x_(resolution_x),
      resolution_z_(resolution_z),
      size_x_(size_x),
      size_z_(size_z),
      center_(center),
      sampling_(sampling),
      orientation_(orientation)
{
}

double PcgHeightField::spacing_x() const
{
    const int divisions = sampling_ == HeightFieldSampling::Corner
        ? resolution_x_ - 1
        : resolution_x_;
    return divisions > 0 ? size_x_ / static_cast<double>(divisions) : 0.0;
}

double PcgHeightField::spacing_z() const
{
    const int divisions = sampling_ == HeightFieldSampling::Corner
        ? resolution_z_ - 1
        : resolution_z_;
    return divisions > 0 ? size_z_ / static_cast<double>(divisions) : 0.0;
}

std::size_t PcgHeightField::sample_count() const
{
    if (resolution_x_ <= 0 || resolution_z_ <= 0)
        return 0;
    return static_cast<std::size_t>(resolution_x_) * static_cast<std::size_t>(resolution_z_);
}

bool PcgHeightField::valid() const
{
    if (resolution_x_ < 2 || resolution_z_ < 2 ||
        !std::isfinite(size_x_) || !std::isfinite(size_z_) ||
        size_x_ <= 0.0 || size_z_ <= 0.0 ||
        !find_layer("height") || !find_layer("mask")) {
        return false;
    }

    for (const auto& [name, layer] : layers_) {
        (void)name;
        if (!layer.valid_for(resolution_x_, resolution_z_))
            return false;
    }
    return true;
}

const PcgHeightFieldLayer* PcgHeightField::find_layer(const std::string& name) const
{
    const auto it = layers_.find(name);
    return it == layers_.end() ? nullptr : &it->second;
}

PcgHeightFieldLayer* PcgHeightField::find_layer_mut(const std::string& name)
{
    const auto it = layers_.find(name);
    return it == layers_.end() ? nullptr : &it->second;
}

PcgHeightFieldLayer& PcgHeightField::create_layer(const std::string& name,
                                                  int tuple_size,
                                                  float initial_value)
{
    PcgHeightFieldLayer layer;
    layer.tuple_size = std::max(1, tuple_size);
    layer.values.assign(sample_count() * static_cast<std::size_t>(layer.tuple_size), initial_value);
    auto [it, inserted] = layers_.insert_or_assign(name, std::move(layer));
    (void)inserted;
    return it->second;
}

PcgVec3 PcgHeightField::sample_position(int x, int z, double height) const
{
    const double sample_offset = sampling_ == HeightFieldSampling::Center ? 0.5 : 0.0;
    const double u = -size_x_ * 0.5 + (static_cast<double>(x) + sample_offset) * spacing_x();
    const double v = -size_z_ * 0.5 + (static_cast<double>(z) + sample_offset) * spacing_z();

    switch (orientation_) {
    case HeightFieldOrientation::XY:
        return PcgVec3{center_.x + u, center_.y + v, center_.z + height};
    case HeightFieldOrientation::YZ:
        return PcgVec3{center_.x + height, center_.y + u, center_.z + v};
    case HeightFieldOrientation::ZX:
    default:
        return PcgVec3{center_.x + u, center_.y + height, center_.z + v};
    }
}

bool PcgHeightField::world_to_grid(double world_x,
                                   double world_y,
                                   double world_z,
                                   double& grid_x,
                                   double& grid_z) const
{
    const double sx = spacing_x();
    const double sz = spacing_z();
    if (sx <= std::numeric_limits<double>::epsilon() ||
        sz <= std::numeric_limits<double>::epsilon()) {
        return false;
    }

    double u = 0.0;
    double v = 0.0;
    switch (orientation_) {
    case HeightFieldOrientation::XY:
        u = world_x - center_.x;
        v = world_y - center_.y;
        break;
    case HeightFieldOrientation::YZ:
        u = world_y - center_.y;
        v = world_z - center_.z;
        break;
    case HeightFieldOrientation::ZX:
    default:
        u = world_x - center_.x;
        v = world_z - center_.z;
        break;
    }

    const double sample_offset = sampling_ == HeightFieldSampling::Center ? 0.5 : 0.0;
    grid_x = (u + size_x_ * 0.5) / sx - sample_offset;
    grid_z = (v + size_z_ * 0.5) / sz - sample_offset;
    return std::isfinite(grid_x) && std::isfinite(grid_z);
}

bool PcgHeightField::sample_scalar_world(const std::string& layer_name,
                                         double world_x,
                                         double world_y,
                                         double world_z,
                                         double& out_value) const
{
    const PcgHeightFieldLayer* layer = find_layer(layer_name);
    if (!layer || layer->tuple_size != 1)
        return false;

    return sample_component_world(
        layer_name, 0, world_x, world_y, world_z, out_value);
}

bool PcgHeightField::sample_component_world(const std::string& layer_name,
                                             int component,
                                             double world_x,
                                             double world_y,
                                             double world_z,
                                             double& out_value) const
{
    const PcgHeightFieldLayer* layer = find_layer(layer_name);
    if (!layer || component < 0 || component >= layer->tuple_size ||
        !layer->valid_for(resolution_x_, resolution_z_))
        return false;

    double gx = 0.0;
    double gz = 0.0;
    if (!world_to_grid(world_x, world_y, world_z, gx, gz))
        return false;

    const int x0 = static_cast<int>(std::floor(gx));
    const int z0 = static_cast<int>(std::floor(gz));
    const double tx = gx - static_cast<double>(x0);
    const double tz = gz - static_cast<double>(z0);

    const double v00 = fetch_component(*layer, resolution_x_, resolution_z_, x0, z0, component);
    const double v10 = fetch_component(*layer, resolution_x_, resolution_z_, x0 + 1, z0, component);
    const double v01 = fetch_component(*layer, resolution_x_, resolution_z_, x0, z0 + 1, component);
    const double v11 = fetch_component(
        *layer, resolution_x_, resolution_z_, x0 + 1, z0 + 1, component);
    const double vx0 = v00 + (v10 - v00) * tx;
    const double vx1 = v01 + (v11 - v01) * tx;
    out_value = vx0 + (vx1 - vx0) * tz;
    return std::isfinite(out_value);
}

} // namespace pcg::internal::data
