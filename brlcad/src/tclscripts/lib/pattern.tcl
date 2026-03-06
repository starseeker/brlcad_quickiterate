#                     P A T T E R N . T C L
# BRL-CAD
#
# Copyright (c) 2004-2025 United States Government as represented by
# the U.S. Army Research Laboratory.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# version 2.1 as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this file; see the file named COPYING for more
# information.
#
###
#
#	procedures to build duplicates of objects in a specified pattern
#
###
#	C R E A T E _ N E W _ N A M E
#
# procedure to create a new unique name for an MGED database object.
#
# Arguments:
#	leaf - the input base name
#	sstr - string to be substituted for in the base name
#	rstr - the replacement string (substituted for "sstr" in base name)
#	increment - amount to increment the number in a name of the form xxx.s15.yyy or xxx.r2.yyy
#
# The increment is performed after the string substitution.
#
# If after the above substitutions a unique name is not produced, an "_x" is suffixed to the
# resulting name (where x is an integer)
#
# Returns a new unique object name

namespace eval cadwidgets {
    if {![info exists ged]} {
	set ged db
    }

    if {![info exists mgedFlag]} {
	set mgedFlag 1
    }
}

if {![info exists local2base]} {
    set local2base 1.0
}

###
#   _ C L O N E _ I N V O K E
#
# Helper: call the libged 'clone' command in the correct way depending on
# whether we are inside MGED (mgedFlag=1, direct command) or a libged
# application (mgedFlag=0, via the $::cadwidgets::ged handle).
#
proc _clone_invoke { args } {
    if {$::cadwidgets::mgedFlag} {
	return [eval clone $args]
    } else {
	return [eval $::cadwidgets::ged clone $args]
    }
}

proc exists_wrapper {args} {
    if {$::cadwidgets::mgedFlag} {
	eval exists $args
    } else {
	eval $::cadwidgets::ged exists $args
    }
}

proc regdef_wrapper {args} {
    if {$::cadwidgets::mgedFlag} {
	eval regdef $args
    } else {
	eval $::cadwidgets::ged regdef $args
    }
}

proc create_new_name { leaf sstr rstr increment } {
    if { [string length $sstr] != 0 } {
	set prefix ""
	set suffix ""
	set sstr_index [string first $sstr $leaf]
	if { $sstr_index >= 0 } {
	    set prefix [string range $leaf 0 [expr $sstr_index - 1]]
	    set suffix [string range $leaf [expr $sstr_index + [string length $sstr]] end]
	    set new_name ${prefix}${rstr}${suffix}
	} else {
	    set new_name $leaf
	}
    } else {
	set new_name $leaf
    }
    if { $increment > 0 } {
	if { [regexp {([^\.]*)(\.)([sr])([0-9]*)(.*)} $new_name the_match tag a_dot obj_type num tail] } {
	    set new_name "${tag}.${obj_type}[expr $num + $increment]${tail}"
	}
    }

    # make sure this name doesn't already exist
    set dummy 0
    set base_name $new_name

    while {[exists_wrapper $new_name] == 1} {
	incr dummy
	set max_len [expr 16 - [string length $dummy] - 1]
	set new_name "[string range $base_name 0 $max_len]_$dummy"
    }

    return $new_name
}

proc copy_tree { args } {
    set usage "Usage:\n\t copy_tree  \[-s source_string replacement_string | -i increment\] \[-primitives\] tree_to_be_copied"
    set sstr ""
    set rstr ""
    set increment 0
    set depth "regions"
    set tree ""
    set opt_str ""
    set argc [llength $args]
    if { $argc < 1 || $argc > 7 } {
	error $usage
    }
    incr argc -1
    set index 0
    while { $index < $argc } {
	set opt [lindex $args $index]
	switch -- $opt {
	    "-s" {
		incr index
		set sstr [lindex $args $index]
		incr index
		set rstr [lindex $args $index]
		if { [string length $sstr] && [string length $rstr] } {
		    set opt_str "$opt_str -s $sstr $rstr"
		}
	    }
	    "-i" {
		incr index
		set increment [lindex $args $index]
		set opt_str "$opt_str -i $increment"
	    }
	    "-primitives" {
		set depth "primitives"
		set opt_str "$opt_str -primitives"
	    }
	    "-regions" {
		set depth "regions"
	    }
	    default {
		error "copy_tree Unrecognized option: $opt"
	    }
	}
	incr index
    }

    set tree [lindex $args $index]

    set op [lindex $tree 0]
    switch -- $op {
	"l" {
	    set leaf [lindex $tree 1]
	    if { [catch {$::cadwidgets::ged get $leaf} leaf_db] } {
		puts "WARNING: $leaf does not actually exist"
		puts "\n$::cadwidgets::ged get $leaf"
		return [list "l" $leaf]
	    }
	    set type [lindex $leaf_db 0]
	    if { $type != "comb" && $depth != "primitives" } {
		# we have reached a leaf primitive, but we don't want to copy the primitives
		return $tree
	    }
	    if { [llength $tree] == 3 } {
		set old_mat [lindex $tree 2]
	    } else {
		set old_mat [mat_idn]
	    }

	    # create new name for this object
	    set new_name [create_new_name $leaf $sstr $rstr $increment]

	    if { $type != "comb" } {
		# this is a primitive
		if { [catch {eval $::cadwidgets::ged put $new_name $leaf_db} ret] } {
		    error "Cannot create copy of primitive $leaf as $new_name\n\t$ret"
		}
	    } else {
		#this is a combination
		set index [lsearch -exact $leaf_db "region"]
		incr index
		set region 0
		if { [lindex $leaf_db $index] == "yes" } {
		    # this is a region
		    set region 1
		    if { $depth == "regions" } {
			# just copy the region to the new name
			if { [catch {eval $::cadwidgets::ged put $new_name $leaf_db} ret] } {
			    error "Cannot create copy of region $leaf as $new_name\n\t$ret"
			}
			# adjust region id
			set regdef [regdef_wrapper]
			set id [lindex $regdef 1]
			if { [catch {$::cadwidgets::ged adjust $new_name id $id} ret] } {
			    error "Cannot adjust region ident for $new_name!!!\n\t$ret"
			}
			incr id
			regdef_wrapper $id
			return [list "l" $new_name $old_mat]
		    }
		}
		set index [lsearch -exact $leaf_db "tree"]
		if { $index < 0 } {
		    error "Combination $leaf has no Boolean tree!!!"
		}
		incr index
		set old_tree [lindex $leaf_db $index]
		set new_tree [eval copy_tree $opt_str [list $old_tree]]
		if { [catch {eval $::cadwidgets::ged put $new_name $leaf_db} ret] } {
		    error "Cannot create copy of combination $leaf as $new_name\n\t$ret"
		}
		if { [catch {$::cadwidgets::ged adjust $new_name tree $new_tree} ret] } {
		    error "Cannot adjust tree in new combination named $new_name\n\t$ret"
		}
		if { $region } {
		    # set region ident according to regdef
		    set regdef [regdef_wrapper]
		    set id [lindex $regdef 1]
		    if { [catch {$::cadwidgets::ged adjust $new_name id $id} ret] } {
			error "Cannot adjust ident number for region ($new_name)!!!!\n\t$ret"
		    }
		    incr id
		    regdef_wrapper $id
		}
	    }
	    return [list "l" $new_name $old_mat]
	}
	"-" -
	"+" -
	"n" -
	"u" {
	    set left [lindex $tree 1]
	    set right [lindex $tree 2]
	    set new_left [eval copy_tree $opt_str [list $left]]
	    set new_right [eval copy_tree $opt_str [list $right]]
	    return [list $op $new_left $new_right]
	}
    }
}

proc copy_obj { args } {
    set usage "Usage:\n\tcopy_obj \[-s source_string replacement_string | -i increment\] \[-primitives\] object_to_be_copied"
    set sstr ""
    set rstr ""
    set increment 0
    set depth "regions"
    set obj ""
    set argc [llength $args]
    if { $argc < 1 || $argc > 7 } {
	puts "Error in command: copy_obj $args\nwrong number of arguments ($argc)"
	error $usage
    }
    incr argc -1
    set opt_str ""
    set index 0
    while { $index < $argc } {
	set opt [lindex $args $index]
	switch -- $opt {
	    "-s" {
		incr index
		set sstr [lindex $args $index]
		incr index
		set rstr [lindex $args $index]
		if { [string length $sstr] && [string length $rstr] } {
		    set opt_str "$opt_str -s $sstr $rstr"
		}
	    }
	    "-i" {
		incr index
		set increment [lindex $args $index]
		set opt_str "$opt_str -i $increment"
	    }
	    "-primitives" {
		set depth "primitives"
		set opt_str "$opt_str -primitives"
	    }
	    "-regions" {
		set depth "regions"
		set opt_str "$opt_str -regions"
	    }
	    default {
		error "Copy_obj: Unrecognized option: $opt"
	    }
	}
	incr index
    }

    set obj [lindex $args $index]
    if { [catch {$::cadwidgets::ged get $obj} obj_db] } {
	error "Cannot retrieve object $obj\n\t$obj_db"
    }

    set type [lindex $obj_db 0]
    if { $type != "comb" } {
	# object is a primitive
	if { $depth != "primitives" } {
	    error "Trying to copy a primitive ($obj) with depth at regions!!!!"
	}
	# just copy the primitive to a new name
	set new_name [create_new_name $obj $sstr $rstr $increment]
	if { [catch {eval $::cadwidgets::ged put $new_name $obj_db} ret] } {
	    error "cannot copy $obj to $new_name!!!\n\t$ret"
	}
	return $new_name
    }

    # this is a combination
    set region 0
    set region_idx [lsearch -exact $obj_db "region"]
    if { $region_idx < 0 } {
	error "Combination ($obj) does not have a region attribute!!!"
    }
    incr region_idx
    if { [lindex $obj_db $region_idx] == "yes" } {
	# this is a region
	set region 1
	if { $depth == "regions" } {
	    # just copy this region to a new name
	    set new_name [create_new_name $obj $sstr $rstr $increment]
	    if { [catch {eval $::cadwidgets::ged put $new_name $obj_db} ret] } {
		error "Cannot copy $obj to $new_name!!!\n\t$ret"
	    }
	    set regdef [regdef_wrapper]
	    set id [lindex $regdef 1]
	    if { [catch {$::cadwidgets::ged adjust $new_name id $id} ret] } {
		error "Cannot adjust ident for region ($new_name)!!!!\n\t$ret"
	    }
	    incr id
	    regdef_wrapper $id
	    return $new_name
	}

    }

    # copy the tree, copy the combination, then adjust the tree
    set tree_idx [lsearch -exact $obj_db "tree"]
    if { $tree_idx < 0 } {
	error "Region ($obj) has no tree!!!!"
    }
    incr tree_idx
    set tree [lindex $obj_db $tree_idx]
    set new_tree [eval copy_tree $opt_str [list $tree]]
    set new_name [create_new_name $obj $sstr $rstr $increment]
    if { [catch {eval $::cadwidgets::ged put $new_name $obj_db} ret] } {
	error "Cannot copy $obj to $new_name!!!\n\t$ret"
    }
    if { [catch {$::cadwidgets::ged adjust $new_name tree $new_tree} ret] } {
	error "Cannot adjust tree on new combination ($new_name)!!!!\n\t$ret"
    }

    if { $region } {
	# set region ident according to regdef
	set regdef [regdef_wrapper]
	set id [lindex $regdef 1]
	if { [catch {$::cadwidgets::ged adjust $new_name id $id} ret] } {
	    error "Cannot adjust ident number for region ($new_name)!!!!\n\t$ret"
	}
	incr id
	regdef_wrapper $id
    }
    return $new_name
}


proc pattern_rect { args } {
    global local2base

    set usage "Usage:\n\tpattern_rect \[-top|-regions|-primitives\] \[-g group_name\] \
		 \[-xdir { x y z }\] \[-ydir { x y z }\] \[-zdir { x y z }\] \
		\[-nx num_x -dx delta_x | -lx list_of_x_values\]\n\t\t \
		\[-ny num_y -dy delta_y | -ly list_of_y_values\] \[-nz num_z -dz delta_z | -lz list_of_z_values\] \
		\[-s source_string replacement_string\] \[-i increment\]  object1 \[object2 object3 ...\]"

    init_vmath

    set opt_str ""
    set group_name ""
    set group_list {}
    set index 0
    set depth top
    set xdir { 1 0 0 }
    set ydir { 0 1 0 }
    set zdir { 0 0 1 }
    set num_x 0
    set num_y 0
    set num_z 0
    set delta_x 0
    set delta_y 0
    set delta_z 0
    set list_x {}
    set list_y {}
    set list_z {}
    set sstr ""
    set rstr ""
    set increment 0
    set inc 1
    set objs {}
    set feed_name ""
    set argc [llength $args]
    while { $index < $argc } {
	set opt [lindex $args $index]
	switch -- $opt {
	    "-top" {
		set depth top
		set opt_str "$opt_str -top"
		incr index
	    }
	    "-regions" {
		set depth regions
		set opt_str "$opt_str -regions"
		incr index
	    }
	    "-primitives" {
		set depth primitives
		set opt_str "$opt_str -primitives"
		incr index
	    }
	    "-g" {
		incr index
		set group_name [lindex $args $index]
		incr index
	    }
	    "-xdir" {
		incr index
		set xdir [lindex $args $index]
		incr index
	    }
	    "-ydir" {
		incr index
		set ydir [lindex $args $index]
		incr index
	    }
	    "-zdir" {
		incr index
		set zdir [lindex $args $index]
		incr index
	    }
	    "-nx" {
		incr index
		set num_x [lindex $args $index]
		incr index
	    }
	    "-ny" {
		incr index
		set num_y [lindex $args $index]
		incr index
	    }
	    "-nz" {
		incr index
		set num_z [lindex $args $index]
		incr index
	    }
	    "-dx" {
		incr index
		set delta_x [lindex $args $index]
		incr index
	    }
	    "-dy" {
		incr index
		set delta_y [lindex $args $index]
		incr index
	    }
	    "-dz" {
		incr index
		set delta_z [lindex $args $index]
		incr index
	    }
	    "-lx" {
		incr index
		set list_x [lindex $args $index]
		incr index
	    }
	    "-ly" {
		incr index
		set list_y [lindex $args $index]
		incr index
	    }
	    "-lz" {
		incr index
		set list_z [lindex $args $index]
		incr index
	    }
	    "-s" {
		incr index
		set sstr [lindex $args $index]
		incr index
		set rstr [lindex $args $index]
		incr index
		if { [string length $rstr] && [string length $sstr] } {
		    set opt_str "$opt_str -s $sstr $rstr"
		}
	    }
	    "-i" {
		incr index
		set increment [lindex $args $index]
		set inc $increment
		incr index
	    }
	    "-feed_name" {
		incr index
		set feed_name [lindex $args $index]
		incr index
	    }
	    default {
		set objs [lrange $args $index end]
		set index $argc
	    }
	}
    }

    if { [llength $objs] < 1 } {
	error "no objects specified!!!\n$usage"
    }

    if {	[llength $list_x] == 0 && $num_x == 0 &&
		[llength $list_y] == 0 && $num_y == 0 &&
		[llength $list_z] == 0 && $num_z == 0 } {
	error "no X, Y, or Z values provided!!!!\n$usage"
    }

    # -----------------------------------------------------------------
    # Delegate all three depths to libged clone C++ implementation.
    # -----------------------------------------------------------------
    set clone_cmd [list --rect --depth $depth]
    if { $group_name ne "" } { lappend clone_cmd -g $group_name }
    if { $increment != 0 }   { lappend clone_cmd -i $increment }
    if { [string length $sstr] > 0 && [string length $rstr] > 0 } {
	lappend clone_cmd -s $sstr $rstr
    }
    lappend clone_cmd --xdir $xdir --ydir $ydir --zdir $zdir
    if { [llength $list_x] > 0 } {
	lappend clone_cmd --lx [join $list_x " "]
    } elseif { $num_x > 0 } {
	lappend clone_cmd --nx $num_x --dx $delta_x
    }
    if { [llength $list_y] > 0 } {
	lappend clone_cmd --ly [join $list_y " "]
    } elseif { $num_y > 0 } {
	lappend clone_cmd --ny $num_y --dy $delta_y
    }
    if { [llength $list_z] > 0 } {
	lappend clone_cmd --lz [join $list_z " "]
    } elseif { $num_z > 0 } {
	lappend clone_cmd --nz $num_z --dz $delta_z
    }
    foreach obj $objs { lappend clone_cmd $obj }

    if { $feed_name ne "" } { catch { $feed_name configure -steps 1 } }
    set result [eval _clone_invoke $clone_cmd]
    if { $feed_name ne "" } { catch { $feed_name step }; update idletasks }

    if { $group_name ne "" } {
	if { $::cadwidgets::mgedFlag } {
	    draw $group_name
	} else {
	    $::cadwidgets::ged draw $group_name
	    $::cadwidgets::ged freezeGUI 0
	}
    }
    return $result
}


proc pattern_sph { args } {
    global DEG2RAD RAD2DEG M_PI M_PI_2 local2base

    init_vmath
    set usage "pattern_sph \[-top | -regions | -primitives\] \[-g group_name\] \[-s source_string replacement_string\] \
		\[-i tag_number_increment\] \[-center_pat {x y z}\] \[-center_obj {x y z}\] \[-rotaz\] \[-rotel\] \
		\[-naz num_az -daz delta_az | -laz list_of_azimuths\] \
		\[-nel num_el -del delta_el | -lel list_of_elevations\] \
		\[-nr num_r -dr delta_r | -lr list_of_radii\] \
		\[-start_az starting_azimuth \] \[-start_el starting_elevation\] \[-start_r starting_radius\] \
		\[-raz\] \[-rel\] \
		object1 \[object2 object3 ...\]"

    set objs {}
    # angles stored in degrees (passed directly to C++ clone which expects degrees)
    set start_az_deg 0
    set start_el_deg -90.0
    set start_r 0
    set rot_az 0
    set rot_el 0
    set depth "top"
    set center_pat { 0 0 0 }
    set center_obj { 0 0 0 }
    set group_name ""
    set group_list {}
    set sstr ""
    set rstr ""
    set increment 0
    set inc 1
    set num_az 0
    set num_el 0
    set num_r 0
    set delta_az_deg 0
    set delta_el_deg 0
    set delta_r 0
    set list_az_deg {}
    set list_el_deg {}
    set list_r {}
    set opt_str ""
    set argc [llength $args]
    set index 0
    set feed_name ""
    while { $index < $argc } {
	set opt [lindex $args $index]
	switch -- $opt {
	    "-start_r" {
		incr index
		set start_r [lindex $args $index]
		incr index
	    }
	    "-top" {
		set depth top
		set opt_str "$opt_str -top"
		incr index
	    }
	    "-regions" {
		set depth regions
		set opt_str "$opt_str -regions"
		incr index
	    }
	    "-primitives" {
		set depth primitives
		set opt_str "$opt_str -primitives"
		incr index
	    }
	    "-g" {
		incr index
		set group_name [lindex $args $index]
		incr index
	    }
	    "-s" {
		incr index
		set sstr [lindex $args $index]
		incr index
		set rstr [lindex $args $index]
		incr index
		if { [string length $rstr] && [string length $sstr] } {
		    set opt_str "$opt_str -s $sstr $rstr"
		}
	    }
	    "-i" {
		incr index
		set increment [lindex $args $index]
		set inc $increment
		incr index
	    }
	    "-start_az" {
		incr index
		set start_az_deg [lindex $args $index]
		incr index
	    }
	    "-start_el" {
		incr index
		set start_el_deg [lindex $args $index]
		incr index
	    }
	    "-center_pat" {
		incr index
		set center_pat [lindex $args $index]
		incr index
	    }
	    "-center_obj" {
		incr index
		set center_obj [lindex $args $index]
		incr index
	    }
	    "-rotaz" {
		set rot_az 1
		incr index
	    }
	    "-rotel" {
		set rot_el 1
		incr index
	    }
	    "-naz" {
		incr index
		set num_az [lindex $args $index]
		incr index
	    }
	    "-nel" {
		incr index
		set num_el [lindex $args $index]
		incr index
	    }
	    "-nr" {
		incr index
		set num_r [lindex $args $index]
		incr index
	    }
	    "-daz" {
		incr index
		set delta_az_deg [lindex $args $index]
		incr index
	    }
	    "-del" {
		incr index
		set delta_el_deg [lindex $args $index]
		incr index
	    }
	    "-dr" {
		incr index
		set delta_r [lindex $args $index]
		incr index
	    }
	    "-laz" {
		incr index
		set list_az_deg [lindex $args $index]
		incr index
	    }
	    "-lel" {
		incr index
		set list_el_deg [lindex $args $index]
		incr index
	    }
	    "-lr" {
		incr index
		set list_r [lindex $args $index]
		incr index
	    }
	    "-feed_name" {
		incr index
		set feed_name [lindex $args $index]
		incr index
	    }
	    default {
		set objs [lrange $args $index end]
		set index $argc
	    }
	}
    }

    if { [llength $objs] < 1 } { error "no objects specified\n$usage" }
    if {	[llength $list_az_deg] == 0 && $num_az == 0 &&
		[llength $list_el_deg] == 0 && $num_el == 0 &&
		[llength $list_r] == 0 && $num_r == 0 } {
	error "No azimuth, elevation, or radii provided!!!\n$usage"
    }

    # -----------------------------------------------------------------
    # Delegate all three depths to libged clone C++ implementation.
    # -----------------------------------------------------------------
    set clone_cmd [list --sph --depth $depth]
    if { $group_name ne "" } { lappend clone_cmd -g $group_name }
    if { $increment != 0 }   { lappend clone_cmd -i $increment }
    if { [string length $sstr] > 0 && [string length $rstr] > 0 } {
	lappend clone_cmd -s $sstr $rstr
    }
    lappend clone_cmd --center-pat $center_pat --center-obj $center_obj
    if { $rot_az } { lappend clone_cmd --rotaz }
    if { $rot_el } { lappend clone_cmd --rotel }
    lappend clone_cmd --start-az $start_az_deg --start-el $start_el_deg --start-r $start_r
    if { [llength $list_az_deg] > 0 } {
	lappend clone_cmd --laz [join $list_az_deg " "]
    } elseif { $num_az > 0 } {
	lappend clone_cmd --naz $num_az --daz $delta_az_deg
    }
    if { [llength $list_el_deg] > 0 } {
	lappend clone_cmd --lel [join $list_el_deg " "]
    } elseif { $num_el > 0 } {
	lappend clone_cmd --nel $num_el --del $delta_el_deg
    }
    if { [llength $list_r] > 0 } {
	lappend clone_cmd --lr [join $list_r " "]
    } elseif { $num_r > 0 } {
	lappend clone_cmd --nr $num_r --dr $delta_r
    }
    foreach obj $objs { lappend clone_cmd $obj }

    if { $feed_name ne "" } { catch { $feed_name configure -steps 1 } }
    set result [eval _clone_invoke $clone_cmd]
    if { $feed_name ne "" } { catch { $feed_name step }; update idletasks }

    if { $group_name ne "" } {
	if { $::cadwidgets::mgedFlag } {
	    draw $group_name
	} else {
	    $::cadwidgets::ged draw $group_name
	    $::cadwidgets::ged freezeGUI 0
	}
    }
    return $result
}


proc pattern_cyl { args } {
    global DEG2RAD M_PI M_PI_2 local2base

    init_vmath

    set usage "pattern_cyl \[-top | -regions | -primitives\] \[-g group_name\] \[-s source_string replacement_string\] \
		\[-i tag_number_increment\] \[-rot\] \[-center_obj {x y z}\] \[-center_base {x y z}\] \[-height_dir {x y z}\] \
		\[-start_az_dir {x y z}\] \
		\[-naz num_az -daz delta_az | -laz list_of_azimuths\] \
		\[-sr start_r\] \
		\[-nr num_r -dr delta_r | -lr list_of_radii\] \
		\[-sh start_h\] \
		\[-nh num_h -dh delta_h | -lh list_of_heights\] \
		object1 \[object2 object3 ...\]"

    set objs {}
    set do_rot 0
    set depth "top"
    set group_name ""
    set group_list {}
    set sstr ""
    set rstr ""
    set increment 0
    set inc 1
    # angles stored in degrees (passed directly to C++ clone which expects degrees)
    set start_az_deg 0
    set start_az_dir { 1 0 0 }
    set start_r 0
    set start_h 0
    set num_az 0
    set num_r 0
    set num_h 0
    set delta_az_deg 0
    set delta_r 0
    set delta_h 0
    set list_az_deg {}
    set list_r {}
    set list_h {}
    set center_base { 0 0 0 }
    set center_obj { 0 0 0 }
    set height_dir { 0 0 1 }
    set opt_str ""
    set argc [llength $args]
    set index 0
    set feed_name ""
    while { $index < $argc } {
	set opt [lindex $args $index]
	switch -- $opt {
	    "-top" {
		set depth top
		set opt_str "$opt_str -top"
		incr index
	    }
	    "-regions" {
		set depth regions
		set opt_str "$opt_str -regions"
		incr index
	    }
	    "-primitives" {
		set depth primitives
		set opt_str "$opt_str -primitives"
		incr index
	    }
	    "-g" {
		incr index
		set group_name [lindex $args $index]
		incr index
	    }
	    "-s" {
		incr index
		set sstr [lindex $args $index]
		incr index
		set rstr [lindex $args $index]
		incr index
		if { [string length $rstr] && [string length $sstr] } {
		    set opt_str "$opt_str -s $sstr $rstr"
		}
	    }
	    "-i" {
		incr index
		set increment [lindex $args $index]
		set inc $increment
		incr index
	    }
	    "-start_az" {
		incr index
		set start_az_deg [lindex $args $index]
		incr index
	    }
	    "-start_az_dir" {
		incr index
		set start_az_dir [lindex $args $index]
		incr index
	    }
	    "-rot" {
		set do_rot 1
		incr index
	    }
	    "-center_obj" {
		incr index
		set center_obj [lindex $args $index]
		incr index
	    }
	    "-center_base" {
		incr index
		set center_base [lindex $args $index]
		incr index
	    }
	    "-height_dir" {
		incr index
		set height_dir [lindex $args $index]
		incr index
	    }
	    "-naz" {
		incr index
		set num_az [lindex $args $index]
		incr index
	    }
	    "-daz" {
		incr index
		set delta_az_deg [lindex $args $index]
		incr index
	    }
	    "-laz" {
		incr index
		set list_az_deg [lindex $args $index]
		incr index
	    }
	    "-nr" {
		incr index
		set num_r [lindex $args $index]
		incr index
	    }
	    "-dr" {
		incr index
		set delta_r [lindex $args $index]
		incr index
	    }
	    "-nh" {
		incr index
		set num_h [lindex $args $index]
		incr index
	    }
	    "-dh" {
		incr index
		set delta_h [lindex $args $index]
		incr index
	    }
	    "-lh" {
		incr index
		set list_h [lindex $args $index]
		incr index
	    }
	    "-lr" {
		incr index
		set list_r [lindex $args $index]
		incr index
	    }
	    "-sr" {
		incr index
		set start_r [lindex $args $index]
		incr index
	    }
	    "-sh" {
		incr index
		set start_h [lindex $args $index]
		incr index
	    }
	    "-feed_name" {
		incr index
		set feed_name [lindex $args $index]
		incr index
	    }
	    default {
		set objs [lrange $args $index end]
		set index $argc
	    }
	}
    }

    if { [llength $objs] < 1 } { error "no objects specified\n$usage" }
    if { 	[llength $list_az_deg] == 0 && $num_az == 0 &&
		[llength $list_r] == 0 && $num_r == 0 &&
		[llength $list_h] == 0 && $num_h == 0 } {
	error "No azimuth, radii, or heights provided!!!!\n$usage"
    }

    # -----------------------------------------------------------------
    # Delegate all three depths to libged clone C++ implementation.
    # -----------------------------------------------------------------
    set clone_cmd [list --cyl --depth $depth]
    if { $group_name ne "" } { lappend clone_cmd -g $group_name }
    if { $increment != 0 }   { lappend clone_cmd -i $increment }
    if { [string length $sstr] > 0 && [string length $rstr] > 0 } {
	lappend clone_cmd -s $sstr $rstr
    }
    if { $do_rot }           { lappend clone_cmd --rot }
    lappend clone_cmd --center-obj $center_obj --center-base $center_base
    lappend clone_cmd --height-dir $height_dir --start-az-dir $start_az_dir
    lappend clone_cmd --start-az $start_az_deg --start-r $start_r --start-h $start_h
    if { [llength $list_az_deg] > 0 } {
	lappend clone_cmd --laz [join $list_az_deg " "]
    } elseif { $num_az > 0 } {
	lappend clone_cmd --naz $num_az --daz $delta_az_deg
    }
    if { [llength $list_r] > 0 } {
	lappend clone_cmd --lr [join $list_r " "]
    } elseif { $num_r > 0 } {
	lappend clone_cmd --nr $num_r --dr $delta_r
    }
    if { [llength $list_h] > 0 } {
	lappend clone_cmd --lh [join $list_h " "]
    } elseif { $num_h > 0 } {
	lappend clone_cmd --nh $num_h --dh $delta_h
    }
    foreach obj $objs { lappend clone_cmd $obj }

    if { $feed_name ne "" } { catch { $feed_name configure -steps 1 } }
    set result [eval _clone_invoke $clone_cmd]
    if { $feed_name ne "" } { catch { $feed_name step }; update idletasks }

    if { $group_name ne "" } {
	if { $::cadwidgets::mgedFlag } {
	    draw $group_name
	} else {
	    $::cadwidgets::ged draw $group_name
	    $::cadwidgets::ged freezeGUI 0
	}
    }
    return $result
}


# Local Variables:
# mode: Tcl
# tab-width: 8
# c-basic-offset: 4
# tcl-indent-level: 4
# indent-tabs-mode: t
# End:
# ex: shiftwidth=4 tabstop=8
