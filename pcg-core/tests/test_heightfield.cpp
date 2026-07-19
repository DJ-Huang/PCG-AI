#include "cook_hash.hpp"
#include "data/pcg_data_collection.hpp"
#include "data/pcg_heightfield_binary.hpp"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_point_binary.hpp"
#include "elements/heightfield_algorithms.hpp"
#include "pcg_api.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using pcg::internal::data::HeightFieldSampling;
using pcg::internal::data::PcgHeightField;
using pcg::internal::elements::ConvertHeightFieldOptions;
using pcg::internal::elements::HeightFieldCreateOptions;
using pcg::internal::elements::HeightFieldBlurOptions;
using pcg::internal::elements::HeightFieldClipOptions;
using pcg::internal::elements::HeightFieldCombineMode;
using pcg::internal::elements::HeightFieldDivisionMode;
using pcg::internal::elements::HeightFieldErodeOptions;
using pcg::internal::elements::HeightFieldDistortByNoiseOptions;
using pcg::internal::elements::HeightFieldFractalMode;
using pcg::internal::elements::HeightFieldLayerMode;
using pcg::internal::elements::HeightFieldLayerOptions;
using pcg::internal::elements::HeightFieldMaskByFeatureOptions;
using pcg::internal::elements::HeightFieldMaskNoiseOptions;
using pcg::internal::elements::HeightFieldNoiseOptions;
using pcg::internal::elements::HeightFieldProjectMode;
using pcg::internal::elements::HeightFieldProjectOptions;
using pcg::internal::elements::HeightFieldResampleOptions;
using pcg::internal::elements::HeightFieldScatterOptions;
using pcg::internal::elements::HeightFieldTerraceOptions;

bool near(double a, double b, double epsilon = 1e-5)
{
    return std::abs(a - b) <= epsilon;
}

PcgHeightField make_bilinear_fixture()
{
    PcgHeightField field(2, 2, 2.0, 2.0, {}, HeightFieldSampling::Corner);
    auto& height = field.create_layer("height", 1, 0.0f);
    height.values = {0.0f, 10.0f, 20.0f, 30.0f};
    field.create_layer("mask", 1, 0.0f);
    return field;
}

void test_contract_and_sampling()
{
    HeightFieldCreateOptions options;
    options.size_x = 256.0;
    options.size_z = 256.0;
    options.sampling = HeightFieldSampling::Corner;
    options.division_mode = HeightFieldDivisionMode::BySize;
    options.grid_spacing = 1.0;

    PcgHeightField field = pcg::internal::elements::create_heightfield(options);
    assert(field.valid());
    assert(field.resolution_x() == 257);
    assert(field.resolution_z() == 257);
    assert(field.sample_count() == 66049);
    assert(near(field.sample_position(0, 0, 0.0).x, -128.0));
    assert(near(field.sample_position(256, 256, 0.0).x, 128.0));
    assert(near(field.sample_position(256, 256, 0.0).z, 128.0));

    auto& flowdir = field.create_layer("flowdir", 2, 0.0f);
    assert(flowdir.valid_for(257, 257));
    assert(flowdir.values.size() == field.sample_count() * 2);

    PcgHeightField fixture = make_bilinear_fixture();
    double sampled = 0.0;
    assert(fixture.sample_scalar_world("height", 0.0, 0.0, 0.0, sampled));
    assert(near(sampled, 15.0));
    assert(fixture.sample_scalar_world("height", 2.0, 0.0, 0.0, sampled));
    assert(near(sampled, 20.0));

    pcg::internal::data::PcgDataCollection collection;
    collection.add_heightfield("out", field);
    assert(collection.find_heightfield("out"));
    assert(collection.primary_type() == pcg::internal::data::PcgDataType::HeightField);
    assert(pcg::internal::hash_heightfield(field) != 0);
}

void test_heightfield_binary_round_trip()
{
    PcgHeightField source = make_bilinear_fixture();
    auto& flow = source.create_layer("flowdir", 2, 0.0f);
    flow.values = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    const int required = pcg::internal::data::heightfield_binary_size(source);
    assert(required > pcg::internal::data::kPcgHeightFieldBinaryHeaderSize);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(required));
    assert(pcg::internal::data::write_heightfield_binary(
        source, bytes.data(), static_cast<int>(bytes.size())));

    PcgHeightField restored;
    assert(pcg::internal::data::read_heightfield_binary(
        bytes.data(), static_cast<int>(bytes.size()), restored));
    assert(restored.valid());
    assert(restored.resolution_x() == 2);
    assert(restored.resolution_z() == 2);
    assert(restored.find_layer("height")->values == source.find_layer("height")->values);
    assert(restored.find_layer("mask")->values == source.find_layer("mask")->values);
    assert(restored.find_layer("flowdir")->tuple_size == 2);
    assert(restored.find_layer("flowdir")->values == flow.values);
}

void test_noise_mask_and_determinism()
{
    HeightFieldCreateOptions create;
    create.size_x = 32.0;
    create.size_z = 32.0;
    create.grid_spacing = 2.0;
    PcgHeightField a = pcg::internal::elements::create_heightfield(create);
    PcgHeightField b = a;

    HeightFieldNoiseOptions noise;
    noise.amplitude = 12.0;
    noise.element_size = 10.0;
    noise.fractal = HeightFieldFractalMode::Terrain;
    noise.max_octaves = 4;
    noise.seed = 17;
    assert(pcg::internal::elements::apply_heightfield_noise(a, nullptr, noise));
    assert(pcg::internal::elements::apply_heightfield_noise(b, nullptr, noise));
    assert(a.find_layer("height")->values == b.find_layer("height")->values);

    PcgHeightField masked = pcg::internal::elements::create_heightfield(create);
    PcgHeightField zero_mask = pcg::internal::elements::create_heightfield(create);
    assert(pcg::internal::elements::apply_heightfield_noise(masked, &zero_mask, noise));
    for (float value : masked.find_layer("height")->values)
        assert(value == 0.0f);
}

PcgHeightField make_ramp_field()
{
    HeightFieldCreateOptions create;
    create.size_x = 4.0;
    create.size_z = 4.0;
    create.grid_spacing = 1.0;
    PcgHeightField field = pcg::internal::elements::create_heightfield(create);
    auto* height = field.find_layer_mut("height");
    for (int z = 0; z < field.resolution_z(); ++z) {
        for (int x = 0; x < field.resolution_x(); ++x)
            height->values[static_cast<std::size_t>(z * field.resolution_x() + x)] =
                static_cast<float>(x);
    }
    return field;
}

void test_l1_masks_and_shaping()
{
    PcgHeightField noise_a = make_ramp_field();
    PcgHeightField noise_b = noise_a;
    HeightFieldMaskNoiseOptions mask_noise;
    mask_noise.noise.center_noise = false;
    mask_noise.noise.amplitude = 1.0;
    mask_noise.noise.element_size = 3.0;
    mask_noise.noise.seed = 31;
    assert(pcg::internal::elements::apply_heightfield_mask_noise(
        noise_a, nullptr, mask_noise));
    assert(pcg::internal::elements::apply_heightfield_mask_noise(
        noise_b, nullptr, mask_noise));
    assert(noise_a.find_layer("mask")->values == noise_b.find_layer("mask")->values);
    for (float value : noise_a.find_layer("mask")->values)
        assert(value >= 0.0f && value <= 1.0f);

    PcgHeightField feature = make_ramp_field();
    HeightFieldMaskByFeatureOptions feature_options;
    feature_options.min_height = 2.0;
    feature_options.max_height = 3.0;
    assert(pcg::internal::elements::apply_heightfield_mask_by_feature(
        feature, nullptr, feature_options));
    const auto* feature_mask = feature.find_layer("mask");
    assert(feature_mask->values[2] == 1.0f);
    assert(feature_mask->values[3] == 1.0f);
    assert(feature_mask->values[1] == 0.0f);
    assert(feature_mask->values[4] == 0.0f);

    feature = make_ramp_field();
    feature_options.mask_by_height = false;
    feature_options.mask_by_slope = true;
    feature_options.min_slope_degrees = 40.0;
    feature_options.max_slope_degrees = 50.0;
    assert(pcg::internal::elements::apply_heightfield_mask_by_feature(
        feature, nullptr, feature_options));
    assert(feature.find_layer("mask")->values[2] == 1.0f);

    PcgHeightField clipped = make_ramp_field();
    HeightFieldClipOptions clip;
    clip.max_clip = 2.5;
    assert(pcg::internal::elements::apply_heightfield_clip(clipped, nullptr, clip));
    assert(near(clipped.find_layer("height")->values[4], 2.5));
    assert(clipped.find_layer("mesa"));
    assert(clipped.find_layer("cliffs"));
    assert(clipped.find_layer("mesa")->values[4] == 1.0f);

    PcgHeightField terraced = make_ramp_field();
    HeightFieldTerraceOptions terrace;
    terrace.fade = 0.0;
    terrace.max_step_size = 2.0;
    terrace.smooth_edges = 0.0;
    assert(pcg::internal::elements::apply_heightfield_terrace(
        terraced, nullptr, terrace));
    assert(near(terraced.find_layer("height")->values[3], 2.0));
    assert(terraced.find_layer("mesa"));
    assert(terraced.find_layer("cliffs"));

    HeightFieldCreateOptions impulse_create;
    impulse_create.size_x = 4.0;
    impulse_create.size_z = 4.0;
    impulse_create.grid_spacing = 1.0;
    PcgHeightField blurred = pcg::internal::elements::create_heightfield(impulse_create);
    blurred.find_layer_mut("height")->values[12] = 9.0f;
    HeightFieldBlurOptions blur;
    blur.radius_meters = 1.0;
    blur.method = pcg::internal::elements::HeightFieldBlurMethod::Box;
    assert(pcg::internal::elements::apply_heightfield_blur(blurred, nullptr, blur));
    assert(blurred.find_layer("height")->values[12] < 9.0f);
    assert(blurred.find_layer("height")->values[11] > 0.0f);
}

void test_l1_resample_and_layer()
{
    PcgHeightField source = make_ramp_field();
    auto& flowdir = source.create_layer("flowdir", 2, 0.0f);
    for (std::size_t i = 0; i < source.sample_count(); ++i) {
        flowdir.values[i * 2] = static_cast<float>(i);
        flowdir.values[i * 2 + 1] = static_cast<float>(100 + i);
    }
    HeightFieldResampleOptions resample;
    resample.resolution_scale = 2.0;
    PcgHeightField upsampled = pcg::internal::elements::resample_heightfield(source, resample);
    assert(upsampled.valid());
    assert(upsampled.resolution_x() == 9);
    assert(upsampled.resolution_z() == 9);
    assert(upsampled.find_layer("flowdir")->tuple_size == 2);
    assert(near(upsampled.find_layer("height")->values.back(), 4.0));
    assert(near(upsampled.find_layer("flowdir")->values.back(), 124.0));

    HeightFieldCreateOptions layer_create;
    layer_create.size_x = 4.0;
    layer_create.size_z = 4.0;
    layer_create.grid_spacing = 2.0;
    layer_create.initial_height = 10.0f;
    const PcgHeightField layer = pcg::internal::elements::create_heightfield(layer_create);

    HeightFieldCreateOptions base_create = layer_create;
    base_create.grid_spacing = 1.0;
    base_create.initial_height = 0.0f;
    const PcgHeightField base = pcg::internal::elements::create_heightfield(base_create);
    PcgHeightField mask = base;
    mask.find_layer_mut("mask")->values[12] = 1.0f;

    HeightFieldLayerOptions composite;
    composite.mode = HeightFieldLayerMode::Replace;
    composite.layers = "height";
    const PcgHeightField result = pcg::internal::elements::composite_heightfields(
        base, layer, &mask, composite);
    assert(result.valid());
    assert(near(result.find_layer("height")->values[12], 10.0));
    assert(near(result.find_layer("height")->values[0], 0.0));
}

void test_l2_erode_layers_and_determinism()
{
    HeightFieldCreateOptions create;
    create.size_x = 32.0;
    create.size_z = 32.0;
    create.grid_spacing = 1.0;
    PcgHeightField a = pcg::internal::elements::create_heightfield(create);
    HeightFieldNoiseOptions noise;
    noise.amplitude = 14.0;
    noise.element_size = 12.0;
    noise.max_octaves = 4;
    noise.seed = 51;
    assert(pcg::internal::elements::apply_heightfield_noise(a, nullptr, noise));
    PcgHeightField b = a;
    const std::vector<float> original = a.find_layer("height")->values;

    HeightFieldErodeOptions erode;
    erode.iterations = 18;
    erode.seed = 77;
    assert(pcg::internal::elements::apply_heightfield_erode(a, nullptr, erode));
    assert(pcg::internal::elements::apply_heightfield_erode(b, nullptr, erode));
    assert(a.find_layer("height")->values == b.find_layer("height")->values);
    assert(a.find_layer("sediment"));
    assert(a.find_layer("debris"));
    assert(a.find_layer("flow"));
    assert(a.find_layer("flowdir"));
    assert(a.find_layer("flowdir")->tuple_size == 2);
    assert(a.find_layer("flowdir")->values.size() == a.sample_count() * 2);
    assert(a.find_layer("height")->values != original);
    assert(*std::max_element(a.find_layer("flow")->values.begin(),
                             a.find_layer("flow")->values.end()) > 0.0f);
    for (float value : a.find_layer("height")->values)
        assert(std::isfinite(value));
    for (std::size_t i = 0; i < a.sample_count(); ++i) {
        const double x = a.find_layer("flowdir")->values[i * 2];
        const double z = a.find_layer("flowdir")->values[i * 2 + 1];
        assert(std::hypot(x, z) <= 1.0001);
    }

    PcgHeightField masked = pcg::internal::elements::create_heightfield(create);
    assert(pcg::internal::elements::apply_heightfield_noise(masked, nullptr, noise));
    const std::vector<float> masked_original = masked.find_layer("height")->values;
    PcgHeightField zero_mask = pcg::internal::elements::create_heightfield(create);
    assert(pcg::internal::elements::apply_heightfield_erode(masked, &zero_mask, erode));
    assert(masked.find_layer("height")->values == masked_original);
}

void test_l2_distort_project_and_scatter()
{
    PcgHeightField distorted_a = make_ramp_field();
    PcgHeightField distorted_b = distorted_a;
    const std::vector<float> original = distorted_a.find_layer("height")->values;
    HeightFieldDistortByNoiseOptions distort;
    distort.amplitude = 1.5;
    distort.element_size = 3.0;
    distort.max_octaves = 3;
    distort.substeps = 3;
    distort.seed = 123;
    assert(pcg::internal::elements::apply_heightfield_distort_by_noise(
        distorted_a, nullptr, distort));
    assert(pcg::internal::elements::apply_heightfield_distort_by_noise(
        distorted_b, nullptr, distort));
    assert(distorted_a.find_layer("height")->values ==
           distorted_b.find_layer("height")->values);
    assert(distorted_a.find_layer("height")->values != original);

    PcgHeightField zero_mask = make_ramp_field();
    std::fill(zero_mask.find_layer_mut("mask")->values.begin(),
              zero_mask.find_layer_mut("mask")->values.end(), 0.0f);
    PcgHeightField masked = make_ramp_field();
    const std::vector<float> masked_original = masked.find_layer("height")->values;
    assert(pcg::internal::elements::apply_heightfield_distort_by_noise(
        masked, &zero_mask, distort));
    assert(masked.find_layer("height")->values == masked_original);

    HeightFieldCreateOptions create;
    create.size_x = 4.0;
    create.size_z = 4.0;
    create.grid_spacing = 1.0;
    PcgHeightField projected = pcg::internal::elements::create_heightfield(create);
    pcg::internal::data::PcgGeometry geometry;
    geometry.points_mut() = {
        {-1.0, 5.0, -1.0}, {1.0, 5.0, -1.0},
        {1.0, 5.0, 1.0}, {-1.0, 5.0, 1.0},
    };
    geometry.faces_mut().push_back({0, 1, 2, 3});
    HeightFieldProjectOptions project;
    project.combine = HeightFieldProjectMode::Maximum;
    project.hit_farthest = true;
    project.max_ray_distance = 10.0;
    assert(pcg::internal::elements::apply_heightfield_project(
        projected, geometry, project));
    const auto* projected_height = projected.find_layer("height");
    assert(near(projected_height->values[12], 5.0));
    assert(near(projected_height->values[0], 0.0));

    std::fill(projected.find_layer_mut("mask")->values.begin(),
              projected.find_layer_mut("mask")->values.end(), 1.0f);
    HeightFieldScatterOptions scatter;
    scatter.point_count = 64;
    scatter.seed = 91;
    scatter.candidates_per_point = 4;
    const auto points_a = pcg::internal::elements::scatter_heightfield(projected, scatter);
    const auto points_b = pcg::internal::elements::scatter_heightfield(projected, scatter);
    assert(points_a.points().size() == 64);
    assert(points_b.points().size() == points_a.points().size());
    for (std::size_t i = 0; i < points_a.points().size(); ++i) {
        const auto& a = points_a.points()[i];
        const auto& b = points_b.points()[i];
        assert(near(a.x, b.x));
        assert(near(a.y, b.y));
        assert(near(a.z, b.z));
        assert(a.x >= -2.0 && a.x <= 2.0);
        assert(a.z >= -2.0 && a.z <= 2.0);
        const double nx = a.attributes.value("nx", 0.0);
        const double ny = a.attributes.value("ny", 0.0);
        const double nz = a.attributes.value("nz", 0.0);
        assert(near(std::sqrt(nx * nx + ny * ny + nz * nz), 1.0, 1e-4));
    }

    std::fill(projected.find_layer_mut("mask")->values.begin(),
              projected.find_layer_mut("mask")->values.end(), 0.0f);
    const auto empty_points = pcg::internal::elements::scatter_heightfield(projected, scatter);
    assert(empty_points.points().empty());
    std::vector<unsigned char> empty_binary(PCG_POINT_BINARY_HEADER_SIZE);
    uint32_t empty_flags = 99;
    assert(pcg::internal::data::write_point_binary(
        empty_points, empty_binary.data(), static_cast<int>(empty_binary.size()), &empty_flags));
    assert(empty_flags == PCG_POINT_ATTR_NONE);
}

void test_l3_mask_pattern_flow_slump_layers_and_file()
{
    HeightFieldCreateOptions create;
    create.size_x = 4.0;
    create.size_z = 4.0;
    create.grid_spacing = 1.0;
    PcgHeightField masked = pcg::internal::elements::create_heightfield(create);
    pcg::internal::data::PcgGeometry geometry;
    geometry.points_mut() = {
        {-1.0, 5.0, -1.0}, {1.0, 5.0, -1.0},
        {1.0, 5.0, 1.0}, {-1.0, 5.0, 1.0},
    };
    geometry.faces_mut().push_back({0, 1, 2, 3});
    pcg::internal::elements::HeightFieldMaskByObjectOptions mask_object;
    mask_object.side = pcg::internal::elements::HeightFieldMaskByObjectSide::Above;
    mask_object.max_ray_distance = 10.0;
    assert(pcg::internal::elements::apply_heightfield_mask_by_object(
        masked, geometry, mask_object));
    assert(masked.find_layer("mask")->values[12] > 0.5f);
    assert(masked.find_layer("mask")->values[0] == 0.0f);

    PcgHeightField patterned = pcg::internal::elements::create_heightfield(create);
    pcg::internal::elements::HeightFieldPatternOptions pattern;
    pattern.pattern = pcg::internal::elements::HeightFieldPatternKind::Ramp;
    pattern.combine = HeightFieldCombineMode::Replace;
    pattern.height = 4.0;
    pattern.size = 4.0;
    assert(pcg::internal::elements::apply_heightfield_pattern(patterned, nullptr, pattern));
    const auto* pattern_height = patterned.find_layer("height");
    assert(pattern_height->values.front() != pattern_height->values.back());

    PcgHeightField flow_field = make_ramp_field();
    pcg::internal::elements::HeightFieldFlowFieldOptions flow;
    flow.spread_iterations = 12;
    flow.copy_to_mask = true;
    flow.adjust_height = true;
    flow.adjust_height_scale = 0.25;
    flow.seed = 3;
    assert(pcg::internal::elements::apply_heightfield_flow_field(flow_field, flow));
    assert(flow_field.find_layer("flow"));
    assert(flow_field.find_layer("flowdir")->tuple_size == 2);
    assert(flow_field.find_layer("water"));
    float max_mask = 0.0f;
    for (float value : flow_field.find_layer("mask")->values)
        max_mask = std::max(max_mask, value);
    assert(max_mask > 0.0f);

    PcgHeightField slumped = make_ramp_field();
    slumped.create_layer("debris", 1, 1.0f);
    pcg::internal::elements::HeightFieldSlumpOptions slump;
    slump.spread_iterations = 8;
    slump.repose_angle_degrees = 20.0;
    slump.add_to_bedrock = true;
    assert(pcg::internal::elements::apply_heightfield_slump(slumped, nullptr, slump));
    assert(slumped.find_layer("flow"));
    assert(slumped.find_layer("flowdir")->tuple_size == 2);

    PcgHeightField layers = make_ramp_field();
    std::fill(layers.find_layer_mut("mask")->values.begin(),
              layers.find_layer_mut("mask")->values.end(), 0.75f);
    pcg::internal::elements::HeightFieldCopyLayerOptions copy;
    copy.source = "mask";
    copy.destination = "veg";
    assert(pcg::internal::elements::apply_heightfield_copy_layer(layers, copy));
    assert(layers.find_layer("veg")->values == layers.find_layer("mask")->values);

    pcg::internal::elements::HeightFieldLayerPropertiesOptions props;
    props.layer = "veg";
    props.border_type = pcg::internal::data::HeightFieldBorderType::Constant;
    props.border_value = 0.25f;
    assert(pcg::internal::elements::apply_heightfield_layer_properties(layers, props));
    assert(layers.find_layer("veg")->border_type ==
           pcg::internal::data::HeightFieldBorderType::Constant);

    pcg::internal::elements::HeightFieldIsolateLayerOptions isolate;
    isolate.layer = "veg";
    isolate.overwrite_mask = true;
    assert(pcg::internal::elements::apply_heightfield_isolate_layer(layers, isolate));
    assert(layers.find_layer("mask")->values == layers.find_layer("veg")->values);

    pcg::internal::elements::HeightFieldLayerClearOptions clear;
    clear.layer = "veg";
    clear.value = 0.0f;
    assert(pcg::internal::elements::apply_heightfield_layer_clear(layers, clear));
    for (float value : layers.find_layer("veg")->values)
        assert(value == 0.0f);

    const std::string pgm_path = "/tmp/pcg_heightfield_file_test.pgm";
    {
        std::ofstream out(pgm_path);
        out << "P2\n2 2\n255\n0 64\n128 255\n";
    }
    pcg::internal::elements::HeightFieldFileOptions file;
    file.file_path = pgm_path;
    file.size = 4.0;
    file.height_scale = 10.0;
    PcgHeightField from_file = pcg::internal::elements::create_heightfield_from_file(file);
    assert(from_file.valid());
    assert(from_file.resolution_x() == 2);
    assert(from_file.resolution_z() == 2);
    assert(near(from_file.find_layer("height")->values[3], 10.0));
}

void test_convert_quality_contract()
{
    HeightFieldCreateOptions create;
    create.size_x = 256.0;
    create.size_z = 256.0;
    create.grid_spacing = 1.0;
    PcgHeightField field = pcg::internal::elements::create_heightfield(create);

    HeightFieldNoiseOptions macro;
    macro.amplitude = 35.0;
    macro.element_size = 72.0;
    macro.max_octaves = 5;
    macro.seed = 23;
    assert(pcg::internal::elements::apply_heightfield_noise(field, nullptr, macro));

    HeightFieldNoiseOptions detail = macro;
    detail.amplitude = 4.0;
    detail.element_size = 12.0;
    detail.max_octaves = 3;
    detail.fractal = HeightFieldFractalMode::Standard;
    detail.seed = 71;
    assert(pcg::internal::elements::apply_heightfield_noise(field, nullptr, detail));

    const auto geometry = pcg::internal::elements::convert_heightfield_to_geometry(
        field, ConvertHeightFieldOptions{});
    assert(geometry.points().size() == 66049);
    assert(geometry.faces().size() == 65536);
    assert(geometry.has_uvs());
    assert(geometry.uvs().size() == geometry.points().size());
    assert(geometry.has_corner_uvs());
    assert(geometry.corner_uvs().size() == geometry.faces().size() * 4);
    assert(geometry.detail().shade_mode == pcg::internal::data::ShadeMode::Smooth);

    for (const auto orientation : {
             pcg::internal::data::HeightFieldOrientation::XY,
             pcg::internal::data::HeightFieldOrientation::YZ,
         }) {
        create.orientation = orientation;
        create.size_x = 2.0;
        create.size_z = 2.0;
        create.grid_spacing = 1.0;
        const auto oriented_field = pcg::internal::elements::create_heightfield(create);
        const auto oriented_geometry = pcg::internal::elements::convert_heightfield_to_geometry(
            oriented_field, ConvertHeightFieldOptions{});
        const auto& face = oriented_geometry.faces().front();
        const auto& a = oriented_geometry.points()[face[0]];
        const auto& b = oriented_geometry.points()[face[1]];
        const auto& c = oriented_geometry.points()[face[2]];
        const double ab_x = b.x - a.x;
        const double ab_y = b.y - a.y;
        const double ab_z = b.z - a.z;
        const double ac_x = c.x - a.x;
        const double ac_y = c.y - a.y;
        const double ac_z = c.z - a.z;
        const pcg::internal::data::PcgVec3 normal{
            ab_y * ac_z - ab_z * ac_y,
            ab_z * ac_x - ab_x * ac_z,
            ab_x * ac_y - ab_y * ac_x,
        };
        if (orientation == pcg::internal::data::HeightFieldOrientation::XY)
            assert(normal.z > 0.0);
        else
            assert(normal.x > 0.0);
    }
}

void test_graph_pipeline()
{
    const char* graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"hf","type":"HeightField","position":{"x":0,"y":0},"data":{
          "sizeX":16,"sizeZ":16,"sampling":"corner","divisionMode":"bySize","gridSpacing":1
        }},
        {"id":"macro","type":"HeightFieldNoise","position":{"x":0,"y":100},"data":{
          "amplitude":6,"elementSize":8,"fractal":"terrain","maxOctaves":4,"seed":5
        }},
        {"id":"detail","type":"HeightFieldNoise","position":{"x":0,"y":200},"data":{
          "amplitude":1,"elementSize":2,"fractal":"standard","maxOctaves":2,"seed":11
        }},
        {"id":"convert","type":"ConvertHeightField","position":{"x":0,"y":300},"data":{}},
        {"id":"out","type":"Output","position":{"x":0,"y":400},"data":{}}
      ],
      "edges":[
        {"id":"e1","source":"hf","target":"macro"},
        {"id":"e2","source":"macro","target":"detail"},
        {"id":"e3","source":"detail","target":"convert"},
        {"id":"e4","source":"convert","target":"out"}
      ]
    })";

    char error[512] = {};
    assert(pcg_validate_graph(graph, error, sizeof(error)) == PCG_OK);
    std::vector<unsigned char> mesh_buffer(1024 * 1024);
    char json[64 * 1024] = {};
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const auto result = pcg_execute_graph_v2(graph,
                                              42,
                                              &kind,
                                              json,
                                              sizeof(json),
                                              mesh_buffer.data(),
                                              static_cast<int>(mesh_buffer.size()),
                                              &vertex_count,
                                              &index_count,
                                              error,
                                              sizeof(error));
    if (result != PCG_OK)
        std::printf("graph error: %s\n", error);
    assert(result == PCG_OK);
    assert(kind == PCG_RESULT_KIND_MESH);
    assert(vertex_count == 289);
    assert(index_count == 16 * 16 * 6);
}

void test_l1_graph_pipeline()
{
    const char* graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"base","type":"HeightField","data":{"sizeX":8,"sizeZ":8,"gridSpacing":1}},
        {"id":"baseNoise","type":"HeightFieldNoise","data":{"amplitude":6,"elementSize":5,"seed":4}},
        {"id":"feature","type":"HeightFieldMaskByFeature","data":{"maskByHeight":false,"maskBySlope":true,"minSlopeAngle":5,"maxSlopeAngle":75,"smoothRadius":1}},
        {"id":"terrace","type":"HeightFieldTerrace","data":{"maxStepSize":2,"fade":0.25}},
        {"id":"blur","type":"HeightFieldBlur","data":{"radius":0.75,"iterations":1}},
        {"id":"resample","type":"HeightFieldResample","data":{"resolutionScale":2}},
        {"id":"layerSource","type":"HeightField","data":{"sizeX":8,"sizeZ":8,"gridSpacing":2,"initialHeight":2}},
        {"id":"maskNoise","type":"HeightFieldMaskNoise","data":{"elementSize":4,"seed":9}},
        {"id":"layer","type":"HeightFieldLayer","data":{"layerMode":"add","layers":"height"}},
        {"id":"clip","type":"HeightFieldClip","data":{"maxClip":8,"generateMaskFrom":"edge"}},
        {"id":"distort","type":"HeightFieldDistortByNoise","data":{"amplitude":0.5,"elementSize":3,"substeps":2,"seed":7}},
        {"id":"erode","type":"HeightFieldErode","data":{"iterations":5,"seed":12}},
        {"id":"convert","type":"ConvertHeightField","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"base","target":"baseNoise"},
        {"id":"e2","source":"baseNoise","target":"feature"},
        {"id":"e3","source":"baseNoise","target":"terrace","targetHandle":"in"},
        {"id":"e4","source":"feature","target":"terrace","targetHandle":"mask"},
        {"id":"e5","source":"terrace","target":"blur","targetHandle":"in"},
        {"id":"e6","source":"feature","target":"blur","targetHandle":"mask"},
        {"id":"e7","source":"blur","target":"resample"},
        {"id":"e8","source":"layerSource","target":"maskNoise"},
        {"id":"e9","source":"resample","target":"layer","targetHandle":"base"},
        {"id":"e10","source":"maskNoise","target":"layer","targetHandle":"layer"},
        {"id":"e11","source":"maskNoise","target":"layer","targetHandle":"mask"},
        {"id":"e12","source":"layer","target":"clip"},
        {"id":"e13","source":"clip","target":"distort"},
        {"id":"e14","source":"distort","target":"erode"},
        {"id":"e15","source":"erode","target":"convert"},
        {"id":"e16","source":"convert","target":"out"}
      ]
    })";

    char error[1024] = {};
    assert(pcg_validate_graph(graph, error, sizeof(error)) == PCG_OK);
    std::vector<unsigned char> mesh_buffer(2 * 1024 * 1024);
    char json[64 * 1024] = {};
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const auto result = pcg_execute_graph_v2(graph,
                                              42,
                                              &kind,
                                              json,
                                              sizeof(json),
                                              mesh_buffer.data(),
                                              static_cast<int>(mesh_buffer.size()),
                                              &vertex_count,
                                              &index_count,
                                              error,
                                              sizeof(error));
    if (result != PCG_OK)
        std::printf("L1 graph error: %s\n", error);
    assert(result == PCG_OK);
    assert(kind == PCG_RESULT_KIND_MESH);
    assert(vertex_count == 17 * 17);
    assert(index_count == 16 * 16 * 6);
}

void test_l2_project_and_scatter_graph_nodes()
{
    const char* project_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"base","type":"HeightField","data":{"sizeX":4,"sizeZ":4,"gridSpacing":1}},
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"project","type":"HeightFieldProject","data":{"combineMethod":"maximum","hitFarthest":true}},
        {"id":"convert","type":"ConvertHeightField","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"base","target":"project","targetHandle":"heightfield"},
        {"id":"e2","source":"box","target":"project","targetHandle":"geometry"},
        {"id":"e3","source":"project","target":"convert"},
        {"id":"e4","source":"convert","target":"out"}
      ]
    })";
    char error[1024] = {};
    const auto project_validation = pcg_validate_graph(project_graph, error, sizeof(error));
    assert(project_validation == PCG_OK);
    std::vector<unsigned char> mesh_buffer(2 * 1024 * 1024);
    char json[64 * 1024] = {};
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const auto project_result = pcg_execute_graph_v2(project_graph,
                                                     5,
                                                     &kind,
                                                     json,
                                                     sizeof(json),
                                                     mesh_buffer.data(),
                                                     static_cast<int>(mesh_buffer.size()),
                                                     &vertex_count,
                                                     &index_count,
                                                     error,
                                                     sizeof(error));
    if (project_result != PCG_OK)
        std::printf("HeightFieldProject graph error: %s\n", error);
    assert(project_result == PCG_OK);
    assert(kind == PCG_RESULT_KIND_MESH);
    pcg::internal::data::PcgMeshData decoded;
    const bool decoded_ok = pcg::internal::data::read_mesh_binary(
        mesh_buffer.data(), static_cast<int>(mesh_buffer.size()), decoded);
    assert(decoded_ok);
    double maximum_height = -1000.0;
    for (const auto& vertex : decoded.vertices())
        maximum_height = std::max(maximum_height, vertex.y);
    assert(near(maximum_height, 1.0));

    const char* scatter_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"base","type":"HeightField","data":{"sizeX":8,"sizeZ":8,"gridSpacing":1,"initialMask":1}},
        {"id":"noise","type":"HeightFieldNoise","data":{"amplitude":2,"elementSize":5,"seed":4}},
        {"id":"scatter","type":"HeightFieldScatter","data":{"pointCount":32,"globalSeed":9}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"base","target":"noise"},
        {"id":"e2","source":"noise","target":"scatter"},
        {"id":"e3","source":"scatter","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const auto scatter_validation = pcg_validate_graph(scatter_graph, error, sizeof(error));
    assert(scatter_validation == PCG_OK);
    std::vector<char> scatter_json(128 * 1024);
    kind = 0;
    vertex_count = 0;
    index_count = 0;
    const auto scatter_result = pcg_execute_graph_v2(scatter_graph,
                                                     5,
                                                     &kind,
                                                     scatter_json.data(),
                                                     static_cast<int>(scatter_json.size()),
                                                     nullptr,
                                                     0,
                                                     &vertex_count,
                                                     &index_count,
                                                     error,
                                                     sizeof(error));
    if (scatter_result != PCG_OK)
        std::printf("HeightFieldScatter graph error: %s\n", error);
    assert(scatter_result == PCG_OK);
    assert(kind == PCG_RESULT_KIND_JSON);
    const nlohmann::json parsed = nlohmann::json::parse(scatter_json.data());
    assert(parsed.contains("points"));
    assert(parsed["points"].size() == 32);

    nlohmann::json empty_graph_json = nlohmann::json::parse(scatter_graph);
    empty_graph_json["nodes"][0]["data"]["initialMask"] = 0.0;
    const std::string empty_graph = empty_graph_json.dump();
    std::fill(scatter_json.begin(), scatter_json.end(), '\0');
    const auto empty_result = pcg_execute_graph_v2(empty_graph.c_str(),
                                                   5,
                                                   &kind,
                                                   scatter_json.data(),
                                                   static_cast<int>(scatter_json.size()),
                                                   nullptr,
                                                   0,
                                                   &vertex_count,
                                                   &index_count,
                                                   error,
                                                   sizeof(error));
    if (empty_result != PCG_OK)
        std::printf("Empty HeightFieldScatter graph error: %s\n", error);
    assert(empty_result == PCG_OK);
    const nlohmann::json empty_parsed = nlohmann::json::parse(scatter_json.data());
    assert(empty_parsed.contains("points"));
    assert(empty_parsed["points"].empty());
}

void test_terrain_demo_graph()
{
    std::ifstream file("../../examples/terrain-demo.pcg");
    assert(file.good());
    const std::string graph((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

    char error[1024] = {};
    assert(pcg_validate_graph(graph.c_str(), error, sizeof(error)) == PCG_OK);
    std::vector<unsigned char> mesh_buffer(16 * 1024 * 1024);
    std::vector<char> json(128 * 1024);
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const auto result = pcg_execute_graph_v2(graph.c_str(),
                                              42,
                                              &kind,
                                              json.data(),
                                              static_cast<int>(json.size()),
                                              mesh_buffer.data(),
                                              static_cast<int>(mesh_buffer.size()),
                                              &vertex_count,
                                              &index_count,
                                              error,
                                              sizeof(error));
    if (result != PCG_OK)
        std::printf("terrain demo error: %s\n", error);
    assert(result == PCG_OK);
    assert(kind == PCG_RESULT_KIND_MESH);
    assert(vertex_count == 257 * 257);
    assert(index_count == 256 * 256 * 6);

    pcg::internal::data::PcgMeshData decoded;
    assert(pcg::internal::data::read_mesh_binary(
        mesh_buffer.data(), static_cast<int>(mesh_buffer.size()), decoded));
    assert(decoded.has_normals());
    assert(decoded.has_uvs());
    assert(*std::max_element(decoded.triangles().begin(), decoded.triangles().end()) > 65535);
}

void test_host_heightfield_v10_round_trip_and_cache_dirty()
{
    const char* graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"host","type":"GetTerrainData","data":{"bindingKey":"targetTerrain"}},
        {"id":"noise","type":"HeightFieldNoise","data":{"amplitude":0,"elementSize":2}},
        {"id":"convert","type":"ConvertHeightField","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"host","target":"noise"},
        {"id":"e2","source":"noise","target":"convert"},
        {"id":"e3","source":"convert","target":"out"}
      ]
    })";

    std::vector<float> heights = {
        0.0f, 1.0f, 2.0f,
        3.0f, 4.0f, 5.0f,
        6.0f, 7.0f, 8.0f,
    };
    std::vector<float> mask(heights.size(), 1.0f);
    PcgHeightFieldSlotV10 slot{};
    slot.slot_id = "host";
    slot.resolution_x = 3;
    slot.resolution_z = 3;
    slot.size_x = 2.0;
    slot.size_z = 2.0;
    slot.center_x = 1.0;
    slot.center_y = 0.0;
    slot.center_z = 1.0;
    slot.sampling = 1;
    slot.orientation = 0;
    slot.height_count = static_cast<int>(heights.size());
    slot.mask_count = static_cast<int>(mask.size());
    slot.height = heights.data();
    slot.mask = mask.data();

    auto execute = [&](PcgHeightField& decoded) {
        std::vector<char> json(64 * 1024);
        std::vector<unsigned char> mesh(1024 * 1024);
        std::vector<unsigned char> points(1024);
        std::vector<unsigned char> geometry(1024 * 1024);
        std::vector<unsigned char> heightfield(1024 * 1024);
        std::vector<char> perf(64 * 1024);
        char error[1024] = {};
        int kind = 0;
        int point_count = 0;
        uint32_t point_flags = 0;
        int vertex_count = 0;
        int index_count = 0;
        int geometry_bytes = 0;
        int heightfield_bytes = 0;
        PcgCookStats stats{};
        const PcgResultCode code = pcg_execute_graph_v10(
            graph, 42,
            nullptr, 0,
            nullptr, 0,
            nullptr, 0,
            &slot, 1,
            &kind,
            json.data(), static_cast<int>(json.size()),
            mesh.data(), static_cast<int>(mesh.size()),
            points.data(), static_cast<int>(points.size()),
            &point_count, &point_flags,
            &vertex_count, &index_count,
            &stats,
            perf.data(), static_cast<int>(perf.size()),
            geometry.data(), static_cast<int>(geometry.size()), &geometry_bytes,
            heightfield.data(), static_cast<int>(heightfield.size()), &heightfield_bytes,
            error, sizeof(error));
        if (code != PCG_OK)
            std::printf("host HeightField v10 error: %s\n", error);
        assert(code == PCG_OK);
        assert(kind == PCG_RESULT_KIND_MESH);
        assert(vertex_count == 9);
        assert(index_count == 24);
        assert(heightfield_bytes > 0);
        assert(pcg::internal::data::read_heightfield_binary(
            heightfield.data(), heightfield_bytes, decoded));
    };

    {
        std::vector<char> json(64 * 1024);
        std::vector<unsigned char> mesh(1024 * 1024);
        std::vector<unsigned char> points(1024);
        std::vector<unsigned char> geometry(1024 * 1024);
        unsigned char undersized_heightfield[1] = {};
        std::vector<char> perf(64 * 1024);
        char error[1024] = {};
        int kind = 0;
        int point_count = 0;
        uint32_t point_flags = 0;
        int vertex_count = 0;
        int index_count = 0;
        int geometry_bytes = 0;
        int required_heightfield_bytes = 0;
        PcgCookStats stats{};
        assert(pcg_execute_graph_v10(
            graph, 42,
            nullptr, 0, nullptr, 0, nullptr, 0, &slot, 1,
            &kind,
            json.data(), static_cast<int>(json.size()),
            mesh.data(), static_cast<int>(mesh.size()),
            points.data(), static_cast<int>(points.size()),
            &point_count, &point_flags, &vertex_count, &index_count, &stats,
            perf.data(), static_cast<int>(perf.size()),
            geometry.data(), static_cast<int>(geometry.size()), &geometry_bytes,
            undersized_heightfield, sizeof(undersized_heightfield),
            &required_heightfield_bytes,
            error, sizeof(error)) == PCG_OK);
        assert(required_heightfield_bytes >
               static_cast<int>(sizeof(undersized_heightfield)));
    }

    pcg_cook_cache_clear();
    PcgHeightField first;
    execute(first);
    assert(first.find_layer("height")->values == heights);

    heights[4] = 19.0f;
    slot.height = heights.data();
    PcgHeightField second;
    execute(second);
    assert(second.find_layer("height")->values == heights);
    assert(near(second.find_layer("height")->values[4], 19.0));

    {
        char error[1024] = {};
        PcgHeightFieldSlot legacy_slot{};
        legacy_slot.slot_id = "host";
        legacy_slot.height = heights.data();
        assert(pcg_execute_graph_v9(
            graph, 42,
            nullptr, 0, nullptr, 0, nullptr, 0, &legacy_slot, 1,
            nullptr, nullptr, 0, nullptr, 0, nullptr, 0,
            nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, nullptr,
            nullptr, 0, nullptr,
            error, sizeof(error)) == PCG_ERR_INVALID_ARGUMENT);
        assert(std::strstr(error, "use v10") != nullptr);

        slot.height_count = static_cast<int>(heights.size()) - 1;
        assert(pcg_execute_graph_v10(
            graph, 42,
            nullptr, 0, nullptr, 0, nullptr, 0, &slot, 1,
            nullptr, nullptr, 0, nullptr, 0, nullptr, 0,
            nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, nullptr,
            nullptr, 0, nullptr,
            error, sizeof(error)) == PCG_ERR_INVALID_ARGUMENT);
        assert(std::strstr(error, "height_count") != nullptr);

        slot.height_count = static_cast<int>(heights.size());
        slot.mask_count = static_cast<int>(mask.size()) - 1;
        assert(pcg_execute_graph_v10(
            graph, 42,
            nullptr, 0, nullptr, 0, nullptr, 0, &slot, 1,
            nullptr, nullptr, 0, nullptr, 0, nullptr, 0,
            nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, nullptr,
            nullptr, 0, nullptr,
            error, sizeof(error)) == PCG_ERR_INVALID_ARGUMENT);
        assert(std::strstr(error, "mask_count") != nullptr);
    }
}

} // namespace

int main()
{
    test_contract_and_sampling();
    test_heightfield_binary_round_trip();
    test_noise_mask_and_determinism();
    test_l1_masks_and_shaping();
    test_l1_resample_and_layer();
    test_l2_erode_layers_and_determinism();
    test_l2_distort_project_and_scatter();
    test_l3_mask_pattern_flow_slump_layers_and_file();
    test_convert_quality_contract();
    test_graph_pipeline();
    test_l1_graph_pipeline();
    test_l2_project_and_scatter_graph_nodes();
    test_terrain_demo_graph();
    test_host_heightfield_v10_round_trip_and_cache_dirty();
    std::printf("PASS: HeightField typed contract, sampling, noise, convert, graph, demo\n");
    return 0;
}
