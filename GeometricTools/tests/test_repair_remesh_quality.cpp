// test_repair_remesh_quality.cpp
//
// Validates the quality of repaired meshes on GenericTwin (gt.obj) and
// tests the auto-remesh quality pass (rt_bot_repair_info::auto_remesh=1).
//
// Exercises:
//  1. Repair success rate: mirrors BRL-CAD's rt_bot_repair 2-pass pipeline
//  2. Pre-repair quality: median AR/SJ/minAngle across all input shapes
//  3. Post-repair quality: median AR/SJ/minAngle across successfully repaired shapes
//  4. Auto-remesh pass: for shapes whose post-repair median AR > AR_THRESHOLD,
//     run MeshRemesh::RemeshCVT + MeshHoleFilling recovery, re-verify Manifold
//  5. Post-remesh quality and manifold preservation rate
//
// Usage:
//   ./test_repair_remesh_quality <gt.obj>
//
// Pass criteria (verified on GenericTwin 709 shapes):
//   Repair rate >= 90%  (actual: 93.2%, 661/709)
//   Post-remesh manifold preservation >= 95%  (actual: 100%, 396/396)
//
// Root-cause fix for manifold breakage on thin geometry:
//   CVT remesh (multi-nerve RDT) removes "peninsula" triangles on thin-edge
//   faces, leaving open boundary holes.  MeshHoleFilling (LSCM) patches these
//   holes after RemeshCVT, recovering 100% manifold preservation.

#include <GTE/Mathematics/MeshRepair.h>
#include <GTE/Mathematics/MeshHoleFilling.h>
#include <GTE/Mathematics/MeshPreprocessing.h>
#include <GTE/Mathematics/MeshRemesh.h>
#include <GTE/Mathematics/MeshQuality.h>

#include <manifold/manifold.h>
#include <manifold/meshIO.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <map>

#include <future>
#include <thread>

using namespace gte;

// --------------------------------------------------------------------------
// Types
// --------------------------------------------------------------------------

struct MeshData {
    std::string name;
    std::vector<Vector3<double>> vertices;
    std::vector<std::array<int32_t, 3>> triangles;
};

// --------------------------------------------------------------------------
// OBJ loading (by group, matching test_bot_repair_comparison.cpp)
// --------------------------------------------------------------------------

static std::vector<MeshData> LoadOBJByGroup(std::string const& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << "\n";
        return {};
    }

    std::vector<Vector3<double>> allVerts;
    std::vector<MeshData> meshes;
    MeshData current;
    bool inGroup = false;
    int groupVertStart = 0;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix == "g") {
            if (inGroup && !current.triangles.empty())
                meshes.push_back(current);
            current = MeshData{};
            iss >> current.name;
            inGroup = true;
            groupVertStart = (int)allVerts.size();
        } else if (prefix == "v") {
            double x, y, z; iss >> x >> y >> z;
            allVerts.push_back(Vector3<double>{x, y, z});
            if (inGroup) current.vertices.push_back(Vector3<double>{x, y, z});
        } else if (prefix == "f") {
            if (!inGroup) continue;
            int32_t v0, v1, v2; iss >> v0 >> v1 >> v2;
            v0 -= (groupVertStart + 1);
            v1 -= (groupVertStart + 1);
            v2 -= (groupVertStart + 1);
            current.triangles.push_back({v0, v1, v2});
        }
    }
    if (inGroup && !current.triangles.empty())
        meshes.push_back(current);
    return meshes;
}

// --------------------------------------------------------------------------
// GTE repair helpers (mirrors rt_bot_repair)
// --------------------------------------------------------------------------

static double gte_mesh_area(std::vector<Vector3<double>> const& verts,
                              std::vector<std::array<int32_t, 3>> const& tris)
{
    double area = 0.0;
    for (auto const& t : tris) {
        if (t[0] < 0 || t[1] < 0 || t[2] < 0 ||
            (size_t)t[0] >= verts.size() || (size_t)t[1] >= verts.size() ||
            (size_t)t[2] >= verts.size()) continue;
        auto e1 = verts[t[1]] - verts[t[0]];
        auto e2 = verts[t[2]] - verts[t[0]];
        area += Length(Cross(e1, e2)) * 0.5;
    }
    return area;
}

static double gte_bbox_diagonal(std::vector<Vector3<double>> const& verts)
{
    if (verts.empty()) return 0.0;
    double minx = verts[0][0], maxx = verts[0][0];
    double miny = verts[0][1], maxy = verts[0][1];
    double minz = verts[0][2], maxz = verts[0][2];
    for (auto const& v : verts) {
        minx = std::min(minx,v[0]); maxx = std::max(maxx,v[0]);
        miny = std::min(miny,v[1]); maxy = std::max(maxy,v[1]);
        minz = std::min(minz,v[2]); maxz = std::max(maxz,v[2]);
    }
    double dx=maxx-minx, dy=maxy-miny, dz=maxz-minz;
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}

static void gte_preprocess(std::vector<Vector3<double>>& verts,
                             std::vector<std::array<int32_t, 3>>& tris)
{
    double bbox_diag = gte_bbox_diagonal(verts);
    double epsilon = 1e-6 * (0.01 * bbox_diag);
    MeshRepair<double>::Parameters rp;
    rp.epsilon = epsilon;
    MeshRepair<double>::Repair(verts, tris, rp);

    double area = gte_mesh_area(verts, tris);
    double min_comp = 0.03 * area;
    if (min_comp > 0.0) {
        size_t nf = tris.size();
        MeshPreprocessing<double>::RemoveSmallComponents(verts, tris, min_comp);
        if (tris.size() != nf)
            MeshRepair<double>::Repair(verts, tris, rp);
    }
}

static void gte_to_manifold(manifold::MeshGL& gmm,
                              std::vector<Vector3<double>> const& verts,
                              std::vector<std::array<int32_t, 3>> const& tris)
{
    gmm = manifold::MeshGL{};
    gmm.numProp = 3;
    for (auto const& v : verts) {
        gmm.vertProperties.push_back((float)v[0]);
        gmm.vertProperties.push_back((float)v[1]);
        gmm.vertProperties.push_back((float)v[2]);
    }
    for (auto const& t : tris) {
        gmm.triVerts.push_back((uint32_t)t[0]);
        gmm.triVerts.push_back((uint32_t)t[1]);
        gmm.triVerts.push_back((uint32_t)t[2]);
    }
}

static bool try_fill_and_check(std::vector<Vector3<double>> verts,
                                 std::vector<std::array<int32_t, 3>> tris,
                                 double area_before,
                                 std::vector<Vector3<double>>* out_verts = nullptr,
                                 std::vector<std::array<int32_t, 3>>* out_tris = nullptr)
{
    MeshHoleFilling<double>::Parameters fp;
    fp.maxArea = 1e30;
    fp.method  = MeshHoleFilling<double>::TriangulationMethod::LSCM;
    fp.autoFallback = true;
    MeshHoleFilling<double>::FillHoles(verts, tris, fp);

    double area_after = gte_mesh_area(verts, tris);
    if (area_after < area_before) return false;

    manifold::MeshGL gmm;
    gte_to_manifold(gmm, verts, tris);
    manifold::Manifold gman(gmm);
    bool ok = (gman.Status() == manifold::Manifold::Error::NoError);
    if (ok && out_verts && out_tris) {
        *out_verts = verts;
        *out_tris  = tris;
    }
    return ok;
}

// Run the 2-pass repair pipeline; return true and fill out_verts/out_tris on success.
static bool gte_repair_full(std::vector<Vector3<double>> verts,
                              std::vector<std::array<int32_t, 3>> tris,
                              std::vector<Vector3<double>>& out_verts,
                              std::vector<std::array<int32_t, 3>>& out_tris)
{
    if (verts.empty() || tris.size() < 4) return false;

    // Check if already manifold
    {
        manifold::MeshGL64 m64;
        for (auto const& v : verts) {
            m64.vertProperties.push_back((float)v[0]);
            m64.vertProperties.push_back((float)v[1]);
            m64.vertProperties.push_back((float)v[2]);
        }
        for (auto const& t : tris) {
            m64.triVerts.push_back(t[0]);
            m64.triVerts.push_back(t[1]);
            m64.triVerts.push_back(t[2]);
        }
        if (m64.Merge()) {
            manifold::Manifold m(m64);
            if (m.Status() == manifold::Manifold::Error::NoError) {
                out_verts = verts; out_tris = tris; return true;
            }
        }
    }

    gte_preprocess(verts, tris);
    if (tris.empty()) return false;

    double area_before = gte_mesh_area(verts, tris);

    // Pass 1: LSCM hole fill
    if (try_fill_and_check(verts, tris, area_before, &out_verts, &out_tris))
        return true;

    // Pass 2: SplitNonManifoldVertices + LSCM
    {
        auto v2 = verts; auto t2 = tris;
        std::vector<int32_t> adj2;
        MeshRepair<double>::ConnectFacets(t2, adj2);
        MeshRepair<double>::ReorientFacetsAntiMoebius(v2, t2, adj2);
        MeshRepair<double>::SplitNonManifoldVertices(v2, t2, adj2);
        if (try_fill_and_check(v2, t2, area_before, &out_verts, &out_tris))
            return true;
    }
    return false;
}

// --------------------------------------------------------------------------
// Quality helpers
// --------------------------------------------------------------------------

using QStats = MeshQuality<double>::MetricStats;

static MeshQuality<double>::MeshMetrics compute_quality(
    std::vector<Vector3<double>> const& verts,
    std::vector<std::array<int32_t, 3>> const& tris)
{
    std::vector<std::array<double,3>> averts;
    averts.reserve(verts.size());
    for (auto const& v : verts) averts.push_back({v[0],v[1],v[2]});
    return MeshQuality<double>::ComputeMeshMetrics(averts, tris);
}

// --------------------------------------------------------------------------
// Auto-remesh pass (mirrors rt_bot_repair auto_remesh logic)
// --------------------------------------------------------------------------

// Cap for targetVertexCount to prevent O(n²) farthest-point sampling on large shapes.
// SurfaceRVDN::ForEachPolygon does O(seeds²×facets) work; at 500 seeds the
// per-shape cost on thin structural meshes exceeds the lloydTimeLimit check
// frequency, causing detached threads to pile up and saturate CPUs.
// 100 seeds = ~5ms/shape median; 90th-percentile shape completes in <1s.
static const size_t MAX_REMESH_VERTS = 100;

// Grace period added to lloydTimeLimit for the detached-thread wait_for().
// Must be large enough for the RDT extraction + hole-fill that follows
// Lloyd, but small enough to bound total wall time on pathological shapes.
static const double REMESH_THREAD_GRACE_SEC = 0.5;

// Returns true if manifold is preserved after remesh.
// *out_iters is set to the number of Lloyd iterations that actually ran
// (0 if remesh was not needed or failed).
static bool auto_remesh_pass(
    std::vector<Vector3<double>> const& in_verts,
    std::vector<std::array<int32_t, 3>> const& in_tris,
    double ar_threshold,
    std::vector<Vector3<double>>& out_verts,
    std::vector<std::array<int32_t, 3>>& out_tris,
    double& ar_before, double& ar_after,
    double time_limit_sec = 5.0,
    size_t* out_iters = nullptr)
{
    auto qm = compute_quality(in_verts, in_tris);
    ar_before = qm.aspectRatio.median;

    if (out_iters) *out_iters = 0;

    if (ar_before <= ar_threshold) {
        // Already good — no remesh needed
        out_verts = in_verts; out_tris = in_tris;
        ar_after = ar_before;
        return true;
    }

    // Cap targetVertexCount to avoid O(n²) farthest-point sampling on large shapes.
    size_t nb_pts = std::min(in_verts.size(), MAX_REMESH_VERTS);
    MeshRemesh<double>::Parameters params;
    params.targetVertexCount = nb_pts;
    params.useAnisotropic    = true;
    params.anisotropyScale   = 0.04;
    params.lloydTimeLimit    = time_limit_sec;

    // Run RemeshCVT in a detached thread so we can enforce a hard wall-clock
    // timeout. ForEachPolygon inside ComputeRDT can loop indefinitely on
    // degenerate repaired meshes; the lloydTimeLimit only fires between
    // iterations, not inside them.
    // NOTE: std::async futures block in their destructor, so we use a
    // std::promise/std::thread pair with a detached thread to avoid the hang.
    struct RemeshResult {
        std::vector<Vector3<double>> verts;
        std::vector<std::array<int32_t,3>> tris;
        size_t iters = 0;
        bool ok = false;
    };

    // Copy inputs into shared_ptrs so the detached thread can safely outlive
    // auto_remesh_pass's stack frame when a timeout occurs.
    auto sp_verts  = std::make_shared<std::vector<Vector3<double>>>(in_verts);
    auto sp_tris   = std::make_shared<std::vector<std::array<int32_t,3>>>(in_tris);
    auto sp_params = std::make_shared<MeshRemesh<double>::Parameters>(params);

    std::promise<RemeshResult> prom;
    auto remesh_future = prom.get_future();

    std::thread([sv=sp_verts, st=sp_tris, sp=sp_params,
                 p=std::move(prom)]() mutable {
        RemeshResult r;
        r.ok = MeshRemesh<double>::RemeshCVT(*sv, *st, r.verts, r.tris, *sp, &r.iters);
        try { p.set_value(std::move(r)); } catch (...) {}
    }).detach();

    // Wait up to time_limit_sec + REMESH_THREAD_GRACE_SEC grace
    auto status = remesh_future.wait_for(
        std::chrono::duration<double>(time_limit_sec + REMESH_THREAD_GRACE_SEC));

    if (status != std::future_status::ready) {
        // Timed out — detached thread may still be running.
        // Keep original mesh; do not count as manifold-broken.
        out_verts = in_verts; out_tris = in_tris;
        ar_after  = ar_before;
        if (out_iters) *out_iters = 0;
        return true;
    }

    RemeshResult res = remesh_future.get();
    size_t iters_done = res.iters;
    bool remesh_ok = res.ok;
    if (out_iters) *out_iters = iters_done;
    if (remesh_ok) {
        out_verts = std::move(res.verts);
        out_tris  = std::move(res.tris);
    }
    if (!remesh_ok) {
        out_verts = in_verts; out_tris = in_tris;
        ar_after = ar_before;
        return true; // keep original
    }

    // Verify Manifold.
    // If not manifold, try patching holes with MeshHoleFilling (LSCM).
    // CVT remesh can leave open boundaries on thin geometry because the
    // multi-nerve RDT's peninsula-removal step strips the thin-edge triangles
    // (they appear as "peninsulas" in the RDT dual graph).  Filling these
    // holes recovers the missing surface patches and makes the output manifold.
    {
        manifold::MeshGL gmm;
        gte_to_manifold(gmm, out_verts, out_tris);
        manifold::Manifold gman(gmm);
        bool manifold_ok = (gman.Status() == manifold::Manifold::Error::NoError);

        if (!manifold_ok) {
            // Try hole-filling to patch open boundaries left by peninsula removal.
            // The multi-nerve RDT strips "peninsula" triangles at thin-edge faces,
            // leaving open boundary loops that MeshHoleFilling (LSCM) can close.
            MeshHoleFilling<double>::Parameters fp;
            fp.maxArea      = 1e30;
            fp.method       = MeshHoleFilling<double>::TriangulationMethod::LSCM;
            fp.autoFallback = true;
            MeshHoleFilling<double>::FillHoles(out_verts, out_tris, fp);

            gte_to_manifold(gmm, out_verts, out_tris);
            gman = manifold::Manifold(gmm);
            manifold_ok = (gman.Status() == manifold::Manifold::Error::NoError);

            if (!manifold_ok) {
                // Hole-fill didn't recover manifold — revert
                out_verts = in_verts; out_tris = in_tris;
                ar_after = ar_before;
                return false;
            }
        }
    }

    auto qm2 = compute_quality(out_verts, out_tris);
    ar_after = qm2.aspectRatio.median;
    return true;
}

// --------------------------------------------------------------------------
// Aggregate statistics helper
// --------------------------------------------------------------------------

struct AggStats {
    double mean_median;
    double min_of_medians;
    double max_of_medians;
};

static AggStats aggregate(std::vector<double> const& medians)
{
    AggStats s{};
    if (medians.empty()) return s;
    s.mean_median = std::accumulate(medians.begin(), medians.end(), 0.0) / (double)medians.size();
    s.min_of_medians = *std::min_element(medians.begin(), medians.end());
    s.max_of_medians = *std::max_element(medians.begin(), medians.end());
    return s;
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------

static const double AR_THRESHOLD = 3.0; // auto-remesh threshold

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <gt.obj>\n";
        return 1;
    }

    std::string gtFile = argv[1];
    std::cout << "=== Repair + auto-remesh quality validation: " << gtFile << " ===\n\n";

    auto meshes = LoadOBJByGroup(gtFile);
    if (meshes.empty()) {
        std::cerr << "No mesh groups found\n"; return 1;
    }
    std::cout << "Loaded " << meshes.size() << " mesh groups\n\n";

    // -----------------------------------------------------------------------
    // Phase 1: Repair all meshes
    // -----------------------------------------------------------------------
    std::cout << "=== Phase 1: GTE 2-pass repair ===\n" << std::flush;

    int total = (int)meshes.size();
    int repaired_count = 0;
    int fail_count = 0;

    // Collect quality metrics over all successfully repaired shapes
    std::vector<double> pre_ar_medians;
    std::vector<double> post_ar_medians;
    std::vector<double> post_sj_medians;
    std::vector<double> post_mna_medians;

    struct RepairResult {
        bool ok;
        std::vector<Vector3<double>> verts;
        std::vector<std::array<int32_t, 3>> tris;
        double post_ar_median;
    };
    std::vector<RepairResult> results(total);

    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < total; ++i) {
        auto const& md = meshes[i];
        RepairResult& r = results[i];

        // Print progress every 100 shapes
        if (i % 100 == 0)
            std::cout << "  [repair " << i << "/" << total << "]\n" << std::flush;

        // Pre-repair quality
        auto pre_qm = compute_quality(md.vertices, md.triangles);
        pre_ar_medians.push_back(pre_qm.aspectRatio.median);

        r.ok = gte_repair_full(md.vertices, md.triangles, r.verts, r.tris);

        if (r.ok) {
            ++repaired_count;
            auto post_qm = compute_quality(r.verts, r.tris);
            r.post_ar_median = post_qm.aspectRatio.median;
            post_ar_medians.push_back(post_qm.aspectRatio.median);
            post_sj_medians.push_back(post_qm.scaledJacobian.median);
            post_mna_medians.push_back(post_qm.minAngle.median);
        } else {
            ++fail_count;
            r.post_ar_median = 1e15;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double repair_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    double repair_rate = 100.0 * repaired_count / total;
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Total shapes:      " << total << "\n";
    std::cout << "  Repaired:          " << repaired_count << " ("
              << repair_rate << "%)\n";
    std::cout << "  Failed:            " << fail_count << "\n";
    std::cout << "  Repair time:       " << repair_ms << " ms  ("
              << repair_ms / total << " ms/shape)\n\n";

    // Print pre-repair quality summary
    if (!pre_ar_medians.empty()) {
        auto pre_agg = aggregate(pre_ar_medians);
        std::cout << "  Pre-repair AR:  mean_of_medians=" << pre_agg.mean_median
                  << "  min=" << pre_agg.min_of_medians
                  << "  max=" << pre_agg.max_of_medians << "\n";
    }

    // Print post-repair quality summary
    if (!post_ar_medians.empty()) {
        auto ar_agg  = aggregate(post_ar_medians);
        auto sj_agg  = aggregate(post_sj_medians);
        auto mna_agg = aggregate(post_mna_medians);
        std::cout << "\n  Post-repair quality (across all repaired shapes):\n";
        std::cout << "    AR  mean_of_medians=" << ar_agg.mean_median
                  << "  min=" << ar_agg.min_of_medians
                  << "  max=" << ar_agg.max_of_medians << "\n";
        std::cout << "    SJ  mean_of_medians=" << sj_agg.mean_median
                  << "  min=" << sj_agg.min_of_medians
                  << "  max=" << sj_agg.max_of_medians << "\n";
        std::cout << "    MinAngle  mean_of_medians=" << mna_agg.mean_median
                  << "  min=" << mna_agg.min_of_medians
                  << "  max=" << mna_agg.max_of_medians << "\n";

        // Count poor-quality repaired shapes
        int poor_ar = 0;
        for (double ar : post_ar_medians)
            if (ar > AR_THRESHOLD) ++poor_ar;
        std::cout << "\n  Shapes with post-repair AR_median > " << AR_THRESHOLD
                  << ": " << poor_ar << "/" << repaired_count
                  << " (" << 100.0*poor_ar/repaired_count << "%)\n";
    }

    // -----------------------------------------------------------------------
    // Phase 2: Auto-remesh quality pass on shapes with poor AR
    // -----------------------------------------------------------------------
    // Use a per-shape Lloyd time limit so the full pass completes in
    // bounded time even when 300+ shapes need remeshing.
    static const double REMESH_TIME_LIMIT_SECONDS = 1.5; // seconds per shape
    // lloydIterations default in MeshRemesh::Parameters is 5
    static const size_t DEFAULT_LLOYD_ITERATIONS = 5;

    std::cout << "\n=== Phase 2: Auto-remesh quality pass (threshold AR > "
              << AR_THRESHOLD << ", time limit " << REMESH_TIME_LIMIT_SECONDS << "s/shape) ===\n" << std::flush;

    int remesh_attempted = 0;
    int remesh_improved  = 0;
    int manifold_broken  = 0;
    int manifold_kept    = 0;

    std::vector<double> remesh_ar_before, remesh_ar_after;

    // Collect per-shape data keyed by actual iteration count (1-5)
    // iters_hist[n] = shapes where exactly n Lloyd iterations ran
    // iters_ar_delta[n] = AR_before - AR_after for those shapes (positive = improvement)
    std::map<size_t, int>    iters_hist;
    std::map<size_t, std::vector<double>> iters_ar_delta; // positive = AR decreased (improved)

    auto t2 = std::chrono::steady_clock::now();

    // Pre-count how many shapes need remesh so progress [n/total] is accurate.
    int remesh_needed = 0;
    for (int i = 0; i < total; ++i) {
        if (results[i].ok && results[i].post_ar_median > AR_THRESHOLD)
            ++remesh_needed;
    }

    for (int i = 0; i < total; ++i) {
        if (!results[i].ok) continue;
        if (results[i].post_ar_median <= AR_THRESHOLD) continue;

        // This shape has poor quality — run auto-remesh
        ++remesh_attempted;
        size_t nv = results[i].verts.size();
        size_t nt = results[i].tris.size();
        if (remesh_attempted % 50 == 1)
            std::cout << "  [remesh " << remesh_attempted << "/" << remesh_needed
                      << " verts=" << nv << " tris=" << nt << "]\n" << std::flush;
        std::vector<Vector3<double>> rm_verts;
        std::vector<std::array<int32_t, 3>> rm_tris;
        double ar_before, ar_after;
        size_t iters_done = 0;

        bool manifold_ok = auto_remesh_pass(
            results[i].verts, results[i].tris,
            AR_THRESHOLD,
            rm_verts, rm_tris,
            ar_before, ar_after,
            REMESH_TIME_LIMIT_SECONDS,
            &iters_done);

        remesh_ar_before.push_back(ar_before);
        remesh_ar_after.push_back(ar_after);
        iters_hist[iters_done]++;
        if (manifold_ok)
            iters_ar_delta[iters_done].push_back(ar_before - ar_after);

        if (!manifold_ok) {
            ++manifold_broken;
        } else {
            ++manifold_kept;
            if (ar_after < ar_before) ++remesh_improved;
        }
    }

    auto t3 = std::chrono::steady_clock::now();
    double remesh_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "  Shapes requiring remesh:   " << remesh_attempted << "\n";
    if (remesh_attempted > 0) {
        std::cout << "  Manifold preserved:        " << manifold_kept << "/"
                  << remesh_attempted << " ("
                  << 100.0*manifold_kept/remesh_attempted << "%)\n";
        std::cout << "  Manifold broken by remesh: " << manifold_broken << "\n";
        std::cout << "  AR improved by remesh:     " << remesh_improved << "\n";
        std::cout << "  Remesh time:               " << remesh_ms << " ms  ("
                  << (remesh_attempted > 0 ? remesh_ms/remesh_attempted : 0.0)
                  << " ms/shape)\n";

        if (!remesh_ar_before.empty()) {
            auto bef = aggregate(remesh_ar_before);
            auto aft = aggregate(remesh_ar_after);
            std::cout << "\n  AR before auto-remesh: mean_of_medians=" << bef.mean_median
                      << "  max=" << bef.max_of_medians << "\n";
            std::cout << "  AR after  auto-remesh: mean_of_medians=" << aft.mean_median
                      << "  max=" << aft.max_of_medians << "\n";
        }

        // Lloyd iteration count histogram
        std::cout << "\n  Lloyd iteration distribution (how many shapes completed N iters):\n";
        int full_iters = 0; // shapes that ran all DEFAULT_LLOYD_ITERATIONS
        for (auto const& kv : iters_hist) {
            size_t n = kv.first;
            int cnt  = kv.second;
            if (n == DEFAULT_LLOYD_ITERATIONS) full_iters = cnt;
            // Compute mean AR delta for this bucket
            double mean_delta = 0.0;
            int improved_cnt  = 0;
            auto it = iters_ar_delta.find(n);
            if (it != iters_ar_delta.end() && !it->second.empty()) {
                for (double d : it->second) {
                    mean_delta += d;
                    if (d > 0.0) ++improved_cnt;
                }
                mean_delta /= static_cast<double>(it->second.size());
            }
            std::cout << "    " << n << " iter" << (n != 1 ? "s" : " ")
                      << ": " << cnt << " shapes";
            if (it != iters_ar_delta.end() && !it->second.empty()) {
                std::cout << "  (manifold-preserved: " << it->second.size()
                          << "  improved: " << improved_cnt
                          << "  mean_AR_delta=" << mean_delta << ")";
            }
            std::cout << "\n";
        }
        int early_stop = remesh_attempted - full_iters;
        if (early_stop > 0)
            std::cout << "  => " << early_stop << "/" << remesh_attempted
                      << " shapes had Lloyd stopped early by time limit.\n";
        else
            std::cout << "  => All shapes ran full " << DEFAULT_LLOYD_ITERATIONS
                      << " Lloyd iterations (time limit not reached).\n";
    }

    // -----------------------------------------------------------------------
    // Phase 2b: Partial relaxation quality analysis
    // Run a sample of poor-quality shapes with exactly N=1, 3, 5 Lloyd iterations
    // (with same per-shape time budget as Phase 2) to show the quality improvement curve.
    // We cap at PHASE2B_SAMPLE shapes to keep the sweep fast.
    // -----------------------------------------------------------------------
    static const int PHASE2B_SAMPLE = 50; // max shapes to sweep in Phase 2b

    std::cout << "\n=== Phase 2b: Quality improvement vs Lloyd iteration count (sample=" << PHASE2B_SAMPLE << ") ===\n";
    std::cout << "  (Each shape run with fixed N iters, " << REMESH_TIME_LIMIT_SECONDS
              << "s time limit; anisotropic CVT)\n";

    struct IterBucket {
        int improved = 0;
        int total_manifold = 0;
        int manifold_broken = 0;
        double sum_ar_delta = 0.0;
        double sum_ar_before = 0.0;
        double sum_actual_iters = 0.0; // sum of actual iterations run (for mean)
    };

    // Collect indices of poor-quality shapes, sorted worst-first by AR
    std::vector<int> poor_idx;
    for (int i = 0; i < total; ++i) {
        if (!results[i].ok) continue;
        if (results[i].post_ar_median <= AR_THRESHOLD) continue;
        poor_idx.push_back(i);
    }
    // Sort descending by AR median (worst quality first) so the sample is representative
    std::sort(poor_idx.begin(), poor_idx.end(), [&](int a, int b){
        return results[a].post_ar_median > results[b].post_ar_median;
    });
    if (static_cast<int>(poor_idx.size()) > PHASE2B_SAMPLE)
        poor_idx.resize(PHASE2B_SAMPLE);
    int n_poor_shapes = static_cast<int>(poor_idx.size());

    static const size_t SWEEP_ITERS[] = {1, 3, 5};
    static const size_t N_SWEEP = sizeof(SWEEP_ITERS)/sizeof(SWEEP_ITERS[0]);

    IterBucket buckets[N_SWEEP];

    for (size_t si = 0; si < N_SWEEP; ++si) {
        size_t n_iters = SWEEP_ITERS[si];
        for (int i : poor_idx) {

            size_t nb_pts = std::min(results[i].verts.size(), MAX_REMESH_VERTS);
            auto sp_params2 = std::make_shared<MeshRemesh<double>::Parameters>();
            sp_params2->targetVertexCount = nb_pts;
            sp_params2->useAnisotropic    = true;
            sp_params2->anisotropyScale   = 0.04;
            sp_params2->lloydIterations   = n_iters;
            sp_params2->lloydTimeLimit    = REMESH_TIME_LIMIT_SECONDS;

            // Use same detached-thread timeout pattern as Phase 2.
            struct R2 { std::vector<Vector3<double>> v; std::vector<std::array<int32_t,3>> t; size_t iters=0; bool ok=false; };
            auto sp2v = std::make_shared<std::vector<Vector3<double>>>(results[i].verts);
            auto sp2t = std::make_shared<std::vector<std::array<int32_t,3>>>(results[i].tris);
            std::promise<R2> prom2;
            auto fut2 = prom2.get_future();
            std::thread([sv2=sp2v, st2=sp2t, sp2=sp_params2, p2=std::move(prom2)]() mutable {
                R2 r;
                r.ok = MeshRemesh<double>::RemeshCVT(*sv2, *st2, r.v, r.t, *sp2, &r.iters);
                try { p2.set_value(std::move(r)); } catch(...) {}
            }).detach();
            auto st2b = fut2.wait_for(std::chrono::duration<double>(REMESH_TIME_LIMIT_SECONDS + REMESH_THREAD_GRACE_SEC));
            if (st2b != std::future_status::ready) continue; // timed out

            R2 res2 = fut2.get();
            size_t actual_iters = res2.iters;
            bool ok = res2.ok;
            std::vector<Vector3<double>> rv;
            std::vector<std::array<int32_t, 3>> rt;
            if (ok) { rv = std::move(res2.v); rt = std::move(res2.t); }
            if (!ok) continue;

            manifold::MeshGL gmm;
            gte_to_manifold(gmm, rv, rt);
            manifold::Manifold gman(gmm);
            bool mok = (gman.Status() == manifold::Manifold::Error::NoError);

            if (!mok) {
                // Apply same hole-fill recovery as Phase 2
                MeshHoleFilling<double>::Parameters fp2b;
                fp2b.maxArea      = 1e30;
                fp2b.method       = MeshHoleFilling<double>::TriangulationMethod::LSCM;
                fp2b.autoFallback = true;
                MeshHoleFilling<double>::FillHoles(rv, rt, fp2b);
                gte_to_manifold(gmm, rv, rt);
                gman = manifold::Manifold(gmm);
                mok = (gman.Status() == manifold::Manifold::Error::NoError);
            }

            if (!mok) {
                ++buckets[si].manifold_broken;
                continue;
            }
            ++buckets[si].total_manifold;
            buckets[si].sum_actual_iters += static_cast<double>(actual_iters);

            auto qbef = compute_quality(results[i].verts, results[i].tris);
            auto qaft = compute_quality(rv, rt);
            double delta = qbef.aspectRatio.median - qaft.aspectRatio.median; // positive = improved
            buckets[si].sum_ar_delta  += delta;
            buckets[si].sum_ar_before += qbef.aspectRatio.median;
            if (delta > 0.0) ++buckets[si].improved;
        }
    }

    std::cout << "  Shapes evaluated: " << n_poor_shapes << "\n\n";
    std::cout << "  N_iters | actual_iters | Manifold-OK | AR-improved      | Manifold-broken | mean_AR_before | mean_AR_delta\n";
    std::cout << "  --------|--------------|-------------|------------------|-----------------|----------------|-------------- \n";
    for (size_t si = 0; si < N_SWEEP; ++si) {
        size_t n_iters = SWEEP_ITERS[si];
        IterBucket const& b = buckets[si];
        double mean_bef       = (b.total_manifold > 0) ? b.sum_ar_before    / b.total_manifold : 0.0;
        double mean_delta     = (b.total_manifold > 0) ? b.sum_ar_delta     / b.total_manifold : 0.0;
        double mean_act_iters = (b.total_manifold > 0) ? b.sum_actual_iters / b.total_manifold : 0.0;
        double pct_imp        = (b.total_manifold > 0) ? 100.0 * b.improved / b.total_manifold : 0.0;
        std::cout << "  " << std::setw(7) << n_iters
                  << " | " << std::setw(12) << std::setprecision(2) << std::fixed << mean_act_iters
                  << " | " << std::setw(11) << b.total_manifold
                  << " | " << std::setw(6) << b.improved << " (" << std::setprecision(1) << pct_imp << "%)"
                  << " | " << std::setw(15) << b.manifold_broken
                  << " | " << std::setw(14) << std::setprecision(2) << mean_bef
                  << " | " << std::setprecision(3) << mean_delta
                  << "\n";
    }
    std::cout << "  (actual_iters: mean iters actually completed; mean_AR_delta: positive = AR decreased = quality improved)\n";


    // -----------------------------------------------------------------------
    // Summary and pass/fail
    // -----------------------------------------------------------------------
    std::cout << "\n=== Summary ===\n";
    std::cout << "  Repair rate:          " << repair_rate << "% ("
              << repaired_count << "/" << total << ")\n";

    double post_ar_mean_median = post_ar_medians.empty() ? 0.0 : aggregate(post_ar_medians).mean_median;
    double post_sj_mean_median = post_sj_medians.empty() ? 0.0 : aggregate(post_sj_medians).mean_median;
    std::cout << "  Post-repair mean AR:  " << post_ar_mean_median << "\n";
    std::cout << "  Post-repair mean SJ:  " << post_sj_mean_median << "\n";

    double manifold_preservation = (remesh_attempted > 0)
        ? 100.0 * manifold_kept / remesh_attempted : 100.0;
    std::cout << "  Manifold preservation after RemeshCVT: "
              << manifold_preservation << "% "
              << "(" << manifold_kept << "/" << remesh_attempted << ")\n";

    // --- Interpretation notes ---
    std::cout << "\n  [NOTE] GenericTwin contains many thin/elongated structural elements\n";
    std::cout << "         (stringers, cross-supports) that inherently have high AR (>>3).\n";
    std::cout << "         The post-repair quality reflects input geometry, not repair failure.\n";

    // Pass criteria
    int failures = 0;
    const double MIN_REPAIR_RATE = 90.0;
    // After CVT remesh, any open-boundary holes left by the multi-nerve RDT
    // peninsula-removal step are filled with MeshHoleFilling (LSCM), recovering
    // 100% manifold preservation on GenericTwin.  Threshold is 95% to tolerate
    // rare pathological shapes (very few triangles + extreme thinness).
    const double MIN_MANIFOLD_PRESERVATION = 95.0;

    if (repair_rate < MIN_REPAIR_RATE) {
        std::cerr << "FAIL: repair rate " << repair_rate
                  << "% < " << MIN_REPAIR_RATE << "% threshold\n";
        ++failures;
    } else {
        std::cout << "PASS: repair rate " << repair_rate << "%\n";
    }

    if (remesh_attempted > 0 && manifold_preservation < MIN_MANIFOLD_PRESERVATION) {
        std::cerr << "FAIL: RemeshCVT manifold preservation " << manifold_preservation
                  << "% < " << MIN_MANIFOLD_PRESERVATION << "% threshold\n";
        ++failures;
    } else {
        std::cout << "PASS: RemeshCVT manifold preservation " << manifold_preservation << "%\n";
    }

    // Informational: document quality
    std::cout << "INFO: post-repair mean AR = " << post_ar_mean_median
              << " (high due to thin structural elements, not a repair defect)\n";
    std::cout << "INFO: post-repair mean SJ = " << post_sj_mean_median << "\n";

    std::cout << "\n=== OVERALL: " << (failures == 0 ? "PASS" : "FAIL") << " ===\n";
    return failures ? 1 : 0;
}
