
if {![package vsatisfies [package provide Tcl] 8.5]} {return}
package ifneeded Itk 3.4 [list load [file join $dir ../../lib/itk3.4 libitk3.4.so] Itk]
