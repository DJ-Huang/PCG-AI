#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_spline_data.hpp"
#include "elements/spline_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/spline_geometry.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected),
                    static_cast<int>(actual));
        std::exit(1);
    }
}

bool read_mesh_binary(const void* buffer,
                      int buffer_size,
                      int* vertex_count,
                      int* index_count,
                      std::vector<float>& positions,
                      std::vector<uint32_t>& indices)
{
    if (!buffer || buffer_size < 16 || !vertex_count || !index_count)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(buffer);
    const uint32_t magic = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    if (magic != PCG_MESH_BINARY_MAGIC)
        return false;

    *vertex_count = static_cast<int>(bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24));
    *index_count = static_cast<int>(bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | (bytes[15] << 24));

    const int pos_bytes = *vertex_count * 3 * static_cast<int>(sizeof(float));
    const int idx_bytes = *index_count * static_cast<int>(sizeof(uint32_t));
    if (buffer_size < 16 + pos_bytes + idx_bytes)
        return false;

    positions.resize(static_cast<size_t>(*vertex_count) * 3);
    std::memcpy(positions.data(), bytes + 16, static_cast<size_t>(pos_bytes));
    indices.resize(static_cast<size_t>(*index_count));
    std::memcpy(indices.data(), bytes + 16 + pos_bytes, static_cast<size_t>(idx_bytes));
    return true;
}

PcgResultCode execute_mesh_graph(const char* json, int seed, int& vertex_count, int& index_count)
{
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int kind = 0;
  char json_out[4096];
    char err[512];
    const PcgResultCode rc = pcg_execute_graph_v7(
        json, seed, nullptr, 0, nullptr, 0, nullptr, 0, &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()), nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count, nullptr, nullptr, 0, err, sizeof(err));
    if (rc != PCG_OK)
        std::printf("execute error: %s\n", err);
    return rc;
}

bool nearly_equal(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) <= tol;
}

bool splines_match(const pcg::internal::data::PcgSplineData& actual,
                   const nlohmann::json& expected,
                   const char* label)
{
    if (!expected.contains("splines") || !expected["splines"].is_array()) {
        std::printf("FAIL: %s expected.splines missing\n", label);
        return false;
    }
    const auto& expected_splines = expected["splines"];
    if (actual.splines().size() != expected_splines.size()) {
        std::printf("FAIL: %s spline count %zu != %zu\n", label, actual.splines().size(),
                    expected_splines.size());
        return false;
    }
    for (size_t i = 0; i < actual.splines().size(); ++i) {
        const auto& spline = actual.splines()[i];
        const auto& exp = expected_splines[i];
        if (spline.closed != exp.value("closed", false)) {
            std::printf("FAIL: %s spline[%zu] closed mismatch\n", label, i);
            return false;
        }
        if (exp.contains("attributes") && !spline.attributes.empty()) {
            if (spline.attributes != exp["attributes"]) {
                std::printf("FAIL: %s spline[%zu] attributes mismatch\n", label, i);
                return false;
            }
        }
        if (!exp.contains("points") || !exp["points"].is_array() ||
            spline.points.size() != exp["points"].size()) {
            std::printf("FAIL: %s spline[%zu] point count %zu\n", label, i, spline.points.size());
            return false;
        }
        for (size_t p = 0; p < spline.points.size(); ++p) {
            const auto& pt = spline.points[p];
            const auto& ept = exp["points"][p];
            if (!nearly_equal(pt.x, ept.value("x", 0.0)) ||
                !nearly_equal(pt.y, ept.value("y", 0.0)) ||
                !nearly_equal(pt.z, ept.value("z", 0.0))) {
                std::printf("FAIL: %s spline[%zu] point[%zu] got (%.17g,%.17g,%.17g)\n", label, i, p,
                            pt.x, pt.y, pt.z);
                return false;
            }
        }
    }
    return true;
}

bool run_condition_outline_golden()
{
    using namespace pcg::internal::elements;
    using namespace pcg::internal::data;

    std::ifstream in("fixtures/condition-outline-golden.json");
    if (!in) {
        std::printf("FAIL: cannot open fixtures/condition-outline-golden.json\n");
        return false;
    }
    nlohmann::json root;
    in >> root;
    if (!root.contains("cases") || !root["cases"].is_array()) {
        std::printf("FAIL: golden cases missing\n");
        return false;
    }

    for (const auto& test_case : root["cases"]) {
        const std::string id = test_case.value("id", std::string("case"));
        ConditionOutlineOptions opts;
        opts.win = test_case.value("win", 1);
        opts.eps = test_case.value("eps", 0.0);
        if (test_case.contains("protectSpans") && test_case["protectSpans"].is_array()) {
            for (const auto& item : test_case["protectSpans"]) {
                ProtectSpan span;
                span.start = item.value("start", 0);
                span.end = item.value("end", 0);
                opts.protect_spans.push_back(span);
            }
        }
        const PcgSplineData input = PcgSplineData::from_json(test_case["input"]);
        std::string error;
        const PcgSplineData actual = condition_outline_data(input, opts, &error);
        if (!error.empty()) {
            std::printf("FAIL: %s unexpected error: %s\n", id.c_str(), error.c_str());
            return false;
        }
        if (!splines_match(actual, test_case["expected"], id.c_str()))
            return false;

        // AC1: protected input coordinates remain bit-identical after conditioning.
        if (!opts.protect_spans.empty() && !input.splines().empty()) {
            const auto& src = input.splines().front();
            const auto& dst = actual.splines().front();
            std::vector<char> mask(src.points.size(), 0);
            for (const auto& span : opts.protect_spans) {
                if (span.start <= span.end) {
                    for (int i = span.start; i <= span.end; ++i)
                        mask[static_cast<size_t>(i)] = 1;
                } else {
                    for (int i = span.start; i < static_cast<int>(src.points.size()); ++i)
                        mask[static_cast<size_t>(i)] = 1;
                    for (int i = 0; i <= span.end; ++i)
                        mask[static_cast<size_t>(i)] = 1;
                }
            }
            size_t dst_i = 0;
            for (size_t i = 0; i < src.points.size(); ++i) {
                if (!mask[i])
                    continue;
                bool found = false;
                for (; dst_i < dst.points.size(); ++dst_i) {
                    if (nearly_equal(dst.points[dst_i].x, src.points[i].x) &&
                        nearly_equal(dst.points[dst_i].y, src.points[i].y) &&
                        nearly_equal(dst.points[dst_i].z, src.points[i].z)) {
                        found = true;
                        ++dst_i;
                        break;
                    }
                }
                if (!found && src.closed && i + 1 == src.points.size() &&
                    !dst.points.empty() &&
                    nearly_equal(dst.points.front().x, src.points[i].x) &&
                    nearly_equal(dst.points.front().y, src.points[i].y) &&
                    nearly_equal(dst.points.front().z, src.points[i].z)) {
                    // Active conditioning canonicalizes an explicit closing duplicate
                    // to the logical first point while retaining closed=true.
                    found = true;
                }
                if (!found) {
                    std::printf("FAIL: %s protected input point %zu missing in output\n", id.c_str(),
                                i);
                    return false;
                }
            }
            if (src.closed != dst.closed) {
                std::printf("FAIL: %s closed flag changed\n", id.c_str());
                return false;
            }
        }
        std::printf("PASS: condition_outline golden %s\n", id.c_str());
    }

    // Illegal parameter coverage (AC2).
    {
        PcgSpline open;
        open.closed = false;
        open.points = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
        PcgSplineData input;
        input.add_spline(open);

        ConditionOutlineOptions bad_win;
        bad_win.win = 2;
        std::string error;
        condition_outline_data(input, bad_win, &error);
        if (error.find("odd") == std::string::npos) {
            std::printf("FAIL: even win should error\n");
            return false;
        }

        ConditionOutlineOptions bad_eps;
        bad_eps.eps = std::numeric_limits<double>::quiet_NaN();
        error.clear();
        condition_outline_data(input, bad_eps, &error);
        if (error.find("eps") == std::string::npos) {
            std::printf("FAIL: non-finite eps should error\n");
            return false;
        }

        ConditionOutlineOptions bad_span;
        bad_span.protect_spans.push_back({0, 99});
        error.clear();
        condition_outline_data(input, bad_span, &error);
        if (error.find("out of range") == std::string::npos) {
            std::printf("FAIL: OOB protectSpans should error\n");
            return false;
        }

        ConditionOutlineOptions seam_on_open;
        seam_on_open.protect_spans.push_back({3, 0});
        error.clear();
        condition_outline_data(input, seam_on_open, &error);
        if (error.find("closed") == std::string::npos) {
            std::printf("FAIL: cross-seam on open should error\n");
            return false;
        }
        std::printf("PASS: condition_outline illegal parameter errors\n");
    }

    return true;
}

bool run_condition_outline_graph()
{
    using namespace pcg::internal::data;

    // CMake add_test WORKING_DIRECTORY = ${CMAKE_CURRENT_SOURCE_DIR}/tests (= pcg-core/tests).
    const char* candidates[] = {
        "../../examples/Test/test-condition-outline.pcg",
        "../examples/Test/test-condition-outline.pcg",
        "examples/Test/test-condition-outline.pcg",
    };
    std::ifstream gin;
    std::string graph_path;
    for (const char* path : candidates) {
        gin.open(path);
        if (gin) {
            graph_path = path;
            break;
        }
        gin.clear();
    }
    if (!gin) {
        std::printf("FAIL: cannot open test-condition-outline.pcg\n");
        return false;
    }
    std::string graph((std::istreambuf_iterator<char>(gin)), std::istreambuf_iterator<char>());

    std::vector<char> json_out(1 << 20);
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    char err[512] = {};
    const PcgResultCode rc = pcg_execute_graph_v7(
        graph.c_str(), 42, nullptr, 0, nullptr, 0, nullptr, 0, &kind, json_out.data(),
        static_cast<int>(json_out.size()), nullptr, 0, nullptr, 0, nullptr, nullptr, &vertex_count,
        &index_count, nullptr, nullptr, 0, err, sizeof(err));
    if (rc != PCG_OK) {
        std::printf("FAIL: condition outline graph cook (%s): %s\n", graph_path.c_str(), err);
        return false;
    }
    if (kind != PCG_RESULT_KIND_JSON) {
        std::printf("FAIL: condition outline graph kind=%d expected JSON\n", kind);
        return false;
    }
    const auto payload = nlohmann::json::parse(json_out.data());
    if (!payload.contains("splines") || !payload["splines"].is_array() ||
        payload["splines"].empty()) {
        std::printf("FAIL: condition outline graph JSON missing splines\n");
        return false;
    }

    std::ifstream golden_in("fixtures/condition-outline-golden.json");
    if (!golden_in) {
        std::printf("FAIL: cannot open condition-outline golden for graph comparison\n");
        return false;
    }
    nlohmann::json golden_root;
    golden_in >> golden_root;
    nlohmann::json expected_case;
    for (const auto& test_case : golden_root.value("cases", nlohmann::json::array())) {
        if (test_case.value("id", std::string()) == "closed_duplicate_smooth") {
            expected_case = test_case;
            break;
        }
    }
    if (!expected_case.is_object()) {
        std::printf("FAIL: closed_duplicate_smooth golden case missing\n");
        return false;
    }

    const PcgSplineData actual = PcgSplineData::from_json(payload);
    if (!splines_match(actual, expected_case["expected"], "condition outline graph")) {
        return false;
    }
    std::printf("PASS: condition outline graph matches golden (%s, %s)\n",
                expected_case.value("id", std::string("case")).c_str(), graph_path.c_str());
    return true;
}

} // namespace

int main()
{
    using namespace pcg::internal::elements;
    using namespace pcg::internal::geometry;

    CreateSplineOptions create_opts;
    create_opts.mode = "line";
    create_opts.start_x = 0.0;
    create_opts.end_x = 20.0;
    const auto spline = create_spline_data(create_opts);
    if (spline.splines().empty() || spline.splines().front().points.size() < 2)
    {
        std::printf("FAIL: create_spline_data line\n");
        return 1;
    }
    std::printf("PASS: create_spline_data line (%zu points)\n",
                spline.splines().front().points.size());

    ExtrudeAlongSplineOptions extrude_opts;
    extrude_opts.profile_width = 4.0;
    extrude_opts.profile_height = 0.5;
    extrude_opts.sample_spacing = 1.0;
    const auto deck = extrude_along_spline(spline, nullptr, extrude_opts);
    if (deck.vertices().empty() || deck.triangles().empty())
    {
        std::printf("FAIL: extrude_along_spline unit\n");
        return 1;
    }
    std::printf("PASS: extrude_along_spline unit (%zu verts, %zu tris)\n", deck.vertices().size(),
                deck.triangles().size() / 3);

    int extrude_left_z = 0;
    int extrude_right_z = 0;
    for (const auto& vertex : deck.vertices())
    {
        if (std::abs(vertex.x) > 0.05 || std::abs(vertex.y + 0.25) > 0.05)
            continue;
        if (vertex.z > 1.5)
            ++extrude_left_z;
        else if (vertex.z < -1.5)
            ++extrude_right_z;
    }
    if (extrude_left_z == 0 || extrude_right_z == 0)
    {
        std::printf("FAIL: extrude width corners missing (left=%d right=%d)\n", extrude_left_z,
                    extrude_right_z);
        return 1;
    }

    int extrude_outward_width = 0;
    int extrude_inward_width = 0;
    for (size_t i = 0; i + 2 < deck.triangles().size(); i += 3)
    {
        const auto& a = deck.vertices()[static_cast<size_t>(deck.triangles()[i])];
        const auto& b = deck.vertices()[static_cast<size_t>(deck.triangles()[i + 1])];
        const auto& c = deck.vertices()[static_cast<size_t>(deck.triangles()[i + 2])];
        const Vec3 va{a.x, a.y, a.z};
        const Vec3 vb{b.x, b.y, b.z};
        const Vec3 vc{c.x, c.y, c.z};
        const Vec3 n = normalize(cross(sub(vb, va), sub(vc, va)));
        if (std::abs(n.y) > 0.5)
            continue;
        if (std::abs(n.z) <= 0.5)
            continue;

        const Vec3 center = scale(add(add(va, vb), vc), 1.0 / 3.0);
        const double outward = dot(n, center);
        if (outward > 0.0)
            ++extrude_outward_width;
        else
            ++extrude_inward_width;
    }
    if (extrude_outward_width == 0 || extrude_inward_width > extrude_outward_width / 2)
    {
        std::printf("FAIL: extrude width-face normals should face outward (out=%d in=%d)\n",
                    extrude_outward_width, extrude_inward_width);
        return 1;
    }
    std::printf("PASS: extrude width orientation + outward side normals (out=%d in=%d)\n",
                extrude_outward_width, extrude_inward_width);

    SweepAlongSplineOptions sweep_opts;
    sweep_opts.surface_shape = "rectangle";
    sweep_opts.profile_width = 4.0;
    sweep_opts.profile_height = 0.5;
    sweep_opts.sample_spacing = 1.0;
    const auto swept_rect = sweep_along_spline(spline, nullptr, sweep_opts);
    if (swept_rect.vertices().empty() || swept_rect.triangles().empty())
    {
        std::printf("FAIL: sweep_along_spline rectangle\n");
        return 1;
    }
    std::printf("PASS: sweep_along_spline rectangle (%zu verts, %zu tris)\n",
                swept_rect.vertices().size(), swept_rect.triangles().size() / 3);

    const std::vector<Vec3> axis_line = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {20.0, 0.0, 0.0}};
    const auto axis_frames = build_frames(axis_line, {0.0, 1.0, 0.0});
    if (axis_frames.empty() || axis_frames.front().normal.y < 0.9)
    {
        std::printf("FAIL: build_frames should keep +Y as profile up for +X backbone\n");
        return 1;
    }
    std::printf("PASS: build_frames +X backbone keeps +Y normal\n");

    const std::vector<Vec3> inflected_line = {
        {0.0, 0.0, 0.0}, {0.8, 0.8, 0.0}, {1.6, 0.0, 0.0},
        {2.4, -0.8, 0.0}, {3.2, 0.0, 0.0},
    };
    const auto transported_frames =
        build_parallel_transport_frames(inflected_line, {0.0, 0.0, 1.0});
    if (transported_frames.size() != inflected_line.size()) {
        std::printf("FAIL: parallel transport frame count\n");
        return 1;
    }
    for (size_t i = 1; i < transported_frames.size(); ++i) {
        if (dot(transported_frames[i - 1].normal, transported_frames[i].normal) <= 0.0) {
            std::printf("FAIL: parallel transport normal flipped at frame %zu\n", i);
            return 1;
        }
    }
    std::printf("PASS: parallel transport frames stay continuous through an inflection\n");

    CreateSplineOptions taper_path_options;
    taper_path_options.mode = "polyline";
    taper_path_options.control_points = {
        {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 2.0, 0.0},
    };
    const auto taper_path = create_spline_data(taper_path_options);
    TaperedSweepOptions taper_options;
    taper_options.stations = {
        {0.0, 0.30, 0.20, 0.0},
        {0.5, 0.18, 0.10, 25.0},
        {1.0, 0.0, 0.0, 45.0},
    };
    taper_options.radial_segments = 8;
    taper_options.sample_spacing = 1.0;
    taper_options.cap_start = true;
    taper_options.cap_end = true;
    std::string taper_error;
    const auto tapered =
        tapered_sweep_along_spline_geometry(taper_path, taper_options, &taper_error);
    if (!taper_error.empty() || tapered.points().size() != 18 || tapered.faces().size() != 24) {
        std::printf("FAIL: tapered sweep shape (points=%zu faces=%zu error=%s)\n",
                    tapered.points().size(), tapered.faces().size(), taper_error.c_str());
        return 1;
    }
    if (!tapered.has_corner_uvs() ||
        tapered.corner_uvs().size() != static_cast<size_t>(tapered.corner_count())) {
        std::printf("FAIL: tapered sweep corner UV coverage\n");
        return 1;
    }
    if (!tapered.groups().members(GroupDomain::Edge, "unshared").empty()) {
        std::printf("FAIL: capped tapered sweep should be watertight\n");
        return 1;
    }

    const auto tapered_mesh = pcg::internal::data::triangulate_geometry_shared(tapered);
    double tapered_signed_volume = 0.0;
    for (size_t i = 0; i + 2 < tapered_mesh.triangles().size(); i += 3) {
        const auto& a = tapered_mesh.vertices()[static_cast<size_t>(tapered_mesh.triangles()[i])];
        const auto& b = tapered_mesh.vertices()[static_cast<size_t>(tapered_mesh.triangles()[i + 1])];
        const auto& c = tapered_mesh.vertices()[static_cast<size_t>(tapered_mesh.triangles()[i + 2])];
        tapered_signed_volume +=
            (a.x * (b.y * c.z - b.z * c.y) +
             a.y * (b.z * c.x - b.x * c.z) +
             a.z * (b.x * c.y - b.y * c.x)) /
            6.0;
    }
    if (tapered_signed_volume <= 0.0) {
        std::printf("FAIL: tapered sweep winding is not outward (volume=%.9f)\n",
                    tapered_signed_volume);
        return 1;
    }
    std::printf("PASS: tapered sweep true point, UVs, manifold, outward winding (volume=%.6f)\n",
                tapered_signed_volume);

    double upward_sum = 0.0;
    int upward_count = 0;
    for (size_t i = 0; i + 2 < swept_rect.triangles().size(); i += 3)
    {
        const auto& a = swept_rect.vertices()[static_cast<size_t>(swept_rect.triangles()[i])];
        const auto& b = swept_rect.vertices()[static_cast<size_t>(swept_rect.triangles()[i + 1])];
        const auto& c = swept_rect.vertices()[static_cast<size_t>(swept_rect.triangles()[i + 2])];
        const Vec3 va{a.x, a.y, a.z};
        const Vec3 vb{b.x, b.y, b.z};
        const Vec3 vc{c.x, c.y, c.z};
        const Vec3 n = normalize(cross(sub(vb, va), sub(vc, va)));
        if (n.y > 0.5)
        {
            upward_sum += n.y;
            ++upward_count;
        }
    }
    if (upward_count == 0 || upward_sum / static_cast<double>(upward_count) < 0.8)
    {
        std::printf("FAIL: sweep rectangle normals should face +Y (count=%d)\n", upward_count);
        return 1;
    }
    std::printf("PASS: sweep rectangle upward normals (avg=%.3f, count=%d)\n",
                upward_sum / static_cast<double>(upward_count), upward_count);

    CreateSplineOptions profile_opts_create;
    profile_opts_create.mode = "polyline";
    profile_opts_create.closed = true;
    profile_opts_create.control_points = {
        {-2.0, -0.25, 0.0},
        {2.0, -0.25, 0.0},
        {2.0, 0.25, 0.0},
        {-2.0, 0.25, 0.0},
    };
    const auto profile_spline = create_spline_data(profile_opts_create);

    SweepAlongSplineOptions curve_sweep_opts;
    curve_sweep_opts.surface_shape = "crossSection";
    curve_sweep_opts.use_profile_spline = true;
    curve_sweep_opts.sample_spacing = 1.0;
    const auto swept_curve = sweep_along_spline(spline, &profile_spline, curve_sweep_opts);
    if (swept_curve.vertices().empty() || swept_curve.triangles().empty())
    {
        std::printf("FAIL: sweep_along_spline profile curve\n");
        return 1;
    }
    std::printf("PASS: sweep_along_spline profile curve (%zu verts, %zu tris)\n",
                swept_curve.vertices().size(), swept_curve.triangles().size() / 3);

    const auto box_profile = create_box_mesh(4.0, 1.0, 0.05);
    ExtrudeAlongSplineOptions profile_opts;
    profile_opts.sample_spacing = 1.0;
    profile_opts.use_profile_mesh = true;
    const auto swept_profile = extrude_along_spline(spline, &box_profile, profile_opts);
    if (swept_profile.vertices().empty() || swept_profile.triangles().empty())
    {
        std::printf("FAIL: extrude_along_spline profile mesh sweep\n");
        return 1;
    }
    std::printf("PASS: extrude_along_spline profile mesh sweep (%zu verts, %zu tris)\n",
                swept_profile.vertices().size(), swept_profile.triangles().size() / 3);

    const auto extracted = extract_cross_section_profile(box_profile, CrossSectionProfileOptions{});
    if (extracted.vertices().size() < 4 || extracted.triangles().size() < 6)
    {
        std::printf("FAIL: extract_cross_section_profile\n");
        return 1;
    }
    std::printf("PASS: extract_cross_section_profile (%zu verts, %zu tris)\n",
                extracted.vertices().size(), extracted.triangles().size() / 3);

    const char* bridge_graph = R"({
      "version": "1.0",
      "nodes": [
        {
          "id": "path",
          "type": "CreateSpline",
          "position": { "x": 0, "y": 0 },
          "data": {
            "mode": "catmullRom",
            "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":12,\"y\":3,\"z\":8},{\"x\":28,\"y\":0,\"z\":12},{\"x\":40,\"y\":0,\"z\":0}]",
            "subdivisions": 6
          }
        },
        {
          "id": "deck_profile",
          "type": "CreateSpline",
          "position": { "x": 0, "y": -180 },
          "data": {
            "mode": "polyline",
            "closed": true,
            "controlPoints": "[{\"x\":-3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":0.2,\"z\":0},{\"x\":-3,\"y\":0.2,\"z\":0}]"
          }
        },
        {
          "id": "deck",
          "type": "SweepAlongSpline",
          "position": { "x": 300, "y": 0 },
          "data": { "surfaceShape": "crossSection", "sampleSpacing": 1.0 }
        },
        {
          "id": "pier_proto",
          "type": "CreateBoxMesh",
          "position": { "x": 0, "y": 200 },
          "data": { "width": 1.0, "height": 6.0, "depth": 1.0 }
        },
        {
          "id": "piers",
          "type": "InstanceAlongSpline",
          "position": { "x": 300, "y": 200 },
          "data": { "spacing": 10.0, "includeEnd": false, "scale": 1.0 }
        },
        {
          "id": "merge",
          "type": "MergeMesh",
          "position": { "x": 600, "y": 100 },
          "data": {}
        },
        {
          "id": "out",
          "type": "Output",
          "position": { "x": 900, "y": 100 },
          "data": { "label": "Bridge" }
        }
      ],
      "edges": [
        { "id": "e1", "source": "path", "target": "deck", "sourceHandle": "out", "targetHandle": "backbone" },
        { "id": "e1b", "source": "deck_profile", "target": "deck", "sourceHandle": "out", "targetHandle": "profile" },
        { "id": "e2", "source": "path", "target": "piers", "sourceHandle": "out", "targetHandle": "spline" },
        { "id": "e3", "source": "pier_proto", "target": "piers", "sourceHandle": "out", "targetHandle": "mesh" },
        { "id": "e4", "source": "deck", "target": "merge", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e5", "source": "piers", "target": "merge", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e6", "source": "merge", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
      ]
    })";

    int vertex_count = 0;
    int index_count = 0;
    expect_code(execute_mesh_graph(bridge_graph, 42, vertex_count, index_count), PCG_OK,
                "bridge graph execute");
    if (vertex_count < 100 || index_count < 100)
    {
        std::printf("FAIL: bridge mesh too small (%d verts, %d indices)\n", vertex_count,
                    index_count);
        return 1;
    }
    std::printf("PASS: bridge graph mesh (%d verts, %d indices)\n", vertex_count, index_count);

    // Closed-backbone sweep: every edge must be shared by exactly 2 triangles.
    CreateSplineOptions ring_opts;
    ring_opts.mode = "catmullRom";
    ring_opts.closed = true;
    ring_opts.subdivisions = 8;
    ring_opts.control_points = {
        {0.4, 0.0, 0.0}, {0.283, 0.0, 0.283}, {0.0, 0.0, 0.4},
        {-0.283, 0.0, 0.283}, {-0.4, 0.0, 0.0}, {-0.283, 0.0, -0.283},
        {0.0, 0.0, -0.4}, {0.283, 0.0, -0.283},
    };
    const auto ring_spline = create_spline_data(ring_opts);

    SweepAlongSplineOptions ring_sweep_opts;
    ring_sweep_opts.surface_shape = "circle";
    ring_sweep_opts.radius = 0.04;
    ring_sweep_opts.columns = 12;
    ring_sweep_opts.sample_spacing = 0.15;
    const auto ring_mesh = sweep_along_spline(ring_spline, nullptr, ring_sweep_opts);
    if (ring_mesh.vertices().empty() || ring_mesh.triangles().empty())
    {
        std::printf("FAIL: closed-backbone sweep produced empty mesh\n");
        return 1;
    }

    {
        std::unordered_map<int64_t, int> edge_count;
        const auto tri = ring_mesh.triangles();
        for (size_t i = 0; i + 2 < tri.size(); i += 3)
        {
            int a = tri[i], b = tri[i + 1], c = tri[i + 2];
            auto add_edge = [&](int x, int y) {
                if (x > y) std::swap(x, y);
                edge_count[static_cast<int64_t>(x) * 1000000 + y]++;
            };
            add_edge(a, b);
            add_edge(b, c);
            add_edge(c, a);
        }
        int bad_edges = 0;
        for (const auto& [key, count] : edge_count)
            if (count != 2) ++bad_edges;
        if (bad_edges != 0)
        {
            std::printf("FAIL: closed-backbone sweep has %d bad edges (expected 0)\n", bad_edges);
            return 1;
        }
    }
    std::printf("PASS: closed-backbone sweep is closed manifold (%zu verts, %zu tris)\n",
                ring_mesh.vertices().size(), ring_mesh.triangles().size() / 3);

    if (!run_condition_outline_golden())
        return 1;
    if (!run_condition_outline_graph())
        return 1;

    std::printf("All phase45 spline tests passed.\n");
    return 0;
}
