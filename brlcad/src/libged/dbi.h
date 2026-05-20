/*                          D B I . H
 * BRL-CAD
 *
 * Copyright (c) 2008-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @addtogroup ged_defines
 *
 * Experimental
 *
 * Geometry EDiting Library structures for reflecting the state of
 * the database and views.
 *
 * These are used to provide a fast, explicit expression in memory of the
 * database and view states, to allow applications to quickly display
 * hierarchical information and manipulate view data.
 *
 * We want this to be visible to C++ APIs like libqtcad, so they can reflect
 * the state of the .g hierarchy in their own structures without us or them
 * having to make copies of the data.  Pattern this on how we handle ON_Brep
 */
/** @{ */
/** @file ged/defines.h */

#ifndef GED_DBI_H
#define GED_DBI_H

#include "common.h"
#include "vmath.h"
#include "bu/cache.h"
#include "bu/vls.h"

#ifdef __cplusplus
#include <array>
#include <set>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <queue>

#include "bv/lod.h"
#include "bsg/settings.h"
#include "ged/defines.h"

/* ---- Phase 1-A: Typed hash wrappers ---------------------------------- */

/* GHash: a hash of a .g object *name* (bu_data_hash of the name string).
 * Used as a key in d_map, p_c, p_v, bboxes, c_inherit, rgb, region_id, gobjs. */
struct GHash {
    unsigned long long v = 0;
    bool operator==(const GHash &o) const { return v == o.v; }
    bool operator!=(const GHash &o) const { return v != o.v; }
    explicit operator bool()         const { return v != 0; }
};

/* InstHash: a hash of a duplicate-instance name ("name@N").
 * Used as a key in i_map, i_str, and as inner keys in matrices / i_bool
 * for duplicate instances. */
struct InstHash {
    unsigned long long v = 0;
    bool operator==(const InstHash &o) const { return v == o.v; }
    bool operator!=(const InstHash &o) const { return v != o.v; }
    explicit operator bool()           const { return v != 0; }
};

/* PathHash: a hash over an ordered sequence of element hashes representing
 * a full database path.  Used in DbiChangeEvent, SceneChangeEvent, and
 * SelectionSet::selected (path-hash → path-vector). */
struct PathHash {
    unsigned long long v = 0;
    bool operator==(const PathHash &o) const { return v == o.v; }
    bool operator!=(const PathHash &o) const { return v != o.v; }
    explicit operator bool()           const { return v != 0; }
};

namespace std {
template<> struct hash<GHash>    { size_t operator()(const GHash &h)    const { return std::hash<unsigned long long>()(h.v); } };
template<> struct hash<InstHash> { size_t operator()(const InstHash &h) const { return std::hash<unsigned long long>()(h.v); } };
template<> struct hash<PathHash> { size_t operator()(const PathHash &h) const { return std::hash<unsigned long long>()(h.v); } };
}

/* DbiPath: typed value for a full database path as an ordered sequence of
 * per-element name hashes.  Carries an implicit conversion to const vector<ull>&
 * so that existing APIs taking a vector reference stay source-compatible. */
struct GED_EXPORT DbiPath {
    std::vector<unsigned long long> hashes;

    DbiPath() = default;
    explicit DbiPath(std::vector<unsigned long long> v) : hashes(std::move(v)) {}

    bool empty()  const { return hashes.empty(); }
    size_t size() const { return hashes.size(); }
    unsigned long long front() const { return hashes.front(); }
    unsigned long long back()  const { return hashes.back(); }
    unsigned long long at(size_t i) const { return hashes.at(i); }

    bool operator==(const DbiPath &o) const { return hashes == o.hashes; }
    bool operator!=(const DbiPath &o) const { return hashes != o.hashes; }

    /* Implicit conversion so DbiPath can be passed where const vector<ull> & is expected. */
    operator const std::vector<unsigned long long>&() const { return hashes; }
};

namespace std {
template<> struct hash<DbiPath> {
    size_t operator()(const DbiPath &p) const {
        size_t seed = p.hashes.size();
        for (auto h : p.hashes)
            seed ^= std::hash<unsigned long long>()(h) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};
}

/* ---- Phase 1-C: Observer protocol ------------------------------------ */

/* Forward declarations */
class GED_EXPORT DbiState;
class GED_EXPORT GObj;
class GED_EXPORT CombInst;

/* Categories of DbiState change events */
enum class DbiChangeKind {
    ObjectAdded,
    ObjectRemoved,
    ObjectModified,
    CombTreeChanged,
    AttributeChanged,
    BatchRebuild
};

/* A single change event fired by DbiState::update() */
struct DbiChangeEvent {
    DbiChangeKind kind;
    GHash         object;     /* which object (v==0 means invalid/batch) */
    bool          batch = false;  /* true when a full rebuild has occurred */
};

/* A single scene-level change event */
struct SceneChangeEvent {
    PathHash path;   /* which path changed */
    bool     batch = false;
};

/* Pure virtual interface implemented by observers of database state changes.
 * Register with DbiState::add_observer() / remove_observer().
 * Called synchronously from the main thread after DbiState::update(). */
class GED_EXPORT IDbiObserver {
public:
    virtual ~IDbiObserver() = default;
    virtual void on_dbi_changed(const std::vector<DbiChangeEvent> &events) = 0;
};

/* Pure virtual interface for observers of scene/view changes */
class GED_EXPORT ISceneObserver {
public:
    virtual ~ISceneObserver() = default;
    virtual void on_scene_changed(const std::vector<SceneChangeEvent> &events) = 0;
};

/* ---- Phase 1-E: DrawList --------------------------------------------- */

/* Draw state values returned by DrawList::query() */
enum class DrawState { NOT_DRAWN = 0, FULLY_DRAWN = 1, PARTIALLY_DRAWN = 2 };

/* Optional per-path draw settings override */
struct DrawSettings {
    bool          has_color = false;
    struct bu_color color = BU_COLOR_INIT_ZERO;
    int           line_width = 1;
    int           mode = 0;
    bool          draw_solid_lines_only = false;
    bool          draw_non_subtract_only = false;
};

/* DrawList manages which database paths are staged for drawing in which modes.
 * Instances are owned by a BViewState and accessed via BViewState::draw_list().
 *
 * Thread-safety: MAIN THREAD ONLY. */
class GED_EXPORT DrawList {
public:
    DrawList() = default;
    ~DrawList() = default;

    struct Entry {
        std::vector<unsigned long long> path;
        unsigned long long full_hash = 0;
        int  mode = 0;
        bool has_settings = false;
        DrawSettings settings;
    };

    /* Stage a path for drawing.  Does not trigger a redraw. */
    void add(const std::vector<unsigned long long> &path_hashes, int mode = 0,
             const DrawSettings *overrides = nullptr);
    void add(const DbiPath &path, int mode = 0,
             const DrawSettings *overrides = nullptr);

    /* Remove entries by full-path hash.  mode < 0 removes all modes. */
    void drop(unsigned long long path_hash, int mode = -1);

    /* Clear all entries, or only those for the given mode. */
    void clear();
    void clear(int mode);

    /* Query draw state of a path hash */
    DrawState query(unsigned long long path_hash, int mode = -1) const;

    /* Return drawn path-hash vectors, optionally filtered by mode */
    std::vector<std::vector<unsigned long long>> drawn_path_hashes(int mode = -1) const;

    /* Count of staged entries */
    size_t count(int mode = -1) const;
    bool empty() const;

    /* Read-only access to underlying entries (e.g. for BViewState::redraw()) */
    const std::vector<Entry> &entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    mutable std::unordered_map<unsigned long long, std::unordered_set<int>> drawn_hash_modes_;
    mutable bool dirty_ = true;

    void rebuild_index() const;
};

/* ---- Phase 1-F: SelectionSet (replaces BSelectState) ----------------- */

struct bsg_selection_set;

class GED_EXPORT SelectionSet {
    public:
	explicit SelectionSet(DbiState *, const char *name = nullptr);

	/* ---- Existing BSelectState API (backward-compatible) ------------- */

	bool select_path(const char *path, bool update);
	bool select_hpath(std::vector<unsigned long long> &hpath);

	bool deselect_path(const char *path, bool update);
	bool deselect_hpath(std::vector<unsigned long long> &hpath);

	void clear();

	bool is_selected(unsigned long long);
	bool is_active(unsigned long long);
	bool is_active_parent(unsigned long long);
	bool is_parent_obj(unsigned long long);
	bool is_immediate_parent_obj(unsigned long long);
	bool is_grand_parent_obj(unsigned long long);

	std::vector<std::string> list_selected_paths();

	void expand();
	void collapse();

	void refresh();
	bool draw_sync();

	unsigned long long state_hash();

	void characterize();

	/* ---- New SelectionSet API (Phase 1-F) ---------------------------- */

	/* Select / deselect by path hash + path vector (preferred; enables
	 * hierarchy computation without a second digest_path() call). */
	bool select(unsigned long long path_hash,
		    const std::vector<unsigned long long> &path_vec,
		    bool update_hierarchy = true);
	bool deselect(unsigned long long path_hash, bool update_hierarchy = true);

	/* Type-safe overloads using DbiPath */
	bool select(const DbiPath &path, bool update_hierarchy = true);
	bool deselect(const DbiPath &path, bool update_hierarchy = true);

	/* Select / deselect by path string (delegates to select_path / deselect_path) */
	bool select(const char *path_str, bool update_hierarchy = true);
	bool deselect(const char *path_str, bool update_hierarchy = true);

	/* Typed query methods */
	bool is_parent(unsigned long long path_hash) const;   /* true when path is direct parent of a selection */
	bool is_ancestor(unsigned long long path_hash) const; /* true when path is any ancestor */
	bool is_obj_immediate_parent(unsigned long long obj_hash) const; /* same as is_immediate_parent_obj */
	bool is_obj_ancestor(unsigned long long obj_hash) const;        /* same as is_grand_parent_obj */

	/* Recompute parent/ancestor hierarchy caches from selected map.
	 * Same as characterize(); provided under the cleaner name for new code. */
	void recompute_hierarchy();

	/* Return sorted list of selected path strings (same as list_selected_paths) */
	std::vector<std::string> selected_paths() const;

	/* Return a snapshot set of selected path hashes */
	std::unordered_set<unsigned long long> selected_hashes() const;

	/* Compute a hash over the current selection state */
	unsigned long long state_hash_val() const;

	/* Read-only access to the full selected map */
	const std::unordered_map<unsigned long long, std::vector<unsigned long long>> &selected_map() const { return selected; }

	/* ---- Public data (kept public for existing callers) -------------- */

	std::unordered_map<unsigned long long, std::vector<unsigned long long>> selected;
	std::unordered_set<unsigned long long> active_paths;   /* solid paths to illuminate */
	std::unordered_set<unsigned long long> active_parents; /* paths above selection */
	/* immediate_parents: combs whose immediate child is the selected leaf.
	 * grand_parents: higher level combs above immediate parents. */
	std::unordered_set<unsigned long long> immediate_parents;
	std::unordered_set<unsigned long long> grand_parents;

    private:
	DbiState *dbis;
	std::string set_name;

	void add_paths(
		unsigned long long c_hash,
		std::vector<unsigned long long> &path_hashes
		);

	void clear_paths(
		unsigned long long c_hash,
		std::vector<unsigned long long> &path_hashes
		);

	void expand_paths(
		std::vector<std::vector<unsigned long long>> &out_paths,
		unsigned long long c_hash,
		std::vector<unsigned long long> &path_hashes
		);

	void collapse_paths(
		std::vector<std::vector<unsigned long long>> &out_paths
		);

	void clear_paths(std::vector<unsigned long long> &path_hashes, unsigned long long c_hash);
	struct bsg_selection_set *bsg_set(bool create = true) const;
	void sync_from_bsg();
	void sync_to_bsg() const;
};

/* Backward-compatibility typedef: existing code that uses BSelectState compiles unchanged. */
typedef SelectionSet BSelectState;


/* ---- Phase 3.5: Concurrent drawing-data pipeline --------------------- */

/**
 * DrawPipeline: 5-stage concurrent pipeline that pre-computes drawing data
 * (attributes, AABB, OBB, LoD) in background threads and makes the results
 * available to the main thread via drain().
 *
 * Replaces the Phase 3-C GeomLoader (single-threaded, OBB-only) with a
 * concurrentqueue-based design matching the qgedobol cache_drawing.cpp
 * pipeline.  Stages:
 *   1. attr_worker  — reads region attrs from AVS, writes to cache
 *   2. aabb_worker  — calls ft_bbox, writes AABB to cache, posts Result::AABB
 *   3. obb_worker   — calls ft_oriented_bbox (real OBB), posts Result::OBB
 *   4. lod_worker   — bv_mesh_lod_cache for BoTs, posts Result::LOD
 *   5. write_worker — serialises all bu_cache writes
 *
 * Thread-safety: push() and drain() must be called from the main thread.
 * All worker threads access only their private struct resource instances
 * and the shared ConcurrentQueue slots; they never write DbiState maps.
 */
class GED_EXPORT DrawPipeline {
public:
    /* Result posted to the main thread after a pipeline stage completes. */
    struct Result {
        enum Type { AABB = 0, OBB = 1, LOD = 2 } type = AABB;
        unsigned long long hash    = 0;
        std::string        dp_name;
        /* AABB */
        point_t bmin;
        point_t bmax;
        /* OBB: 8 corner points produced by ft_oriented_bbox */
        bool    obb_valid = false;
        point_t obb_pts[8];
        /* LOD */
        bool               has_lod = false;
        unsigned long long lod_key = 0;
    };

    /* Work item submitted to the pipeline by the main thread. */
    struct WorkItem {
        unsigned long long  hash = 0;
        struct directory   *dp   = nullptr;
    };

    explicit DrawPipeline(struct ged *gedp, struct bu_cache *dcache);
    ~DrawPipeline();

    /* Queue objects for background processing.  MAIN THREAD ONLY. */
    void push(const std::vector<WorkItem> &items);

    /* Drain all pending Result notifications.  Non-blocking.  MAIN THREAD ONLY. */
    size_t drain(std::vector<Result> &out);

    /* Returns true when all pipeline queues are empty. */
    bool settled() const;

    /* Notify the pipeline of the LoD context (may be updated after init). */
    void set_lod_ctx(struct bv_mesh_lod_context *lod_ctx);

private:
    /* Opaque pointer to the internal DrawPipelineState defined in dbi_state.cpp. */
    struct DrawPipelineState *state_ = nullptr;
};


class GED_EXPORT BViewState {
    public:
	BViewState(DbiState *);


	// Adds path to the BViewState container, but doesn't trigger a re-draw - that
	// should be done once all paths to be added in a given draw cycle are added.
	// The actual drawing (and mode specifications) are done with redraw and a
	// supplied bsg_settings structure.
	void add_path(const char *path);
	void add_hpath(std::vector<unsigned long long> &path_hashes);

	// Erases paths from the view for the given mode.  If mode < 0, all
	// matching paths are erased.  For modes that are un-evaluated, all
	// paths that are subsets of the specified path are removed.  For
	// evaluated modes like 3 (bigE) that generate an evaluated visual
	// specific to that path, only precise path matches are removed
	void erase_path(int mode, int argc, const char **argv);
	void erase_hpath(int mode, unsigned long long c_hash, std::vector<unsigned long long> &path_hashes, bool cache_collapse = true);

	// Return a sorted vector of strings encoding the drawn paths in the
	// view.  If mode == -1 list all paths, otherwise list those specific
	// to the mode.  If list_collapsed is true, return the minimal path set
	// that captures what is drawn - otherwise, return the direct list of
	// scene objects
	std::vector<std::string> list_drawn_paths(int mode, bool list_collapsed);

	// Get a count of the drawn paths
	size_t count_drawn_paths(int mode, bool list_collapsed);

	// Report if a path hash is drawn - 0 == not drawn, 1 == fully drawn, 2 == partially drawn
	int is_hdrawn(int mode, unsigned long long phash);

	// Clear all drawn objects (TODO - should allow mode specification here)
	void clear();

	// A View State refresh regenerates already drawn objects.
	unsigned long long refresh(struct bview *v, int argc, const char **argv);

	// A View State redraw can impact multiple views with a shared state - most of
	// the elements will be the same, but adaptive plotting will be view specific even
	// with otherwise common objects - we must update accordingly.
	unsigned long long redraw(const struct bsg_settings *vs, std::unordered_set<struct bview *> &views, int no_autoview);

	// Phase 3.5: Drain completed pipeline results from DrawPipeline.
	// Delegates to DbiState::drain_geom_results().
	// Returns the count of new results now available.
	// Callers should trigger a view refresh when the return value > 0.
	size_t drain_geom_results();

	// Allow callers to calculate the drawing hash of a path
	unsigned long long path_hash(std::vector<unsigned long long> &path, size_t max_len);

	// Debugging methods for printing out current states - the use of hashes
	// means direct inspection of most data isn't informative, so we provide
	// convenience methods that decode it to user-comprehensible info.
	void print_view_state(struct bu_vls *o = NULL);

	// Phase 1-E: DrawList — staged draw intents for this view.
	// Populated by add_path()/add_hpath(); consumed by redraw().
	DrawList &draw_list() { return draw_list_; }
	const DrawList &draw_list() const { return draw_list_; }

    private:
	// Sets defining all drawn solid paths (including invalid paths).  The
	// s_keys holds the ordered individual keys of each drawn solid path - it
	// is the latter that allows for the collapse operation to populate
	// drawn_paths.  s_map uses the same key as s_keys to map instances to
	// actual scene objects.  Because objects may be represented by more than
	// one type of scene object (shaded, wireframe, evaluated, etc.) the mapping of
	// key to scene object is not unique - we must also take scene object type
	// into account.
	std::unordered_map<unsigned long long, std::unordered_map<int, struct bv_scene_obj *>> s_map;
	std::unordered_map<unsigned long long, std::vector<unsigned long long>> s_keys;

	// Called when the parent Db context is getting ready to update the data
	// structures - we may need to redraw, so we save any necessary information
	// ahead of the changes.  Although this is a public function of the BViewState,
	// it is practically speaking an implementation detail
	void cache_collapsed();

	DbiState *dbis;

	int check_status(
		std::unordered_set<unsigned long long> *invalid_objects,
		std::unordered_set<unsigned long long> *changed_paths,
		unsigned long long path_hash,
		std::vector<unsigned long long> &cpath,
		bool leaf_expand
		);

	void walk_tree(
		std::unordered_set<struct bv_scene_obj *> &objs,
		unsigned long long chash,
		int curr_mode,
		struct bview *v,
		const struct bsg_draw_request *vs,
		matp_t m,
		std::vector<unsigned long long> &path_hashes,
		std::unordered_set<struct bview *> &views,
		unsigned long long *ret
		);

	void gather_paths(
		std::unordered_set<struct bv_scene_obj *> &objs,
		unsigned long long c_hash,
		int curr_mode,
		struct bview *v,
		const struct bsg_draw_request *vs,
		matp_t m,
		matp_t lm,
		std::vector<unsigned long long> &path_hashes,
		std::unordered_set<struct bview *> &views,
		unsigned long long *ret
		);

	struct bv_scene_obj * scene_obj(
		std::unordered_set<struct bv_scene_obj *> &objs,
		int curr_mode,
		const struct bsg_draw_request *vs,
		matp_t m,
		std::vector<unsigned long long> &path_hashes,
		std::unordered_set<struct bview *> &views,
		struct bview *v
		);

	int leaf_check(unsigned long long chash, std::vector<unsigned long long> &path_hashes);

	// Paths supplied by commands to be incorporated into the drawn state by redraw method
	std::vector<std::vector<unsigned long long>> staged;

	// The collapsed drawn paths from the previous db state, organized
	// by drawn mode
	void depth_group_collapse(
		std::vector<std::vector<unsigned long long>> &collapsed,
		std::unordered_set<unsigned long long> &d_paths,
		std::unordered_set<unsigned long long> &p_d_paths,
	       	std::map<size_t, std::unordered_set<unsigned long long>> &depth_groups
		);
	std::unordered_map<int, std::vector<std::vector<unsigned long long>>> mode_collapsed;
	std::vector<std::vector<unsigned long long>> all_collapsed;

	// Set of hashes of all drawn paths and subpaths, constructed during the collapse
	// operation from the set of drawn solid paths.  This allows calling codes to
	// spot check any path to see if it is active, without having to interrogate
	// other data structures or walk down the tree.
	std::unordered_map<int, std::unordered_set<unsigned long long>> drawn_paths;
	std::unordered_set<unsigned long long> all_drawn_paths;

	// Set of partially drawn paths, constructed during the collapse operation.
	// This holds the paths that should return 2 for is_hdrawn
	std::unordered_map<int, std::unordered_set<unsigned long long>> partially_drawn_paths;
	std::unordered_set<unsigned long long> all_partially_drawn_paths;

	friend class SelectionSet;

    private:
	DrawList draw_list_;
};

#define GED_DBISTATE_DB_CHANGE   0x01
#define GED_DBISTATE_VIEW_CHANGE 0x02

class GED_EXPORT DbiState {
    public:
	DbiState(struct ged *);
	~DbiState();

	unsigned long long update();

	std::vector<unsigned long long> tops(bool show_cyclic);

	bool path_color(struct bu_color *c, std::vector<unsigned long long> &elements);

	bool path_is_subtraction(std::vector<unsigned long long> &elements);
	db_op_t bool_op(unsigned long long, unsigned long long);

	bool get_matrix(matp_t m, unsigned long long p_key, unsigned long long i_key);
	bool get_path_matrix(matp_t m, std::vector<unsigned long long> &elements);

	bool get_bbox(point_t *bbmin, point_t *bbmax, matp_t curr_mat, unsigned long long hash);
	bool get_path_bbox(point_t *bbmin, point_t *bbmax, std::vector<unsigned long long> &elements);

	bool valid_hash(unsigned long long phash);
	bool valid_hash_path(std::vector<unsigned long long> &phashes);
	bool print_hash(struct bu_vls *opath, unsigned long long phash);
	void print_path(struct bu_vls *opath, std::vector<unsigned long long> &path, size_t pmax = 0, int verbsose = 0);

	const char *pathstr(std::vector<unsigned long long> &path, size_t pmax = 0);
	const char *hashstr(unsigned long long);

	std::vector<unsigned long long> digest_path(const char *path);

	unsigned long long path_hash(std::vector<unsigned long long> &path, size_t max_len);

	void clear_cache(struct directory *dp);

	BViewState *get_view_state(struct bview *);

	std::vector<BSelectState *> get_selected_states(const char *sname);
	BSelectState * find_selected_state(const char *sname);

	void put_selected_state(const char *sname);
	std::vector<std::string> list_selection_sets();

	// Phase 1-G: cleaner selection-set management
	// get_selection_set: returns the named set (or default when name is null/empty).
	//   Creates a new empty set when the name does not yet exist.
	SelectionSet *get_selection_set(const char *name = nullptr);
	// get_selection_sets: returns all sets matching pattern (* = wildcard).
	//   If pattern is null/empty returns the default set.
	std::vector<SelectionSet *> get_selection_sets(const char *pattern = nullptr);
	void          add_selection_set(const char *name);
	void          remove_selection_set(const char *name = nullptr);

	// Phase 3.5: background geometry-data pipeline (DrawPipeline).
	//
	// start_geom_load() pushes the given (hash, dp) work items onto the
	// background pipeline's q_init queue.  Workers compute AABB, OBB and
	// LoD in background threads.  MAIN THREAD ONLY.
	//
	// drain_geom_results() drains all available results from the pipeline,
	// integrates AABBs into bboxes[], OBBs into obbs[], fires scene-observer
	// notifications.  Returns the number of results processed.
	// MAIN THREAD ONLY.  Call periodically (e.g. from a Qt timer) and
	// trigger a view repaint when the return value is non-zero.
	//
	// wait_for_pipeline() polls drain_geom_results() until settled() or
	// max_ms elapsed (max_ms <= 0 == indefinite).  Useful for tests.
	void   start_geom_load(const std::vector<DrawPipeline::WorkItem> &items);
	size_t drain_geom_results();
	size_t wait_for_pipeline(int max_ms = 5000);

	// These maps are the ".g ground truth" of the comb structures - the set
	// associated with each hash contains all the child hashes from the comb
	// definition in the database for quick lookup, and the vector preserves
	// the comb ordering for listing.
	std::unordered_map<unsigned long long, std::unordered_set<unsigned long long>> p_c;
	// Note: to match MGED's 'l' printing you need to use a reverse_iterator
	std::unordered_map<unsigned long long, std::vector<unsigned long long>> p_v;

	// Translate individual object hashes to their directory names.  This map must
	// be updated any time a database object changes to remain valid.
	struct directory *get_hdp(unsigned long long);
	std::unordered_map<unsigned long long, struct directory *> d_map;

	// For invalid comb entry strings, we can't point to a directory pointer.  This
	// map must also be updated after every db change - if a directory pointer hash
	// maps to an entry in this map it needs to be removed, and newly invalid entries
	// need to be added.
	std::unordered_map<unsigned long long, std::string> invalid_entry_map;

	// This is a map of non-uniquely named child instances (i.e. instances that must be
	// numbered) to the .g database name associated with those instances.  Allows for
	// one unique entry in p_c rather than requiring per-instance duplication
	std::unordered_map<unsigned long long, unsigned long long> i_map;
	std::unordered_map<unsigned long long, std::string> i_str;

	// Matrices above comb instances are critical to geometry placement.  For non-identity
	// matrices, we store them locally so they may be accessed without having to unpack
	// the comb from disk.
	std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, std::vector<fastf_t>>> matrices;

	// Similar to matrices, store non-union bool ops for instances
	std::unordered_map<unsigned long long, std::unordered_map<unsigned long long, size_t>> i_bool;


	// Bounding boxes for each solid.  To calculate the bbox for a comb, the
	// children are walked combining the bboxes.  The idea is to be able to
	// retrieve solid bboxes and calculate comb bboxes without having to touch
	// the disk beyond the initial per-solid calculations, which may be done
	// once per load and/or dimensional change.
	std::unordered_map<unsigned long long, std::vector<fastf_t>> bboxes;

	// Oriented bounding boxes (OBBs) as 8 corner points (24 fastf_t values)
	// from ft_oriented_bbox.  Populated by the OBB stage of DrawPipeline via
	// drain_geom_results().  These are tighter than AABB and can be used for
	// better view frustum culling and placeholder rendering.
	std::unordered_map<unsigned long long, std::array<fastf_t, 24>> obbs;


	// We also have a number of standard attributes that can impact drawing,
	// which are normally only accessible by loading in the attributes of
	// the object.  We stash them in maps to have the information available
	// without having to interrogate the disk
	std::unordered_map<unsigned long long, int> c_inherit; // color inheritance flag
	std::unordered_map<unsigned long long, unsigned int> rgb; // color RGB value  (r + (g << 8) + (b << 16))
	std::unordered_map<unsigned long long, int> region_id; // region_id


	// Data to be used by callbacks
	std::unordered_set<struct directory *> added;
	std::unordered_set<struct directory *> changed;
	std::unordered_set<unsigned long long> changed_hashes;
	std::unordered_set<unsigned long long> removed;
	std::unordered_map<unsigned long long, std::string> old_names;

	// The shared view is common to multiple views, so we always update it.
	// For other associated views (if any), we track their drawn states
	// separately, but they too need to update in response to database
	// changes (as well as draw/erase commands).
	BViewState *shared_vs = NULL;
	std::unordered_map<struct bview *, BViewState *> view_states;

	// We have a "default" selection state that is always available,
	// and applications may define other named selection states.
	BSelectState *default_selected;
	std::unordered_map<std::string, BSelectState *> selected_sets;

	// Database Instance associated with this container
	struct ged *gedp = NULL;
	struct db_i *dbip = NULL;

	bool need_update_nref = true;

	// Phase 1-D: GObj model — one GObj per .g object.
	// Populated alongside the flat maps by update_dp(); owned by DbiState.
	// Access: gobjs[hash] or get_gobj(hash).
	std::unordered_map<unsigned long long, GObj *> gobjs;
	const GObj *get_gobj(unsigned long long hash) const {
	    auto it = gobjs.find(hash);
	    return (it != gobjs.end()) ? it->second : nullptr;
	}

	// Phase 1-C: Observer registration
	void add_observer(IDbiObserver *obs);
	void remove_observer(IDbiObserver *obs);
	void add_scene_observer(ISceneObserver *obs);
	void remove_scene_observer(ISceneObserver *obs);

	// Debugging methods for printing out current states - the use of hashes
	// means direct inspection of most data isn't informative, so we provide
	// convenience methods that decode it to user-comprehensible info.
	void print_dbi_state(struct bu_vls *o = NULL, bool report_view_states = false);

    private:
	void gather_cyclic(
		std::unordered_set<unsigned long long> &cyclic,
		unsigned long long c_hash,
		std::vector<unsigned long long> &path_hashes
		);
	void print_leaves(
		std::set<std::string> &leaves,
		unsigned long long c_hash,
		std::vector<unsigned long long> &path_hashes
		);

	void populate_maps(struct directory *dp, unsigned long long phash, int reset);
	unsigned long long update_dp(struct directory *dp, int reset);
	unsigned int color_int(struct bu_color *);
	int int_color(struct bu_color *c, unsigned int);
	struct resource *res = NULL;
	struct bu_cache *dcache = NULL;
	struct bu_vls hash_string = BU_VLS_INIT_ZERO;
	struct bu_vls path_string = BU_VLS_INIT_ZERO;

	// Phase 3.5: background OBB/AABB/LoD pipeline
	DrawPipeline *draw_pipeline_ = NULL;

	// Phase 1-C: observer dispatch
	std::vector<IDbiObserver *>   dbi_observers_;
	std::vector<ISceneObserver *> scene_observers_;
	void notify_dbi_observers(const std::vector<DbiChangeEvent> &events);
	void notify_scene_observers(const std::vector<SceneChangeEvent> &events);

	// GObj and CombInst need access to private DbiState internals (res, dcache)
	friend class GObj;
	friend class CombInst;
	// BViewState needs access to res for per-object draw update data
	friend class BViewState;
	// DrawPipeline accesses dbip, gedp, dcache for pipeline operations
	friend class DrawPipeline;
};


/* ---- Phase 1-D: GObj and CombInst object model ----------------------- */

/* CombInst: one per child instance in a comb tree.
 * Owned by GObj::cv; not registered in the gobjs map. */
class GED_EXPORT CombInst {
public:
    CombInst(DbiState *dbis, const char *p_name, const char *o_name,
             unsigned long long icnt, int i_op, matp_t i_mat);
    ~CombInst();

    db_op_t bool_op();
    void bbox(point_t *min, point_t *max);

    std::string cname; /* name of parent comb */
    std::string oname; /* name of instanced object */
    std::string iname; /* unique instance name (with @N suffix for duplicates) */
    unsigned long long id = 0;

    unsigned long long chash; /* hash of parent comb name */
    unsigned long long ohash; /* hash of instanced object name */
    unsigned long long ihash; /* hash of unique instance identifier */

    int boolean_op; /* OP_UNION, OP_SUBTRACT, or OP_INTERSECT */

    mat_t m;
    bool non_default_matrix = false;

    DbiState *d = NULL;
};

/* GObj: one per .g database object (comb or solid).
 * Registered in DbiState::gobjs[hash]; owned by DbiState. */
class GED_EXPORT GObj {
public:
    GObj(DbiState *dbis, struct directory *dp_i);
    ~GObj();

    void bbox(point_t *min, point_t *max);

    std::string name;
    unsigned long long hash = 0;

    int c_inherit = 0;
    int region_id = -1;
    int region_flag = 0;

    struct bu_color color = BU_COLOR_INIT_ZERO;
    bool color_set = false;

    /* Comb tree instances (populated for comb objects, empty for solids) */
    std::vector<CombInst *> cv;

    DbiState *d = NULL;
    struct directory *dp = NULL;

private:
    vect_t bb_min;
    vect_t bb_max;
    bool bb_valid = false;

    void GenCombInstances();
};


#else

/* Placeholders to allow for compilation when we're included in a C file */
typedef struct _dbi_state {
    int dummy; /* MS Visual C hack which can be removed if the struct contains something meaningful */
} DbiState;
typedef struct _bview_state {
    int dummy; /* MS Visual C hack which can be removed if the struct contains something meaningful */
} BViewState;
typedef struct _bselect_state {
    int dummy; /* MS Visual C hack which can be removed if the struct contains something meaningful */
} BSelectState;
typedef struct _bselect_state SelectionSet;

#endif

#endif /* GED_DBI_H */

/** @} */

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
