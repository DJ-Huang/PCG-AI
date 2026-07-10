// Shewchuk adaptive robust predicates implementation.
// Based on Jonathan Richard Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates" (1997).

#include "geometry/robust_predicates.hpp"
#include "geometry/wide_int.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace pcg::internal::geometry {

// ── Constants ──────────────────────────────────────────────

static const double kSplitter = 134217729.0; // 2^27 + 1
static const double kEpsilon = 2.22044604925031308e-16; // 2^-52
static const double kOrient2dErrboundA = (4.0 + 4.0 * kEpsilon) * kEpsilon;
static const double kOrient3dErrboundB = (4.0 + 6.0 * kEpsilon) * kEpsilon;

// ── Error-free transformations ─────────────────────────────

namespace detail {

void split(double a, double& hi, double& lo)
{
    double c = kSplitter * a;
    double abig = c - a;
    hi = c - abig;
    lo = a - hi;
}

double two_sum(double a, double b, double& err)
{
    double s = a + b;
    double bb = s - a;
    err = (a - (s - bb)) + (b - bb);
    return s;
}

double two_product(double a, double b, double& err)
{
    double ahi, alo, bhi, blo;
    split(a, ahi, alo);
    split(b, bhi, blo);
    double p = a * b;
    err = ((ahi * bhi - p) + (ahi * blo + alo * bhi)) + (alo * blo);
    return p;
}

/// Grow an expansion by one term. The expansion e must be sorted by increasing
/// magnitude, and |b| should be small relative to e's largest term.
/// Output h has elen+1 components, sorted by increasing magnitude.
void grow_expansion(const double* e, int elen, double b, double* h)
{
    double q = b;
    double hnew;
    for (int i = 0; i < elen; i++) {
        q = two_sum(q, e[i], hnew);
        h[i] = hnew;
    }
    h[elen] = q;
}

/// Add expansion f to expansion e. Both must be sorted by increasing magnitude.
/// Output has at most elen + flen components.
int expansion_sum(const double* e, int elen, const double* f, int flen, double* h)
{
    if (flen == 0) {
        std::memcpy(h, e, elen * sizeof(double));
        return elen;
    }
    if (elen == 0) {
        std::memcpy(h, f, flen * sizeof(double));
        return flen;
    }

    // Merge by adding f[0] into e using grow, then f[1], etc.
    std::memcpy(h, e, elen * sizeof(double));
    int hlen = elen;
    for (int j = 0; j < flen; j++) {
        double q = f[j];
        for (int i = 0; i < hlen; i++) {
            double hnew;
            q = two_sum(q, h[i], hnew);
            h[i] = hnew;
        }
        h[hlen] = q;
        hlen++;
    }

    // Compress: remove zero terms and re-sort by magnitude
    double tmp[64];
    int tlen = 0;
    for (int i = 0; i < hlen; i++) {
        if (h[i] != 0.0) {
            tmp[tlen++] = h[i];
        }
    }
    // Sort by absolute value (smallest first)
    std::sort(tmp, tmp + tlen, [](double a, double b) {
        return std::fabs(a) < std::fabs(b);
    });
    std::memcpy(h, tmp, tlen * sizeof(double));
    return tlen;
}

/// Scale an expansion by a scalar. e must be sorted by increasing magnitude.
/// Output has at most 2*elen components.
int scale_expansion(const double* e, int elen, double b, double* h)
{
    if (elen == 0) return 0;

    double result[64];
    int rlen = 0;

    for (int i = 0; i < elen; i++) {
        double err;
        double prod = two_product(b, e[i], err);
        // Build 2-component expansion [err, prod] (err is smaller)
        double term[2];
        int tlen = 0;
        if (err != 0.0) term[tlen++] = err;
        if (prod != 0.0) term[tlen++] = prod;
        if (tlen == 0) continue;

        // Ensure sorted by magnitude
        if (tlen == 2 && std::fabs(term[0]) > std::fabs(term[1]))
            std::swap(term[0], term[1]);

        double tmp[64];
        rlen = expansion_sum(result, rlen, term, tlen, tmp);
        std::memcpy(result, tmp, rlen * sizeof(double));
    }

    std::memcpy(h, result, rlen * sizeof(double));
    return rlen;
}

int expansion_sign(const double* e, int elen)
{
    // Expansion is sorted by increasing magnitude; sign is determined by
    // the last nonzero element.
    for (int i = elen - 1; i >= 0; i--) {
        if (e[i] > 0.0) return 1;
        if (e[i] < 0.0) return -1;
    }
    return 0;
}

} // namespace detail

// ── Adaptive predicates ────────────────────────────────────

namespace {

/// Fast 2D orientation (floating-point only).
/// det = (ax-cx)*(by-cy) - (ay-cy)*(bx-cx)
double orient2dfast(double ax, double ay, double bx, double by, double cx, double cy)
{
    const double detleft = (ax - cx) * (by - cy);
    const double detright = (ay - cy) * (bx - cx);
    return detleft - detright;
}

/// Exact 2D orientation using expansion arithmetic.
/// det = acx*bcy - acy*bcx where acx = ax-cx, etc.
int orient2dexact(double ax, double ay, double bx, double by, double cx, double cy)
{
    const double acx = ax - cx;
    const double acy = ay - cy;
    const double bcx = bx - cx;
    const double bcy = by - cy;

    // Each product is a 2-component expansion [error, product] (sorted by magnitude)
    double t1[2], t2[2];
    double e1, e2;
    t1[1] = detail::two_product(acx, bcy, e1); t1[0] = e1;
    t2[1] = detail::two_product(acy, bcx, e2); t2[0] = e2;

    // Negate t2: det = t1 - t2
    double neg_t2[2] = {-t2[0], -t2[1]};
    // Re-sort neg_t2 by magnitude
    if (std::fabs(neg_t2[0]) > std::fabs(neg_t2[1]))
        std::swap(neg_t2[0], neg_t2[1]);

    double result[8];
    int rlen = detail::expansion_sum(t1, 2, neg_t2, 2, result);
    if (rlen == 0) return 0;
    return detail::expansion_sign(result, rlen);
}

/// Adaptive 2D orientation.
int orient2dadaptive(double ax, double ay, double bx, double by, double cx, double cy)
{
    const double detleft = (ax - cx) * (by - cy);
    const double detright = (ay - cy) * (bx - cx);
    double det = detleft - detright;

    const double detsum = std::fabs(detleft) + std::fabs(detright);

    if (det != 0.0 && std::fabs(det) >= kOrient2dErrboundA * detsum) {
        return det > 0.0 ? 1 : (det < 0.0 ? -1 : 0);
    }

    return orient2dexact(ax, ay, bx, by, cx, cy);
}

/// Fast 3D orientation (floating-point only).
/// Returns -det[a-d, b-d, c-d] so that Positive means d is above plane abc.
double orient3dfast(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    const double adx = a.x - d.x;
    const double ady = a.y - d.y;
    const double adz = a.z - d.z;
    const double bdx = b.x - d.x;
    const double bdy = b.y - d.y;
    const double bdz = b.z - d.z;
    const double cdx = c.x - d.x;
    const double cdy = c.y - d.y;
    const double cdz = c.z - d.z;

    const double det = adz * (bdx * cdy - cdx * bdy)
                    + bdz * (cdx * ady - adx * cdy)
                    + cdz * (adx * bdy - bdx * ady);

    return -det; // Negate for "d above plane" convention
}

/// Exact 3D orientation using expansion arithmetic.
int orient3dexact(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    const double adx = a.x - d.x;
    const double ady = a.y - d.y;
    const double adz = a.z - d.z;
    const double bdx = b.x - d.x;
    const double bdy = b.y - d.y;
    const double bdz = b.z - d.z;
    const double cdx = c.x - d.x;
    const double cdy = c.y - d.y;
    const double cdz = c.z - d.z;

    // Each product: two_product returns (product, error), |error| << |product|
    // Build 2-component expansions [error, product] sorted by magnitude

    // Cofactor 0: bdx*cdy - cdx*bdy
    double p01[2], p02[2];
    double e01, e02;
    p01[1] = detail::two_product(bdx, cdy, e01); p01[0] = e01;
    p02[1] = detail::two_product(cdx, bdy, e02); p02[0] = e02;
    double neg_p02[2] = {-p02[0], -p02[1]};
    if (std::fabs(neg_p02[0]) > std::fabs(neg_p02[1])) std::swap(neg_p02[0], neg_p02[1]);
    double c0[8];
    int c0len = detail::expansion_sum(p01, 2, neg_p02, 2, c0);

    // Cofactor 1: cdx*ady - adx*cdy
    double p11[2], p12[2];
    double e11, e12;
    p11[1] = detail::two_product(cdx, ady, e11); p11[0] = e11;
    p12[1] = detail::two_product(adx, cdy, e12); p12[0] = e12;
    double neg_p12[2] = {-p12[0], -p12[1]};
    if (std::fabs(neg_p12[0]) > std::fabs(neg_p12[1])) std::swap(neg_p12[0], neg_p12[1]);
    double c1[8];
    int c1len = detail::expansion_sum(p11, 2, neg_p12, 2, c1);

    // Cofactor 2: adx*bdy - bdx*ady
    double p21[2], p22[2];
    double e21, e22;
    p21[1] = detail::two_product(adx, bdy, e21); p21[0] = e21;
    p22[1] = detail::two_product(bdx, ady, e22); p22[0] = e22;
    double neg_p22[2] = {-p22[0], -p22[1]};
    if (std::fabs(neg_p22[0]) > std::fabs(neg_p22[1])) std::swap(neg_p22[0], neg_p22[1]);
    double c2[8];
    int c2len = detail::expansion_sum(p21, 2, neg_p22, 2, c2);

    // Scale each cofactor by the corresponding z-difference and sum
    double buf[64];
    int blen = 0;

    auto add_scaled = [&](const double* e, int elen, double scale) {
        if (elen == 0) return;
        double scaled[64];
        int slen = detail::scale_expansion(e, elen, scale, scaled);
        if (slen == 0) return;
        double tmp[64];
        blen = detail::expansion_sum(buf, blen, scaled, slen, tmp);
        std::memcpy(buf, tmp, blen * sizeof(double));
    };

    add_scaled(c0, c0len, adz);
    add_scaled(c1, c1len, bdz);
    add_scaled(c2, c2len, cdz);

    if (blen == 0) return 0;
    // Negate for "d above plane" convention
    return -detail::expansion_sign(buf, blen);
}

/// Adaptive 3D orientation.
int orient3dadaptive(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    const double det = orient3dfast(a, b, c, d);

    // Compute error bound
    const double adx = a.x - d.x;
    const double ady = a.y - d.y;
    const double adz = a.z - d.z;
    const double bdx = b.x - d.x;
    const double bdy = b.y - d.y;
    const double bdz = b.z - d.z;
    const double cdx = c.x - d.x;
    const double cdy = c.y - d.y;
    const double cdz = c.z - d.z;

    const double permanent =
        (std::fabs(bdx * cdy) + std::fabs(cdx * bdy)) * std::fabs(adz)
      + (std::fabs(cdx * ady) + std::fabs(adx * cdy)) * std::fabs(bdz)
      + (std::fabs(adx * bdy) + std::fabs(bdx * ady)) * std::fabs(cdz);

    if (det != 0.0 && std::fabs(det) >= kOrient3dErrboundB * permanent) {
        return det > 0.0 ? 1 : (det < 0.0 ? -1 : 0);
    }

    return orient3dexact(a, b, c, d);
}

} // anonymous namespace

// ── Public API ─────────────────────────────────────────────

OrientSign orient2d(const Vec3& a, const Vec3& b, const Vec3& c)
{
    // Choose the best projection plane (largest normal component).
    const double nx = std::fabs((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y));
    const double ny = std::fabs((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z));
    const double nz = std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));

    int sign;
    if (nx >= ny && nx >= nz) {
        sign = orient2dadaptive(a.y, a.z, b.y, b.z, c.y, c.z);
    } else if (ny >= nz) {
        sign = orient2dadaptive(a.x, a.z, b.x, b.z, c.x, c.z);
    } else {
        sign = orient2dadaptive(a.x, a.y, b.x, b.y, c.x, c.y);
    }

    if (sign > 0) return OrientSign::Positive;
    if (sign < 0) return OrientSign::Negative;
    return OrientSign::Zero;
}

OrientSign orient3d(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    const int sign = orient3dadaptive(a, b, c, d);
    if (sign > 0) return OrientSign::Positive;
    if (sign < 0) return OrientSign::Negative;
    return OrientSign::Zero;
}

OrientSign orient3d_fast(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    const double det = orient3dfast(a, b, c, d);
    if (det > 0.0) return OrientSign::Positive;
    if (det < 0.0) return OrientSign::Negative;
    return OrientSign::Zero;
}

// ── Triangle-triangle intersection ────────────────────────

TriIntersectResult tri_tri_intersect(const Vec3 tri_a[3], const Vec3 tri_b[3])
{
    // Based on Möller's triangle-triangle intersection test,
    // enhanced with robust orientation predicates for edge cases.

    const Vec3& a0 = tri_a[0];
    const Vec3& a1 = tri_a[1];
    const Vec3& a2 = tri_a[2];

    const double e1x = a1.x - a0.x, e1y = a1.y - a0.y, e1z = a1.z - a0.z;
    const double e2x = a2.x - a0.x, e2y = a2.y - a0.y, e2z = a2.z - a0.z;

    // Normal of triangle A
    const double nx = e1y * e2z - e1z * e2y;
    const double ny = e1z * e2x - e1x * e2z;
    const double nz = e1x * e2y - e1y * e2x;
    const double nlen = std::sqrt(nx * nx + ny * ny + nz * nz);

    TriIntersectResult result;

    if (nlen < 1e-20) {
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    // Use robust orientation to classify B vertices relative to plane A
    const OrientSign o0 = orient3d(a0, a1, a2, tri_b[0]);
    const OrientSign o1 = orient3d(a0, a1, a2, tri_b[1]);
    const OrientSign o2 = orient3d(a0, a1, a2, tri_b[2]);

    const bool b0_pos = o0 == OrientSign::Positive;
    const bool b0_neg = o0 == OrientSign::Negative;
    const bool b1_pos = o1 == OrientSign::Positive;
    const bool b1_neg = o1 == OrientSign::Negative;
    const bool b2_pos = o2 == OrientSign::Positive;
    const bool b2_neg = o2 == OrientSign::Negative;

    if ((b0_pos || b0_neg) && (b1_pos || b1_neg) && (b2_pos || b2_neg)) {
        if ((b0_pos && b1_pos && b2_pos) || (b0_neg && b1_neg && b2_neg)) {
            result.kind = TriIntersectResult::Kind::Disjoint;
            return result;
        }
    }

    // Check if all three are exactly zero (coplanar)
    if (o0 == OrientSign::Zero && o1 == OrientSign::Zero && o2 == OrientSign::Zero) {
        // Need to check if they actually overlap in 2D.
        // Project onto best plane and check for vertex containment or edge crossing.
        const int axis = (std::fabs(nx) >= std::fabs(ny) && std::fabs(nx) >= std::fabs(nz)) ? 0
                       : (std::fabs(ny) >= std::fabs(nz) ? 1 : 2);

        auto orient2d_raw = [](double ax, double ay, double bx, double by, double cx, double cy) -> double {
            return (ax - cx) * (by - cy) - (ay - cy) * (bx - cx);
        };

        auto pt2d = [](const Vec3& v, int ax) -> std::pair<double, double> {
            if (ax == 0) return {v.y, v.z};
            if (ax == 1) return {v.x, v.z};
            return {v.x, v.y};
        };

        auto [a0x, a0y] = pt2d(a0, axis);
        auto [a1x, a1y] = pt2d(a1, axis);
        auto [a2x, a2y] = pt2d(a2, axis);

        // Check if any B vertex is inside A
        for (int i = 0; i < 3; i++) {
            auto [bx, by] = pt2d(tri_b[i], axis);
            double d1 = orient2d_raw(bx, by, a0x, a0y, a1x, a1y);
            double d2 = orient2d_raw(bx, by, a1x, a1y, a2x, a2y);
            double d3 = orient2d_raw(bx, by, a2x, a2y, a0x, a0y);
            bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(has_neg && has_pos)) {
                result.kind = TriIntersectResult::Kind::CoplanarOverlap;
                return result;
            }
        }

        // Check if any A vertex is inside B
        auto [b0x, b0y] = pt2d(tri_b[0], axis);
        auto [b1x, b1y] = pt2d(tri_b[1], axis);
        auto [b2x, b2y] = pt2d(tri_b[2], axis);
        for (int i = 0; i < 3; i++) {
            auto [px, py] = pt2d(tri_a[i], axis);
            double d1 = orient2d_raw(px, py, b0x, b0y, b1x, b1y);
            double d2 = orient2d_raw(px, py, b1x, b1y, b2x, b2y);
            double d3 = orient2d_raw(px, py, b2x, b2y, b0x, b0y);
            bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(has_neg && has_pos)) {
                result.kind = TriIntersectResult::Kind::CoplanarOverlap;
                return result;
            }
        }

        // Check edge-edge crossings
        auto edges_cross = [&](double p1x, double p1y, double p2x, double p2y,
                                double p3x, double p3y, double p4x, double p4y) -> bool {
            double d1 = orient2d_raw(p3x, p3y, p4x, p4y, p1x, p1y);
            double d2 = orient2d_raw(p3x, p3y, p4x, p4y, p2x, p2y);
            double d3 = orient2d_raw(p1x, p1y, p2x, p2y, p3x, p3y);
            double d4 = orient2d_raw(p1x, p1y, p2x, p2y, p4x, p4y);
            return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
                   ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
        };

        double a_edges[3][4] = {{a0x, a0y, a1x, a1y}, {a1x, a1y, a2x, a2y}, {a2x, a2y, a0x, a0y}};
        double b_edges[3][4] = {{b0x, b0y, b1x, b1y}, {b1x, b1y, b2x, b2y}, {b2x, b2y, b0x, b0y}};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if (edges_cross(a_edges[i][0], a_edges[i][1], a_edges[i][2], a_edges[i][3],
                                 b_edges[j][0], b_edges[j][1], b_edges[j][2], b_edges[j][3])) {
                    result.kind = TriIntersectResult::Kind::CoplanarOverlap;
                    return result;
                }
            }
        }

        // No overlap
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    // Compute the plane equation of triangle B
    const Vec3& b0 = tri_b[0];
    const Vec3& b1 = tri_b[1];
    const Vec3& b2 = tri_b[2];

    const double be1x = b1.x - b0.x, be1y = b1.y - b0.y, be1z = b1.z - b0.z;
    const double be2x = b2.x - b0.x, be2y = b2.y - b0.y, be2z = b2.z - b0.z;

    const double mnx = be1y * be2z - be1z * be2y;
    const double mny = be1z * be2x - be1x * be2z;
    const double mnz = be1x * be2y - be1y * be2x;
    const double mnlen = std::sqrt(mnx * mnx + mny * mny + mnz * mnz);

    if (mnlen < 1e-20) {
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    const OrientSign oa0 = orient3d(b0, b1, b2, a0);
    const OrientSign oa1 = orient3d(b0, b1, b2, a1);
    const OrientSign oa2 = orient3d(b0, b1, b2, a2);

    const bool a0_pos = oa0 == OrientSign::Positive;
    const bool a0_neg = oa0 == OrientSign::Negative;
    const bool a1_pos = oa1 == OrientSign::Positive;
    const bool a1_neg = oa1 == OrientSign::Negative;
    const bool a2_pos = oa2 == OrientSign::Positive;
    const bool a2_neg = oa2 == OrientSign::Negative;

    if ((a0_pos || a0_neg) && (a1_pos || a1_neg) && (a2_pos || a2_neg)) {
        if ((a0_pos && a1_pos && a2_pos) || (a0_neg && a1_neg && a2_neg)) {
            result.kind = TriIntersectResult::Kind::Disjoint;
            return result;
        }
    }

    // Both triangles cross each other's planes → compute intersection segment.
    // Direction of the intersection line:
    const double dirx = ny * mnz - nz * mny;
    const double diry = nz * mnx - nx * mnz;
    const double dirz = nx * mny - ny * mnx;
    const double dirlen = std::sqrt(dirx * dirx + diry * diry + dirz * dirz);

    if (dirlen < 1e-20) {
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    // Compute signed distances using the plane normals
    const double d0 = (tri_b[0].x - a0.x) * nx + (tri_b[0].y - a0.y) * ny + (tri_b[0].z - a0.z) * nz;
    const double d1 = (tri_b[1].x - a0.x) * nx + (tri_b[1].y - a0.y) * ny + (tri_b[1].z - a0.z) * nz;
    const double d2 = (tri_b[2].x - a0.x) * nx + (tri_b[2].y - a0.y) * ny + (tri_b[2].z - a0.z) * nz;

    const double da0 = (a0.x - b0.x) * mnx + (a0.y - b0.y) * mny + (a0.z - b0.z) * mnz;
    const double da1 = (a1.x - b0.x) * mnx + (a1.y - b0.y) * mny + (a1.z - b0.z) * mnz;
    const double da2 = (a2.x - b0.x) * mnx + (a2.y - b0.y) * mny + (a2.z - b0.z) * mnz;

    // Compute the intersection segment for triangle B crossing plane A
    // Buffer can hold up to 6 points (2 per edge × 3 edges) for degenerate cases.
    Vec3 seg_b[6];
    int seg_b_count = 0;

    auto compute_edge_plane_intersection = [](const Vec3& p1, const Vec3& p2,
                                              double dp1, double dp2,
                                              Vec3* seg, int& seg_count) {
        if (seg_count >= 5) return; // cap at 5 to leave room
        if (dp1 == 0.0 && dp2 == 0.0) return;
        if (dp1 == 0.0) { seg[seg_count++] = p1; return; }
        if (dp2 == 0.0) { seg[seg_count++] = p2; return; }
        if ((dp1 > 0.0 && dp2 > 0.0) || (dp1 < 0.0 && dp2 < 0.0)) return;
        const double t = dp1 / (dp1 - dp2);
        seg[seg_count++] = Vec3{
            p1.x + t * (p2.x - p1.x),
            p1.y + t * (p2.y - p1.y),
            p1.z + t * (p2.z - p1.z),
        };
    };

    compute_edge_plane_intersection(tri_b[0], tri_b[1], d0, d1, seg_b, seg_b_count);
    compute_edge_plane_intersection(tri_b[1], tri_b[2], d1, d2, seg_b, seg_b_count);
    compute_edge_plane_intersection(tri_b[2], tri_b[0], d2, d0, seg_b, seg_b_count);

    Vec3 seg_a[6];
    int seg_a_count = 0;

    compute_edge_plane_intersection(a0, a1, da0, da1, seg_a, seg_a_count);
    compute_edge_plane_intersection(a1, a2, da1, da2, seg_a, seg_a_count);
    compute_edge_plane_intersection(a2, a0, da2, da0, seg_a, seg_a_count);

    if (seg_a_count < 2 || seg_b_count < 2) {
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    // Project both segments onto the intersection line direction.
    auto project = [&](const Vec3& p) -> double {
        return p.x * dirx + p.y * diry + p.z * dirz;
    };

    double a0p = project(seg_a[0]);
    double a1p = project(seg_a[1]);
    double b0p = project(seg_b[0]);
    double b1p = project(seg_b[1]);

    int a_lo_idx = (a0p < a1p) ? 0 : 1;
    int a_hi_idx = 1 - a_lo_idx;
    int b_lo_idx = (b0p < b1p) ? 0 : 1;
    int b_hi_idx = 1 - b_lo_idx;

    double a_lo_val = std::min(a0p, a1p);
    double a_hi_val = std::max(a0p, a1p);
    double b_lo_val = std::min(b0p, b1p);
    double b_hi_val = std::max(b0p, b1p);

    double overlap_lo = std::max(a_lo_val, b_lo_val);
    double overlap_hi = std::min(a_hi_val, b_hi_val);

    if (overlap_hi <= overlap_lo) {
        result.kind = TriIntersectResult::Kind::Disjoint;
        return result;
    }

    // Store full crossing segments for each triangle (edge-to-edge)
    result.a_p0 = seg_a[0];
    result.a_p1 = seg_a[1];
    result.b_p0 = seg_b[0];
    result.b_p1 = seg_b[1];

    // The overlap segment endpoints come from whichever segment is more restrictive.
    result.kind = TriIntersectResult::Kind::Segment;
    result.p0 = (a_lo_val >= b_lo_val) ? seg_a[a_lo_idx] : seg_b[b_lo_idx];
    result.p1 = (a_hi_val <= b_hi_val) ? seg_a[a_hi_idx] : seg_b[b_hi_idx];
    return result;
}

} // namespace pcg::internal::geometry
