#include "elements/structural_algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

constexpr double kEps = 1e-9;

double cross(const Vec2D& o, const Vec2D& a, const Vec2D& b)
{
    return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
}

double dist2(const Vec2D& a, const Vec2D& b)
{
    const double dx = a.x - b.x;
    const double dz = a.z - b.z;
    return dx * dx + dz * dz;
}

data::PcgSpline make_segment_spline(const data::PcgPoint& a, const data::PcgPoint& b)
{
    data::PcgSpline spline;
    spline.points.push_back({a.x, a.y, a.z});
    spline.points.push_back({b.x, b.y, b.z});
    return spline;
}

data::PcgSplineData edges_to_splines(const std::vector<Edge2D>& edges, const data::PcgPointData& points)
{
    data::PcgSplineData out;
    const auto& pts = points.points();
    for (const Edge2D& edge : edges) {
        if (edge.a < 0 || edge.b < 0 || edge.a >= static_cast<int>(pts.size()) ||
            edge.b >= static_cast<int>(pts.size()))
            continue;
        out.add_spline(make_segment_spline(pts[static_cast<size_t>(edge.a)], pts[static_cast<size_t>(edge.b)]));
    }
    return out;
}

struct Triangle {
    int a = 0;
    int b = 0;
    int c = 0;
};

bool in_circumcircle(const Vec2D& p, const Triangle& tri, const std::vector<Vec2D>& coords)
{
    const Vec2D& a = coords[static_cast<size_t>(tri.a)];
    const Vec2D& b = coords[static_cast<size_t>(tri.b)];
    const Vec2D& c = coords[static_cast<size_t>(tri.c)];

    const double ax = a.x - p.x;
    const double az = a.z - p.z;
    const double bx = b.x - p.x;
    const double bz = b.z - p.z;
    const double cx = c.x - p.x;
    const double cz = c.z - p.z;

    const double det = (ax * ax + az * az) * (bx * cz - cx * bz) -
                       (bx * bx + bz * bz) * (ax * cz - cx * ax) +
                       (cx * cx + cz * cz) * (ax * bz - bx * ax);
    return det > kEps;
}

std::vector<Triangle> bowyer_watson(const std::vector<Vec2D>& coords)
{
    if (coords.size() < 3)
        return {};

    double min_x = coords[0].x;
    double max_x = coords[0].x;
    double min_z = coords[0].z;
    double max_z = coords[0].z;
    for (const Vec2D& p : coords) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_z = std::min(min_z, p.z);
        max_z = std::max(max_z, p.z);
    }

    const double dx = max_x - min_x;
    const double dz = max_z - min_z;
    const double delta = std::max(dx, dz) * 20.0 + 1.0;
    const Vec2D p1{min_x - delta, min_z - delta};
    const Vec2D p2{min_x + 2.0 * delta, min_z - delta};
    const Vec2D p3{min_x + delta * 0.5, max_z + 2.0 * delta};

    std::vector<Vec2D> all = coords;
    const int super_a = static_cast<int>(all.size());
    all.push_back(p1);
    const int super_b = static_cast<int>(all.size());
    all.push_back(p2);
    const int super_c = static_cast<int>(all.size());
    all.push_back(p3);

    std::vector<Triangle> triangles{{super_a, super_b, super_c}};

    for (int i = 0; i < static_cast<int>(coords.size()); ++i) {
        std::vector<Triangle> bad;
        for (const Triangle& tri : triangles) {
            if (in_circumcircle(coords[static_cast<size_t>(i)], tri, all))
                bad.push_back(tri);
        }

        std::vector<std::pair<int, int>> polygon;
        for (const Triangle& tri : bad) {
            const int edges[3][2] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
            for (const auto& edge : edges) {
                auto it = std::find_if(polygon.begin(), polygon.end(), [&](const std::pair<int, int>& e) {
                    return (e.first == edge[1] && e.second == edge[0]);
                });
                if (it != polygon.end())
                    polygon.erase(it);
                else
                    polygon.emplace_back(edge[0], edge[1]);
            }
        }

        triangles.erase(std::remove_if(triangles.begin(),
                                       triangles.end(),
                                       [&](const Triangle& tri) {
                                           return std::find_if(bad.begin(), bad.end(), [&](const Triangle& b) {
                                                      return b.a == tri.a && b.b == tri.b && b.c == tri.c;
                                                  }) != bad.end();
                                       }),
                        triangles.end());

        for (const auto& edge : polygon)
            triangles.push_back({i, edge.first, edge.second});
    }

    triangles.erase(std::remove_if(triangles.begin(),
                                   triangles.end(),
                                   [&](const Triangle& tri) {
                                       return tri.a >= static_cast<int>(coords.size()) ||
                                              tri.b >= static_cast<int>(coords.size()) ||
                                              tri.c >= static_cast<int>(coords.size());
                                   }),
                    triangles.end());
    return triangles;
}

std::vector<Edge2D> unique_edges_from_triangles(const std::vector<Triangle>& triangles,
                                                const std::vector<Vec2D>& coords)
{
    std::set<std::pair<int, int>> seen;
    std::vector<Edge2D> edges;
    for (const Triangle& tri : triangles) {
        const int pairs[3][2] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
        for (const auto& pair : pairs) {
            const int a = std::min(pair[0], pair[1]);
            const int b = std::max(pair[0], pair[1]);
            if (seen.count({a, b}))
                continue;
            seen.insert({a, b});
            edges.push_back({a, b, std::sqrt(dist2(coords[static_cast<size_t>(a)], coords[static_cast<size_t>(b)]))});
        }
    }
    return edges;
}

struct UnionFind {
    std::vector<int> parent;

    explicit UnionFind(int n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }

    int find(int x)
    {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }

    bool unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b)
            return false;
        parent[static_cast<size_t>(a)] = b;
        return true;
    }
};

Vec2D circumcenter(const Vec2D& a, const Vec2D& b, const Vec2D& c)
{
    const double d = 2.0 * (a.x * (b.z - c.z) + b.x * (c.z - a.z) + c.x * (a.z - b.z));
    if (std::abs(d) < kEps)
        return {(a.x + b.x + c.x) / 3.0, (a.z + b.z + c.z) / 3.0};

    const double a2 = a.x * a.x + a.z * a.z;
    const double b2 = b.x * b.x + b.z * b.z;
    const double c2 = c.x * c.x + c.z * c.z;
    const double ux = (a2 * (b.z - c.z) + b2 * (c.z - a.z) + c2 * (a.z - b.z)) / d;
    const double uz = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    return {ux, uz};
}

} // namespace

std::vector<Vec2D> points_to_xz(const data::PcgPointData& points)
{
    std::vector<Vec2D> out;
    out.reserve(points.points().size());
    for (const auto& p : points.points())
        out.push_back({p.x, p.z});
    return out;
}

data::PcgSplineData convex_hull_spline(const data::PcgPointData& points, double tolerance)
{
    data::PcgSplineData out;
    const auto& pts = points.points();
    if (pts.size() < 3)
        return out;

    const double tol = std::max(tolerance, kEps);

    const std::vector<Vec2D> coords = points_to_xz(points);
    std::vector<int> order(coords.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (std::abs(coords[static_cast<size_t>(a)].x - coords[static_cast<size_t>(b)].x) > kEps)
            return coords[static_cast<size_t>(a)].x < coords[static_cast<size_t>(b)].x;
        return coords[static_cast<size_t>(a)].z < coords[static_cast<size_t>(b)].z;
    });

    std::vector<int> hull;
    for (int idx : order) {
        while (hull.size() >= 2 &&
               cross(coords[static_cast<size_t>(hull[hull.size() - 2])],
                     coords[static_cast<size_t>(hull.back())],
                     coords[static_cast<size_t>(idx)]) <= tol)
            hull.pop_back();
        hull.push_back(idx);
    }

    const size_t lower_size = hull.size();
    for (int i = static_cast<int>(order.size()) - 2; i >= 0; --i) {
        const int idx = order[static_cast<size_t>(i)];
        while (hull.size() > lower_size &&
               cross(coords[static_cast<size_t>(hull[hull.size() - 2])],
                     coords[static_cast<size_t>(hull.back())],
                     coords[static_cast<size_t>(idx)]) <= tol)
            hull.pop_back();
        hull.push_back(idx);
    }

    if (hull.size() > 1)
        hull.pop_back();

    data::PcgSpline spline;
    spline.closed = true;
    for (int idx : hull) {
        const auto& p = pts[static_cast<size_t>(idx)];
        spline.points.push_back({p.x, p.y, p.z});
    }
    if (!spline.points.empty())
        out.add_spline(std::move(spline));
    return out;
}

data::PcgSplineData connect_nearest_splines(const data::PcgPointData& points, int k, double max_distance)
{
    data::PcgSplineData out;
    const auto& pts = points.points();
    const int n = static_cast<int>(pts.size());
    if (n < 2 || k <= 0)
        return out;

    const std::vector<Vec2D> coords = points_to_xz(points);
    const double max_dist2 = max_distance >= 0.0 ? max_distance * max_distance : std::numeric_limits<double>::max();

    std::set<std::pair<int, int>> seen;
    for (int i = 0; i < n; ++i) {
        std::vector<std::pair<double, int>> neighbors;
        for (int j = 0; j < n; ++j) {
            if (i == j)
                continue;
            const double d2 = dist2(coords[static_cast<size_t>(i)], coords[static_cast<size_t>(j)]);
            if (d2 <= max_dist2)
                neighbors.emplace_back(d2, j);
        }
        std::sort(neighbors.begin(), neighbors.end());
        const int limit = std::min(k, static_cast<int>(neighbors.size()));
        for (int ni = 0; ni < limit; ++ni) {
            const int j = neighbors[static_cast<size_t>(ni)].second;
            const int a = std::min(i, j);
            const int b = std::max(i, j);
            if (seen.count({a, b}))
                continue;
            seen.insert({a, b});
            out.add_spline(make_segment_spline(pts[static_cast<size_t>(a)], pts[static_cast<size_t>(b)]));
        }
    }
    return out;
}

data::PcgSplineData delaunay_edge_splines(const data::PcgPointData& points)
{
    const std::vector<Vec2D> coords = points_to_xz(points);
    if (coords.size() < 3)
        return {};

    const std::vector<Triangle> triangles = bowyer_watson(coords);
    return edges_to_splines(unique_edges_from_triangles(triangles, coords), points);
}

data::PcgSplineData mst_splines(const data::PcgSplineData& edges, const data::PcgPointData& points)
{
    std::vector<Edge2D> edge_list = extract_edges_from_splines(edges, points);
    if (edge_list.empty())
        return {};

    std::sort(edge_list.begin(), edge_list.end(), [](const Edge2D& a, const Edge2D& b) {
        return a.weight < b.weight;
    });

    const int n = static_cast<int>(points.points().size());
    UnionFind uf(n);
    std::vector<Edge2D> mst;
    for (const Edge2D& edge : edge_list) {
        if (uf.unite(edge.a, edge.b))
            mst.push_back(edge);
    }
    return edges_to_splines(mst, points);
}

data::PcgSplineData voronoi_edge_splines(const data::PcgPointData& points)
{
    data::PcgSplineData out;
    const std::vector<Vec2D> coords = points_to_xz(points);
    if (coords.size() < 3)
        return out;

    const std::vector<Triangle> triangles = bowyer_watson(coords);
    std::unordered_map<long long, Vec2D> centers;
    auto key = [](const Vec2D& v) -> long long {
        return static_cast<long long>(std::lround(v.x * 1000.0)) * 1000003LL +
               static_cast<long long>(std::lround(v.z * 1000.0));
    };

    for (const Triangle& tri : triangles) {
        const Vec2D cc = circumcenter(coords[static_cast<size_t>(tri.a)],
                                      coords[static_cast<size_t>(tri.b)],
                                      coords[static_cast<size_t>(tri.c)]);
        centers[key(cc)] = cc;
    }

    std::set<std::pair<long long, long long>> seen;
    for (const Triangle& tri : triangles) {
        const Vec2D c0 = circumcenter(coords[static_cast<size_t>(tri.a)],
                                      coords[static_cast<size_t>(tri.b)],
                                      coords[static_cast<size_t>(tri.c)]);
        const long long k0 = key(c0);

        const int pairs[3][2] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
        for (const auto& pair : pairs) {
            for (const Triangle& other : triangles) {
                if (other.a == tri.a && other.b == tri.b && other.c == tri.c)
                    continue;
                const int op[3] = {other.a, other.b, other.c};
                bool shares = false;
                for (int i = 0; i < 3 && !shares; ++i) {
                    for (int j = 0; j < 3; ++j) {
                        if ((op[i] == pair[0] && op[j] == pair[1]) || (op[i] == pair[1] && op[j] == pair[0])) {
                            shares = true;
                            break;
                        }
                    }
                }
                if (!shares)
                    continue;

                const Vec2D c1 = circumcenter(coords[static_cast<size_t>(other.a)],
                                              coords[static_cast<size_t>(other.b)],
                                              coords[static_cast<size_t>(other.c)]);
                const long long k1 = key(c1);
                if (k0 == k1)
                    continue;
                const long long a = std::min(k0, k1);
                const long long b = std::max(k0, k1);
                if (seen.count({a, b}))
                    continue;
                seen.insert({a, b});

                data::PcgSpline spline;
                spline.points.push_back({c0.x, 0.0, c0.z});
                spline.points.push_back({c1.x, 0.0, c1.z});
                out.add_spline(std::move(spline));
            }
        }
    }
    return out;
}

data::PcgSplineData astar_path_spline(const data::PcgSplineData& edges,
                                      const data::PcgPointData& points,
                                      int start_index,
                                      int end_index)
{
    data::PcgSplineData out;
    const auto& pts = points.points();
    const int n = static_cast<int>(pts.size());
    if (n == 0 || start_index < 0 || end_index < 0 || start_index >= n || end_index >= n)
        return out;

    std::vector<Edge2D> edge_list = extract_edges_from_splines(edges, points);
    std::vector<std::vector<std::pair<int, double>>> adj(static_cast<size_t>(n));
    for (const Edge2D& edge : edge_list) {
        adj[static_cast<size_t>(edge.a)].emplace_back(edge.b, edge.weight);
        adj[static_cast<size_t>(edge.b)].emplace_back(edge.a, edge.weight);
    }

    const std::vector<Vec2D> coords = points_to_xz(points);
    auto heuristic = [&](int node) {
        return std::sqrt(dist2(coords[static_cast<size_t>(node)], coords[static_cast<size_t>(end_index)]));
    };

    std::vector<double> g_score(static_cast<size_t>(n), std::numeric_limits<double>::infinity());
    std::vector<int> came_from(static_cast<size_t>(n), -1);
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    g_score[static_cast<size_t>(start_index)] = 0.0;
    open.emplace(heuristic(start_index), start_index);

    while (!open.empty()) {
        const int current = open.top().second;
        open.pop();
        if (current == end_index)
            break;

        for (const auto& [next, cost] : adj[static_cast<size_t>(current)]) {
            const double tentative = g_score[static_cast<size_t>(current)] + cost;
            if (tentative >= g_score[static_cast<size_t>(next)])
                continue;
            came_from[static_cast<size_t>(next)] = current;
            g_score[static_cast<size_t>(next)] = tentative;
            open.emplace(tentative + heuristic(next), next);
        }
    }

    if (came_from[static_cast<size_t>(end_index)] == -1 && start_index != end_index)
        return out;

    std::vector<int> path;
    for (int at = end_index; at != -1; at = came_from[static_cast<size_t>(at)])
        path.push_back(at);
    std::reverse(path.begin(), path.end());

    data::PcgSpline spline;
    for (int idx : path) {
        const auto& p = pts[static_cast<size_t>(idx)];
        spline.points.push_back({p.x, p.y, p.z});
    }
    if (!spline.points.empty())
        out.add_spline(std::move(spline));
    return out;
}

std::vector<Edge2D> extract_edges_from_splines(const data::PcgSplineData& splines,
                                               const data::PcgPointData& points)
{
    std::vector<Edge2D> edges;
    const auto& pts = points.points();
    if (pts.empty())
        return edges;

    const std::vector<Vec2D> coords = points_to_xz(points);
    auto find_index = [&](const data::PcgSplinePoint& sp) -> int {
        int best = -1;
        double best_d = std::numeric_limits<double>::max();
        for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
            const double d = dist2({sp.x, sp.z}, coords[static_cast<size_t>(i)]);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        return best_d < 1e-4 ? best : -1;
    };

    for (const auto& spline : splines.splines()) {
        if (spline.points.size() < 2)
            continue;
        const int a = find_index(spline.points.front());
        const int b = find_index(spline.points.back());
        if (a < 0 || b < 0 || a == b)
            continue;
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        edges.push_back({lo, hi, std::sqrt(dist2(coords[static_cast<size_t>(lo)], coords[static_cast<size_t>(hi)]))});
    }
    return edges;
}

} // namespace pcg::internal::elements
