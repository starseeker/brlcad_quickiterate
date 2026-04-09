
foreach arg $argv {
puts "Generating pkgIndex.tcl in $arg"
catch {pkg_mkIndex -verbose $arg *.tcl *.itcl *.itk *.sh} errout
if {![file exists "$arg/pkgIndex.tcl"]} {
puts "$errout"
return -code 1
}
}