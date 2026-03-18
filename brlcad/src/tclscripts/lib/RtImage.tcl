#                          R T I M A G E . T C L
# BRL-CAD
#
# Copyright (c) 1998-2025 United States Government as represented by
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
# Description -
#	This creates rtwizard types of images.
#

package provide cadwidgets::RtImage 1.0

proc ::pid_wait { pid } {
    if {$::tcl_platform(platform) == "windows"} {
	set task_cmd [auto_execok tasklist]
	set task_args [list $task_cmd /FI "PID eq $pid" /FI {STATUS eq running} "/NH"]
	set task_list "$pid"
	while {[string match "*$pid*" $task_list]} {
	    catch {eval exec $task_args} task_list
	    after 50
	}
    } else {
	while {![catch {exec kill -0 $pid} pid_results]} {
	    after 50
	}
    }
}

namespace eval cadwidgets {

# rtimage_file --
#
#   File-based (libdm-free) rendering path.  Uses temp .pix files as the
#   accumulation buffer rather than a running fbserv process.
#
#   Activated by callers that include an _outfile key in rtimage_dict; the
#   rt and rtedge binaries write to temporary .pix files, the ghost and edge
#   compositing steps use the same file-based utilities (pixmatte, pix-bw,
#   bwmod, bw-pix) already used by the original ghost-composite path, and
#   the final result is written directly to _outfile without ever touching a
#   framebuffer server.
#
#   This path is the replacement for the fbserv-subprocess model in Obol
#   builds and in any environment where the libdm fb_* API is not available.
proc rtimage_file {rtimage_dict} {
    global tcl_platform
    global env
    set necessary_vars [list _outfile _dbfile _w _n _viewsize _orientation \
	_eye_pt _perspective _bgcolor _ecolor _necolor _occmode _gamma \
	_benchmark_mode _log_file]
    set necessary_lists [list _color_objects _ghost_objects _edge_objects]

    foreach param [dict keys $rtimage_dict] {
	set $param [dict get $rtimage_dict $param]
    }
    foreach var ${necessary_vars} {
	if {![info exists $var]} { set $var "" }
    }
    foreach var ${necessary_lists} {
	if {![info exists $var]} { set $var {} }
    }

    if {$::tcl_platform(platform) == "windows"} {
	if {[catch {set dir $env(TMP)}]} {
	    return "rtimage_file: env(TMP) does not exist"
	}
    } else {
	set dir "/tmp"
	if {![file exists $dir]} {
	    return "rtimage_file: $dir does not exist"
	}
    }

    set pid [pid]
    set binpath [bu_dir bin]
    set ar [expr {$_w.0 / $_n.0}]

    # Provide a safe default for ghost-intensity gamma
    if {$_gamma eq ""} { set _gamma 6 }

    # Primary accumulation buffer (replaces the fbserv framebuffer)
    set tmpfb   [file join $dir ${pid}_rtfb.pix]
    # Temp files used by the ghost-composite sub-pipeline
    set tgi     [file join $dir ${pid}_ghost.pix]
    set tfci    [file join $dir ${pid}_fc.pix]
    set tgfci   [file join $dir ${pid}_ghostfc.pix]
    set tmi     [file join $dir ${pid}_merge.pix]
    set tmi2    [file join $dir ${pid}_merge2.pix]
    set tbw     [file join $dir ${pid}_bw.bw]
    set tmod    [file join $dir ${pid}_bwmod.bw]
    set tbwpix  [file join $dir ${pid}_bwpix.pix]
    # Temp file for the edge-render pass
    set tmpe    [file join $dir ${pid}_edges.pix]
    set tmpc    [file join $dir ${pid}_comp.pix]

    # Common view args shared by all rt/rtedge invocations
    set view_args [list \
	-w $_w -n $_n \
	-V $ar \
	-R \
	-A 0.9 \
	-p $_perspective \
	-C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
	"-c {viewsize $_viewsize}" \
	"-c {orientation $_orientation}" \
	"-c {eye_pt $_eye_pt}" \
	[list $_dbfile]]

    # --- Step 1: Color objects (rt → tmpfb) ---
    if {[llength $_color_objects]} {
	set cmd [concat [list [file join $binpath rt]] \
	    -w $_w -n $_n $_benchmark_mode \
	    -o [list $tmpfb] \
	    -V $ar -R -A 0.9 \
	    -p $_perspective \
	    -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
	    "-c {viewsize $_viewsize}" \
	    "-c {orientation $_orientation}" \
	    "-c {eye_pt $_eye_pt}" \
	    [list $_dbfile]]
	foreach obj $_color_objects { lappend cmd $obj }
	catch {eval exec $cmd >& $_log_file}
    } else {
	# No color objects: write a solid background .pix file
	set bgR [lindex $_bgcolor 0]
	set bgG [lindex $_bgcolor 1]
	set bgB [lindex $_bgcolor 2]
	if {$bgR eq "" || $bgG eq "" || $bgB eq ""} {
	    set bgR 255; set bgG 255; set bgB 255
	}
	set fd [open $tmpfb wb]
	for {set i 0} {$i < $_w * $_n} {incr i} {
	    puts -nonewline $fd [binary format ccc $bgR $bgG $bgB]
	}
	close $fd
    }

    # --- Step 2: Ghost objects (file-based composite; same logic as rtimage) ---
    if {[llength $_ghost_objects]} {
	set occlude_objects [lsort -unique [concat $_color_objects $_ghost_objects]]

	# tfci = snapshot of the current accumulated image (color render or blank bg)
	file copy -force $tmpfb $tfci

	# Ghost-only render → tgi
	set cmd [concat [list [file join $binpath rt]] \
	    -w $_w -n $_n $_benchmark_mode \
	    -o [list $tgi] \
	    -V $ar -R -A 0.9 \
	    -p $_perspective \
	    -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
	    "-c {viewsize $_viewsize}" \
	    "-c {orientation $_orientation}" \
	    "-c {eye_pt $_eye_pt}" \
	    [list $_dbfile]]
	foreach obj $_ghost_objects { lappend cmd $obj }
	catch {eval exec $cmd >& $_log_file}

	# Full occluded render (color + ghost together) → tgfci
	set cmd [concat [list [file join $binpath rt]] \
	    -w $_w -n $_n $_benchmark_mode \
	    -o [list $tgfci] \
	    -V $ar -R -A 0.9 \
	    -p $_perspective \
	    -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
	    "-c {viewsize $_viewsize}" \
	    "-c {orientation $_orientation}" \
	    "-c {eye_pt $_eye_pt}" \
	    [list $_dbfile]]
	foreach obj $occlude_objects { lappend cmd $obj }
	catch {eval exec $cmd >& $_log_file}

	# Convert ghost render to greyscale with gamma-adjusted intensity
	catch {exec [file join $binpath pix-bw] -e crt $tgi > $tbw}
	catch {exec [file join $binpath bwmod] -a 4 -d259 -r$_gamma -m255 $tbw > $tmod}
	catch {exec [file join $binpath bw-pix] $tmod > $tbwpix}

	# Composite: identical to the original rtimage ghost path
	set bgl "=[lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2]"
	catch {exec [file join $binpath pixmatte] -e $tfci $bgl $tbwpix $tfci > $tmi}
	catch {exec [file join $binpath pixmatte] -e $tgfci $bgl $tfci $tmi > $tmi2}

	# tmi2 is the new accumulation buffer
	file rename -force $tmi2 $tmpfb

	foreach f [list $tgi $tfci $tgfci $tbw $tmod $tbwpix $tmi] {
	    catch {file delete -force $f}
	}
    }

    # --- Step 3: Edge objects (rtedge → tmpe, then composite over tmpfb) ---
    if {[llength $_edge_objects]} {
	# Determine the foreground (edge) color
	if {[llength $_ecolor] == 3} {
	    set r [lindex $_ecolor 0]
	    set g [lindex $_ecolor 1]
	    set b [lindex $_ecolor 2]
	    if {[string is digit $r] && $r <= 255 &&
		[string is digit $g] && $g <= 255 &&
		[string is digit $b] && $b <= 255} {
		set fgMode [list set fg=[lindex $_ecolor 0],[lindex $_ecolor 1],[lindex $_ecolor 2]]
	    } else {
		set fgMode [list set rc=1]
	    }
	} else {
	    set fgMode [list set rc=1]
	}

	# Determine the non-edge background color for the edge render.
	# Non-edge pixels are filled with this color; we use it as the
	# "transparent" mask when compositing edges over the accumulated image.
	# Use necolor when available, otherwise fall back to bgcolor.
	if {[llength $_necolor] == 3} {
	    set ne_r [lindex $_necolor 0]
	    set ne_g [lindex $_necolor 1]
	    set ne_b [lindex $_necolor 2]
	} else {
	    set ne_r [lindex $_bgcolor 0]
	    set ne_g [lindex $_bgcolor 1]
	    set ne_b [lindex $_bgcolor 2]
	}
	set bgMode [list set bg=$ne_r,$ne_g,$ne_b]
	# Constant colour token used by pixmatte for the non-edge pixels
	set nonEdgeCl "=$ne_r/$ne_g/$ne_b"

	# Occlusion options (mirror original rtimage behaviour)
	set occlude_objects [lsort -unique [concat $_color_objects $_ghost_objects]]
	if {[llength $occlude_objects]} {
	    set coMode [list "-c" "set om=$_occmode" "-c" "set oo=\\\"$occlude_objects\\\""]
	} else {
	    set coMode {}
	}

	set cmd [concat [list [file join $binpath rtedge]] \
	    -w $_w -n $_n $_benchmark_mode \
	    -o [list $tmpe] \
	    -V $ar -R -A 0.9 \
	    -p $_perspective \
	    "-c {$fgMode}" "-c {$bgMode}" \
	    $coMode \
	    "-c {viewsize $_viewsize}" \
	    "-c {orientation $_orientation}" \
	    "-c {eye_pt $_eye_pt}" \
	    [list $_dbfile]]
	foreach obj $_edge_objects { lappend cmd $obj }
	catch {eval exec $cmd >& $_log_file}

	# Composite: where the edge pixel == nonEdgeCl (no edge there) → keep
	# the accumulated color/ghost; otherwise take the edge pixel.
	# pixmatte -e tmpe nonEdgeCl tmpfb tmpe → output
	#   true  (tmpe == nonEdgeCl): use tmpfb  (show color beneath)
	#   false (tmpe != nonEdgeCl): use tmpe   (show edge line)
	catch {exec [file join $binpath pixmatte] \
	    -e $tmpe $nonEdgeCl $tmpfb $tmpe > $tmpc}
	file rename -force $tmpc $tmpfb

	catch {file delete -force $tmpe}
    }

    # --- Step 4: Write final output ---
    if {[string length $_outfile]} {
	if {[file extension $_outfile] eq ".png"} {
	    catch {exec [file join $binpath pix-png] -w $_w -n $_n $tmpfb $_outfile}
	} else {
	    file copy -force $tmpfb $_outfile
	}
    }

    catch {file delete -force $tmpfb}
}

proc rtimage {rtimage_dict} {
    global tcl_platform
    global env

    # When _outfile is provided, use the file-based path (no fbserv required).
    # This is the standard path for Obol builds and any environment where the
    # libdm framebuffer API may not be available.
    if {[dict exists $rtimage_dict _outfile] && \
	    [dict get $rtimage_dict _outfile] ne ""} {
	rtimage_file $rtimage_dict
	return
    }

    set necessary_vars [list _dbfile _port _w _n _viewsize _orientation \
    _eye_pt _perspective _bgcolor _ecolor _necolor _occmode _gamma _benchmark_mode]
    set necessary_lists [list _color_objects _ghost_objects _edge_objects]

    # It's the responsibility of the calling function
    # to populate the dictionary with what is needed.
    # Make the variables for local processing.
    foreach param [dict keys $rtimage_dict] {
        set $param [dict get $rtimage_dict $param]
    }

    # Anything we don't already have from the dictionary
    # is assumed empty
    foreach var ${necessary_vars} {
      if {![info exists $var]} { set $var "" }
    }
    foreach var ${necessary_lists} {
      if {![info exists $var]} { set $var {} }
    }

    set ar [ expr $_w.0 / $_n.0 ]

    if {$::tcl_platform(platform) == "windows"} {
	if {[catch {set dir $env(TMP)}]} {
	    return "make_image: env(TMP) does not exist"
	}
    } else {
	set dir "/tmp"

	if {![file exists $dir]} {
	    return "make_image: $dir does not exist"
	}
    }

    set pid [pid]
    set tgi [list [file join $dir $pid\_ghost.pix] ]
    set tfci [list [file join $dir $pid\_fc.pix] ]
    set tgfci [list [file join $dir $pid\_ghostfc.pix] ]
    set tmi [list [file join $dir $pid\_merge.pix] ]
    set tmi2 [list [file join $dir $pid\_merge2.pix] ]
    set tbw [list [file join $dir $pid\_bw.bw] ]
    set tmod [list [file join $dir $pid\_bwmod.bw] ]
    set tbwpix [list [file join $dir $pid\_bwpix.pix] ]

    set binpath [bu_dir bin]

    if {[llength $_color_objects]} {
	set have_color_objects 1

	set cmd [concat [list [file join $binpath rt]] -w $_w -n $_n $_benchmark_mode \
	    -F $_port \
	    -V $ar \
	    -R \
	    -A 0.9 \
	    -p $_perspective \
	    -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
	    "-c {viewsize $_viewsize}" \
	    "-c {orientation $_orientation}" \
	    "-c {eye_pt $_eye_pt}" \
	    [list $_dbfile]]

	foreach obj $_color_objects {
	    lappend cmd $obj
	}

	#puts "RT (with fullcolor): $cmd"

	#
	# Run rt to generate the color insert
	#
	catch {eval exec $cmd >& $_log_file} curr_pid

	# Look for color objects that also get edges
	if {[llength $_edge_objects] && [llength $_ecolor] == 3} {

	    set r [lindex $_ecolor 0]
	    set g [lindex $_ecolor 1]
	    set b [lindex $_ecolor 2]

	    if {[string is digit $r] && $r <= 255 ||
		[string is digit $g] && $g <= 255 ||
		[string is digit $b] && $b <= 255} {

		set fgMode [list set fg=[lindex $_ecolor 0],[lindex $_ecolor 1],[lindex $_ecolor 2]]

		set ce_objects {}
		set ne_objects {}
		foreach cobj $_color_objects {
		    set i [lsearch $_edge_objects $cobj]
		    if {$i != -1} {
			lappend ce_objects $cobj
		    } else {
			lappend ne_objects $cobj
		    }
		}

		if {[llength $ce_objects]} {
		    set bgMode [list set bg=[lindex $_bgcolor 0],[lindex $_bgcolor 1],[lindex $_bgcolor 2]]

		    set cmd [concat [list [file join $binpath rtedge]] -w $_w -n $_n $_benchmark_mode \
		                 -F $_port \
				 -V $ar \
				 -R \
				 -A 0.9 \
				 -p $_perspective \
				 "-c {$fgMode}" \
				 "-c {$bgMode}" \
				 "-c {set ov=1}" \
				 "-c {viewsize $_viewsize}" \
				 "-c {orientation $_orientation}" \
				 "-c {eye_pt $_eye_pt}" \
				 [list $_dbfile]]

		    foreach obj $ce_objects {
			lappend cmd $obj
		    }
		}

		# !!! FIXME: this runs rt in regress-D ...
		#puts "RTEDGE (with fullcolor): $cmd"
		
		#
		# Run rtedge to generate the full-color with edges
		#
		catch {eval exec $cmd >& $_log_file} curr_pid
	    }
	}

    } else {
	set have_color_objects 0

	# Put a blank image into the framebuffer
	catch {exec [list [file join $binpath fbclear]] -F $_port [lindex $_bgcolor 0] [lindex $_bgcolor 1] [lindex $_bgcolor 2]}
    }

    set occlude_objects [lsort -unique [concat $_color_objects $_ghost_objects]]

    if {[llength $_ghost_objects]} {

	# Pull the image from the framebuffer
	catch {exec [file join $binpath fb-pix] -w $_w -n $_n -F $_port $tfci}

	set have_ghost_objects 1
	set cmd [concat [list [file join $binpath rt]] -w $_w -n $_n $_benchmark_mode \
	             -o $tgi \
		     -V $ar \
		     -R \
		     -A 0.9 \
		     -p $_perspective \
		     -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
		     "-c {viewsize $_viewsize}" \
		     "-c {orientation $_orientation}" \
		     "-c {eye_pt $_eye_pt}" \
		     [list $_dbfile]]

	foreach obj $_ghost_objects {
	    lappend cmd $obj
	}

	#puts "RT (ghosted): $cmd"

	#
	# Run rt to generate the full-color version of the ghost image
	#
	catch {eval exec $cmd >& $_log_file} curr_pid

	set cmd [concat [list [file join $binpath rt]] -w $_w -n $_n $_benchmark_mode \
	             -o $tgfci \
		     -V $ar \
		     -R \
		     -A 0.9 \
		     -p $_perspective \
		     -C [lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2] \
		     "-c {viewsize $_viewsize}" \
		     "-c {orientation $_orientation}" \
		     "-c {eye_pt $_eye_pt}" \
		     [list $_dbfile]]

	foreach obj $occlude_objects {
	    lappend cmd $obj
	}

	#puts "RT (occluded): $cmd"
	
	#
	# Run rt to generate the full-color version of the occlude_objects (i.e. color and ghost)
	#
	catch {eval exec $cmd >& $_log_file} curr_pid

	#
	# Convert to ghost image
	#
	catch {exec [file join $binpath pix-bw] -e crt $tgi > $tbw}
	catch {exec [file join $binpath bwmod] -a 4 -d259 -r$_gamma -m255 $tbw > $tmod}
	catch {exec [file join $binpath bw-pix] $tmod > $tbwpix}

	set bgl "=[lindex $_bgcolor 0]/[lindex $_bgcolor 1]/[lindex $_bgcolor 2]"
	catch {exec [file join $binpath pixmatte] -e $tfci $bgl $tbwpix $tfci > $tmi}
	catch {exec [file join $binpath pixmatte] -e $tgfci $bgl $tfci $tmi > $tmi2}

	# Put the image into the framebuffer
	catch {exec [file join $binpath pix-fb] -w $_w -n $_n -F $_port $tmi2}
    } else {
	set have_ghost_objects 0
    }

    if {[llength $_edge_objects]} {
	set have_edge_objects 1

	if {[llength $_ecolor] != 3} {
	    set fgMode [list set rc=1]
	} else {
	    set r [lindex $_ecolor 0]
	    set g [lindex $_ecolor 1]
	    set b [lindex $_ecolor 2]
	    if {![string is digit $r] || $r > 255 ||
		![string is digit $g] || $g > 255 ||
		![string is digit $b] || $b > 255} {
		set fgMode [list set rc=1]
	    } else {
		set fgMode [list set fg=[lindex $_ecolor 0],[lindex $_ecolor 1],[lindex $_ecolor 2]]
	    }
	}

	if {[llength $occlude_objects]} {
	    set coMode "-c {set om=$_occmode} -c {set oo=\\\"$occlude_objects\\\"}"
	    set bgMode [list set bg=[lindex $_necolor 0],[lindex $_necolor 1],[lindex $_necolor 2]]
	} else {
	    set coMode ""
	    set bgMode [list set bg=[lindex $_bgcolor 0],[lindex $_bgcolor 1],[lindex $_bgcolor 2]]
	}

	set cmd [concat [list [file join $binpath rtedge]] -w $_w -n $_n $_benchmark_mode \
	             -F $_port \
		     -V $ar \
		     -R \
		     -A 0.9 \
		     -p $_perspective \
		     "-c {$fgMode}" \
		     "-c {$bgMode}" \
		     $coMode \
	             "-c {viewsize $_viewsize}" \
		     "-c {orientation $_orientation}" \
		     "-c {eye_pt $_eye_pt}" \
		     [list $_dbfile]]
	foreach obj $_edge_objects {
	    lappend cmd $obj
	}

	#puts "RTEDGE: $cmd"
	
	#
	# Run rtedge to generate the full-color version of the ghost image
	# !!! manually write an rtedge log
	catch {eval exec $cmd >& rtedge.log} curr_pid
    }

    catch {file delete -force $tgi}
    catch {file delete -force $tfci}
    catch {file delete -force $tgfci}
    catch {file delete -force $tmi}
    catch {file delete -force $tmi2}
    catch {file delete -force $tbw}
    catch {file delete -force $tmod}
    catch {file delete -force $tbwpix}

#end proc rtimage
}

#end namespace cadwidgets
}


# Local Variables:
# mode: Tcl
# tab-width: 8
# c-basic-offset: 4
# tcl-indent-level: 4
# indent-tabs-mode: t
# End:
# ex: shiftwidth=4 tabstop=8
