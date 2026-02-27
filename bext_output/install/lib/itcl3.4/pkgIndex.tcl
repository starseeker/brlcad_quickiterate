
if {![package vsatisfies [package provide Tcl] 8.5]} {return}
package ifneeded Itcl 3.4 [list load [file join $dir ../../lib/itcl3.4 libitcl3.4.so] Itcl]
