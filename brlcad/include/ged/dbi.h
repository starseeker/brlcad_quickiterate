/*                          D B I . H
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
#include "bu/color.h"
#include "bu/vls.h"

#ifdef __cplusplus
#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "rt/op.h"
#include "ged/defines.h"

/**
 * @file dbi.h — DBI (Database Interface) layer for BRL-CAD's qged stack.
 *
 * Thread-safety model (L2)
 * ------------------------
 * NONE of the classes in this file (DbiState, BViewState, DrawList,
 * SelectionSet) are thread-safe.  Every public method must be called
 * exclusively from the application main thread unless explicitly marked
 * otherwise below.
 *
 * Background geometry loading is planned (see DBI_TODO.md §12.4 L1) and
 * will require:
 *   - A per-DbiState mutex gating all mutations.
 *   - A producer/consumer queue for geometry results posted from the loader
 *     thread to be integrated on the main thread.
 *
 * Until that work is done callers must ensure single-threaded access.
 */

// Typed wrappers for the three distinct hash spaces
struct GHash    { unsigned long long v = 0;
    bool operator==(const GHash &o)    const { return v == o.v; }
    bool operator!=(const GHash &o)    const { return v != o.v; }
    explicit operator bool()           const { return v != 0; }
};
struct InstHash { unsigned long long v = 0;
    bool operator==(const InstHash &o) const { return v == o.v; }
    bool operator!=(const InstHash &o) const { return v != o.v; }
    explicit operator bool()           const { return v != 0; }
};
struct PathHash { unsigned long long v = 0;
    bool operator==(const PathHash &o) const { return v == o.v; }
    bool operator!=(const PathHash &o) const { return v != o.v; }
    explicit operator bool()           const { return v != 0; }
};

// Specializations for use as unordered_map/unordered_set keys
namespace std {
template<> struct hash<GHash>    { size_t operator()(const GHash &h)    const { return std::hash<unsigned long long>()(h.v); } };
template<> struct hash<InstHash> { size_t operator()(const InstHash &h) const { return std::hash<unsigned long long>()(h.v); } };
template<> struct hash<PathHash> { size_t operator()(const PathHash &h) const { return std::hash<unsigned long long>()(h.v); } };
}

// DbiPath is a typed value representing a full database path as an ordered
// sequence of per-element name hashes.  It is the preferred alternative to
// raw std::vector<unsigned long long> in new code; the implicit conversion
// operator preserves backward compatibility with existing APIs that still
// take a vector reference.
struct GED_EXPORT DbiPath {
    std::vector<unsigned long long> hashes;

    DbiPath() = default;
    explicit DbiPath(std::vector<unsigned long long> v) : hashes(std::move(v)) {}

    bool empty() const { return hashes.empty(); }
    size_t size() const { return hashes.size(); }
    unsigned long long front() const { return hashes.front(); }
    unsigned long long back()  const { return hashes.back(); }
    unsigned long long at(size_t i) const { return hashes.at(i); }

    bool operator==(const DbiPath &o) const { return hashes == o.hashes; }
    bool operator!=(const DbiPath &o) const { return hashes != o.hashes; }

    // Implicit conversion allows DbiPath to be passed where a const vector
    // reference is expected, preserving backward compatibility.
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

class GED_EXPORT DbiState;
class GED_EXPORT BViewState;
class GED_EXPORT GObj;
class GED_EXPORT CombInst;

// SelectionSet tracks which database paths are currently selected and
// maintains hierarchical relationships (active subpaths, parent paths,
// ancestor paths) derived from the selection.
//
// Thread-safety: MAIN THREAD ONLY.  No locking is performed internally.
class GED_EXPORT SelectionSet {
public:
    explicit SelectionSet(DbiState *);
    ~SelectionSet() = default;

    // Select/deselect by path hash (path_vec must be the ordered element hash
    // vector corresponding to path_hash so that hierarchy can be computed)
    bool select(unsigned long long path_hash, const std::vector<unsigned long long> &path_vec, bool update_hierarchy = true);
    bool deselect(unsigned long long path_hash, bool update_hierarchy = true);
    void clear();

    // Preferred type-safe overloads using DbiPath.  The path hash is computed
    // internally from the DbiPath so callers no longer need to pre-compute it.
    bool select(const DbiPath &path, bool update_hierarchy = true);
    bool deselect(const DbiPath &path, bool update_hierarchy = true);

    // Query state by path hash
    bool is_selected(unsigned long long path_hash) const;
    bool is_active(unsigned long long path_hash) const;
    // is_parent: returns true if path_hash is the immediate parent of a selected path
    bool is_parent(unsigned long long path_hash) const;
    // is_ancestor: returns true if path_hash is any ancestor of a selected path
    bool is_ancestor(unsigned long long path_hash) const;

    // Object-hash queries (use the terminal element hash from a path, i.e. a
    // database-object name hash, rather than a full path hash).  These answer
    // questions about whether a given database object appears anywhere above a
    // selected path in the topology of the database — regardless of which
    // specific path instance was selected.
    //
    // is_obj_immediate_parent: true when obj_hash is the direct-parent object
    //   of any selected path's leaf element anywhere in the database.
    bool is_obj_immediate_parent(unsigned long long obj_hash) const;
    // is_obj_ancestor: true when obj_hash is any higher-level ancestor of any
    //   selected path's leaf element (not the immediate parent).
    bool is_obj_ancestor(unsigned long long obj_hash) const;

    // Select/deselect by path string
    bool select(const char *path_str, bool update_hierarchy = true);
    bool deselect(const char *path_str, bool update_hierarchy = true);

    // Return sorted list of selected path strings
    std::vector<std::string> selected_paths() const;

    // Compute a hash over the current selection state
    unsigned long long state_hash() const;

    // Synchronize highlight markers to the given view state
    void sync_to_drawn(BViewState *vs);
    // Synchronize highlight markers to all views in the associated ged context
    bool sync_to_all_views();

    // Expand all selected paths to their leaf solid paths.
    // Each selected comb is replaced with all of its leaf-solid descendants.
    void expand();

    // Collapse the selected paths toward the root, replacing groups of sibling
    // paths that together cover all children of their parent with the parent
    // path.  The process repeats until no further collapse is possible.
    void collapse();

    // Revalidate selected paths against the current database state and
    // regenerate the active set.  Paths that are no longer valid are removed.
    void refresh();

    // Recompute parent/ancestor/active hierarchy caches from the current
    // selected_ map.  Called automatically when update_hierarchy=true is
    // passed to select() or deselect().  May also be called directly after a
    // batch of select()/deselect(false) calls to update the caches once.
    void recompute_hierarchy();

    // Raw access for compatibility with existing code
    // Returns a snapshot set of all selected path hashes
    std::unordered_set<unsigned long long> selected_hashes() const;

    // Direct access to the full selected map (hash → path vector)
    const std::unordered_map<unsigned long long, std::vector<unsigned long long>> &selected() const { return selected_; }

private:
    DbiState *dbis_;
    // Map from full-path hash to the ordered element-hash vector for that path.
    // Storing the vector allows hierarchy and path-string computation.
    std::unordered_map<unsigned long long, std::vector<unsigned long long>> selected_;
    std::unordered_set<unsigned long long> active_;       // selected + all subpath hashes
    std::unordered_set<unsigned long long> parents_;      // immediate parent path hashes
    std::unordered_set<unsigned long long> ancestors_;    // all ancestor path hashes

    // Object-hash sets (keyed by terminal element / name hash, not path hash)
    std::unordered_set<unsigned long long> obj_immediate_parents_; // direct-parent db objects
    std::unordered_set<unsigned long long> obj_ancestors_;         // higher-level db objects

    // Internal: expand one path to its leaf-solid sub-paths (recursive)
    void expand_path(std::vector<std::vector<unsigned long long>> &out_paths,
                     unsigned long long c_hash,
                     std::vector<unsigned long long> &path_hashes);
};


// DrawState values for DrawList::query()
enum class DrawState { NOT_DRAWN = 0, FULLY_DRAWN = 1, PARTIALLY_DRAWN = 2 };

// Per-path draw settings override (optional)
struct DrawSettings {
    bool   has_color = false;
    struct bu_color color = BU_COLOR_INIT_ZERO;
    int    line_width = 1;
    int    mode = 0;
    bool   draw_solid_lines_only = false;
    bool   draw_non_subtract_only = false;
};

// DrawList manages which database paths are drawn in which modes.
// It is separate from BViewState to cleanly separate "draw intent"
// from "rendered scene objects".
//
// Thread-safety: MAIN THREAD ONLY.  No locking is performed internally.
//
// Ownership: DrawList instances are owned by a BViewState.
class GED_EXPORT DrawList {
public:
    DrawList() = default;
    ~DrawList() = default;

    // Each entry records a path to draw, the draw mode, and optional
    // per-path settings overrides.  full_hash is the precomputed
    // bu_data_hash of the path vector (same hash DbiState::path_hash()
    // would return for that path) and is used for O(1) removal.
    struct Entry {
        std::vector<unsigned long long> path;
        unsigned long long full_hash = 0;  // bu_data_hash(path.data(), path.size() * sizeof(ull))
        int mode = 0;
        bool has_settings = false;
        DrawSettings settings;
    };

    // Stage a path for drawing in the given mode with optional settings override.
    // Does not trigger a redraw; call BViewState::redraw() after all changes are staged.
    void add(const std::vector<unsigned long long> &path_hashes, int mode = 0,
             const DrawSettings *overrides = nullptr);
    // Preferred type-safe overload; DbiPath implicitly converts to const vector ref
    // so existing call sites that pass a DbiPath will resolve here automatically.
    void add(const DbiPath &path, int mode = 0,
             const DrawSettings *overrides = nullptr);

    // Remove paths matching the given full path hash in the given mode (-1 = all modes).
    // path_hash must be the full-path hash (bu_data_hash over the path vector), i.e.
    // the same value returned by DbiState::path_hash() for that path.
    void remove(unsigned long long path_hash, int mode = -1);

    // Clear the entire draw list
    void clear();

    // Clear only entries drawn in the given mode.  Entries for all other
    // modes are preserved.  Use clear() (no argument) to remove everything.
    void clear(int mode);

    // Query draw state of a path hash
    // Returns NOT_DRAWN, FULLY_DRAWN, or PARTIALLY_DRAWN
    DrawState query(unsigned long long path_hash, int mode = -1) const;

    // Return sorted list of drawn path hash vectors, optionally filtered by mode
    std::vector<std::vector<unsigned long long>> drawn_path_hashes(int mode = -1) const;

    // Return count of staged paths
    size_t count(int mode = -1) const;

    // Check if this list is empty
    bool empty() const;

    // Read-only access to the underlying entries, e.g. for BViewState::redraw()
    // to iterate pending (not-yet-drawn) entries.
    const std::vector<Entry> &entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    mutable std::unordered_map<unsigned long long, std::unordered_set<int>> drawn_hash_modes_;
    mutable bool dirty_ = true;

    void rebuild_index() const;
};

// BViewState manages the set of drawn paths for a specific view and owns the
// bv_scene_obj instances that correspond to drawn paths.
//
// Thread-safety: MAIN THREAD ONLY.  No locking is performed internally.
class GED_EXPORT BViewState {
    public:
	BViewState(DbiState *);


	// Adds path to the BViewState container, but doesn't trigger a re-draw - that
	// should be done once all paths to be added in a given draw cycle are added.
	// The actual drawing (and mode specifications) are done with redraw and a
	// supplied bv_obj_settings structure.
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
	unsigned long long redraw(struct bv_obj_settings *vs, std::unordered_set<struct bview *> &views, int no_autoview);

	// Allow callers to calculate the drawing hash of a path
	unsigned long long path_hash(std::vector<unsigned long long> &path, size_t max_len);

	// Debugging methods for printing out current states - the use of hashes
	// means direct inspection of most data isn't informative, so we provide
	// convenience methods that decode it to user-comprehensible info.
	void print_view_state(struct bu_vls *o = NULL);

	DrawList &draw_list() { return draw_list_; }
	const DrawList &draw_list() const { return draw_list_; }

	// Link this view so it sources its draw list from another (primary)
	// BViewState.  Once linked, redraw() reads the primary's draw list so
	// quad-view panels share geometry without duplicating draw intent.
	// Only the camera (struct bview *) remains independent.
	// Passing nullptr is equivalent to calling unlink().
	//
	// NOTE: This is a stub.  The full DrawList-sharing implementation is
	// deferred (see DBI_TODO.md §12.3 A2/A3).  The pointer is stored and
	// accessible via is_linked()/linked_primary() for future use.
	void link_to(BViewState *primary);
	void unlink();
	bool is_linked() const { return linked_to_ != nullptr; }
	BViewState *linked_primary() const { return linked_to_; }

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
		struct bv_obj_settings *vs,
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
		struct bv_obj_settings *vs,
		matp_t m,
		matp_t lm,
		std::vector<unsigned long long> &path_hashes,
		std::unordered_set<struct bview *> &views,
		unsigned long long *ret
		);

	struct bv_scene_obj * scene_obj(
		std::unordered_set<struct bv_scene_obj *> &objs,
		int curr_mode,
		struct bv_obj_settings *vs,
		matp_t m,
		std::vector<unsigned long long> &path_hashes,
		std::unordered_set<struct bview *> &views,
		struct bview *v
		);

	int leaf_check(unsigned long long chash, std::vector<unsigned long long> &path_hashes);

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

	DrawList draw_list_;
	BViewState *linked_to_ = nullptr;
};

#define GED_DBISTATE_DB_CHANGE   0x01
#define GED_DBISTATE_VIEW_CHANGE 0x02

// Change categories for observer notifications
enum class DbiChangeKind { ObjectAdded, ObjectRemoved, ObjectModified,
                           CombTreeChanged, AttributeChanged, BatchRebuild };

struct DbiChangeEvent {
    DbiChangeKind kind;
    GHash         object;   // which object (zero = invalid/batch)
    bool          batch = false;  // if true, a full rebuild has occurred
};

struct SceneChangeEvent {
    PathHash      path;  // which path changed
    bool          batch = false;
};

// Interface implemented by observers of database state changes
class GED_EXPORT IDbiObserver {
public:
    virtual ~IDbiObserver() = default;
    // Called synchronously on the main thread after DbiState::sync() completes
    virtual void on_dbi_changed(const std::vector<DbiChangeEvent> &events) = 0;
};

// Interface implemented by observers of scene/view changes
class GED_EXPORT ISceneObserver {
public:
    virtual ~ISceneObserver() = default;
    virtual void on_scene_changed(const std::vector<SceneChangeEvent> &events) = 0;
};

struct bu_cache;

// DbiState is the in-memory mirror of a BRL-CAD .g database.  It drives all
// drawing and selection logic in the qged/libqtcad stack.
//
// Thread-safety: MAIN THREAD ONLY.  DbiState holds a struct resource *, raw
// pointers into librt data structures, and STL containers that are not
// guarded by any mutex.  All methods must be called from the application main
// thread.  See DBI_TODO.md §12.4 L1-L2 for the planned locking approach.
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

	unsigned long long path_hash(const std::vector<unsigned long long> &path, size_t max_len);

	void clear_cache(struct directory *dp);

	BViewState *get_view_state(struct bview *);

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

	// Database Instance associated with this container
	struct ged *gedp = NULL;
	struct db_i *dbip = NULL;

	bool need_update_nref = true;

	// Phase 3 object model: GObj instances keyed by object-name hash.
	// Populated alongside the flat maps during update_dp(); CombInst
	// children are owned by GObj::cv and accessed via the GObj.
	std::unordered_map<unsigned long long, GObj *> gobjs;

	// Convenience accessor for gobjs (returns nullptr if not found)
	const GObj *get_gobj(unsigned long long hash) const {
	    auto it = gobjs.find(hash);
	    return (it != gobjs.end()) ? it->second : nullptr;
	}

	// Debugging methods for printing out current states - the use of hashes
	// means direct inspection of most data isn't informative, so we provide
	// convenience methods that decode it to user-comprehensible info.
	void print_dbi_state(struct bu_vls *o = NULL, bool report_view_states = false);

	// Observer registration
	void add_observer(IDbiObserver *);
	void remove_observer(IDbiObserver *);
	void add_scene_observer(ISceneObserver *);
	void remove_scene_observer(ISceneObserver *);

	// SelectionSet management
	// get_selection_set: returns the named set (or default if name is null/empty).
	//   Creates a new empty set if the name does not yet exist.
	SelectionSet *get_selection_set(const char *name = nullptr);
	// get_selection_sets: returns all sets whose names match the glob pattern.
	//   If pattern is null or empty, returns the default set.
	//   If pattern contains a '*', returns all matching named sets.
	//   Otherwise returns the single named set if it exists.
	std::vector<SelectionSet *> get_selection_sets(const char *pattern = nullptr);
	void          add_selection_set(const char *name);
	void          remove_selection_set(const char *name = nullptr);
	std::vector<std::string> list_selection_sets() const;

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

	std::vector<IDbiObserver *>    dbi_observers_;
	std::vector<ISceneObserver *>  scene_observers_;
	void notify_dbi_observers(const std::vector<DbiChangeEvent> &);
	void notify_scene_observers(const std::vector<SceneChangeEvent> &);

	std::unique_ptr<SelectionSet> default_selection_set_;
	std::unordered_map<std::string, std::unique_ptr<SelectionSet>> selection_sets_;

	// GObj and CombInst need access to private DbiState internals (res, dcache)
	friend class GObj;
	friend class CombInst;
	// BViewState needs access to res for per-object draw update data
	friend class BViewState;
};


// In-memory representation of a comb tree instance from a .g database.
// Each unique instance of an object inside one comb is captured here.
class GED_EXPORT CombInst {

    public:
	CombInst(DbiState *dbis, const char *p_name, const char *o_name, unsigned long long icnt, int i_op, matp_t i_mat);
	~CombInst();

	// Return the BRL-CAD op type of this instance
	db_op_t bool_op();

	// Calculate bounding box of the instance
	void bbox(point_t *min, point_t *max);

	// Object name and instance name strings
	std::string cname; // Name of parent comb
	std::string oname; // Name of instanced object
	std::string iname; // Unique instance name (with id-based suffix if needed)
	unsigned long long id = 0;

	unsigned long long chash; // Hash of parent comb name
	unsigned long long ohash; // Hash of instanced object name
	unsigned long long ihash; // Hash of unique instance identifier

	int boolean_op; // OP_UNION, OP_SUBTRACT or OP_INTERSECT

	mat_t m;
	bool non_default_matrix = false;

	DbiState *d;
};

// In-memory representation of a BRL-CAD geometry database object.
// Combs and solids are both represented by GObj instances.
class GED_EXPORT GObj {

    public:
	GObj(DbiState *d_s, struct directory *dp_i);
	~GObj();

	void bbox(point_t *min, point_t *max);

	std::string name;
	unsigned long long hash = 0;

	int c_inherit = 0;
	int region_id = -1;
	int region_flag = 0;

	struct bu_color color;
	bool color_set = false;

	// Comb tree instances (populated for comb objects)
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

#endif

/*
 * C surface — accessible from both C and C++ translation units.
 *
 * All functions take a `struct ged *` so that callers never need to
 * manage DbiState, SelectionSet, or GObj pointers directly.  The
 * `dbi_state` field of the ged struct is cast to the appropriate C++
 * type internally; from C it remains opaque.
 *
 * Naming convention: ged_dbi_*  for database-state queries,
 *                    ged_selection_* for selection-set operations.
 */

__BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Database-state helpers                                              */
/* ------------------------------------------------------------------ */

/**
 * Trigger a full DbiState update (same as ged_db_dirty/ged_update_nref
 * but acting on the new DBI layer).  Returns a change-flags bitmask
 * (GED_DBISTATE_DB_CHANGE | GED_DBISTATE_VIEW_CHANGE) or 0 on error.
 */
GED_EXPORT extern unsigned long long ged_dbi_update(struct ged *gedp);

/**
 * Return non-zero if @a hash is a valid object-name hash in the
 * current DbiState (i.e. it appears in d_map).
 */
GED_EXPORT extern int ged_dbi_valid_hash(struct ged *gedp, unsigned long long hash);

/**
 * Look up the name hash for a named object.  Returns 0 if the object
 * is not found in the current DbiState.
 */
GED_EXPORT extern unsigned long long ged_dbi_hash_of(struct ged *gedp, const char *name);

/**
 * Fill @a out with the name hashes of all top-level objects in the
 * current DbiState.  @a out must be a valid, initialised bu_ptbl.
 * Returns the count of tops items added, or -1 on error.
 *
 * Each entry stored in @a out is an unsigned long long value cast to
 * (void *) — callers should cast back with (unsigned long long)(uintptr_t).
 */
GED_EXPORT extern int ged_dbi_tops(struct ged *gedp, struct bu_ptbl *out);

/* ------------------------------------------------------------------ */
/* GObj helpers (read-only access to the Phase-3 object model)        */
/* ------------------------------------------------------------------ */

/**
 * Return non-zero if @a name refers to a combination object in the
 * current DbiState's gobjs map.  Returns -1 if the object is not found.
 */
GED_EXPORT extern int ged_dbi_gobj_is_comb(struct ged *gedp, const char *name);

/**
 * Return the region_id of @a name, or -1 if the object is not found.
 */
GED_EXPORT extern int ged_dbi_gobj_region_id(struct ged *gedp, const char *name);

/**
 * Fill @a r, @a g, @a b with the explicitly-set color of @a name.
 * Returns 1 if a color is set, 0 if no color is set, -1 if not found.
 */
GED_EXPORT extern int ged_dbi_gobj_color(struct ged *gedp, const char *name,
					  unsigned char *r, unsigned char *g_out, unsigned char *b);

/**
 * Return the number of direct child instances of @a name in the
 * current DbiState, or -1 if the object is not found / is not a comb.
 */
GED_EXPORT extern int ged_dbi_gobj_child_count(struct ged *gedp, const char *name);

/* ------------------------------------------------------------------ */
/* SelectionSet helpers                                                */
/* ------------------------------------------------------------------ */

/**
 * Select the path given by @a path_str in the named selection set
 * (@a sname == NULL selects the default set).  Returns 1 if the
 * selection changed, 0 if it was already selected, -1 on error.
 */
GED_EXPORT extern int ged_selection_select(struct ged *gedp, const char *sname,
					    const char *path_str);

/**
 * Deselect @a path_str from the named selection set.  Returns 1 if
 * the selection changed, 0 if it was not selected, -1 on error.
 */
GED_EXPORT extern int ged_selection_deselect(struct ged *gedp, const char *sname,
					      const char *path_str);

/**
 * Return non-zero if @a path_str is currently selected in the named
 * selection set, 0 if not, -1 on error.
 */
GED_EXPORT extern int ged_selection_is_selected(struct ged *gedp, const char *sname,
						 const char *path_str);

/**
 * Clear all selections from the named selection set.
 */
GED_EXPORT extern void ged_selection_clear(struct ged *gedp, const char *sname);

/**
 * Return the number of currently-selected paths in the named selection
 * set, or -1 on error.
 */
GED_EXPORT extern int ged_selection_count(struct ged *gedp, const char *sname);

/**
 * Fill @a out with the currently-selected path strings in the named
 * selection set.  Each entry is a bu_strdup'd C string; the caller is
 * responsible for freeing each entry and the table contents.  @a out
 * must be a valid, initialised bu_ptbl.  Returns the count added, or
 * -1 on error.
 */
GED_EXPORT extern int ged_selection_list_paths(struct ged *gedp, const char *sname,
					        struct bu_ptbl *out);

__END_DECLS

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
