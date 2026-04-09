
foreach arg $argv {
puts "Generating tclIndex in $arg"
catch {auto_mkindex $arg *.tcl *.itcl *.itk *.sh} errout
if {![file exists "$arg/tclIndex"]} {
puts "$errout"
return -code 1
}
}