The open2open directory contains an attempt at conversion logic
between the opennurbs 3dm format and FreeCAD/opencascade.  We
want to use this as the foundation for a libgcv plugin that allows
BRL-CAD to read and write FreeCAD's format.  BRL-CAD uses opennurbs
for our brep primitive, so that aspect of open2open should be
directly usable, but the other aspects of conversion will need more
work.

My initial thought is to utilize and enhance BRL-CAD's existing 3dm
import support - make it modular so it can be used by more than
one plugin, and translate the aspects of FCStd data that can translate
through open2open to 3dm from open2open's 3dm data into .g data the
same way 3dm-g would.  This will also help us enhance and improve our
3dm support.

A later phase will be a more direct translation of the OCCT data that
is not heading for ON_Brep into BRL-CAD data structures, to avoid any
losses induced by staging through 3dm - if that proves to be an easier
way to handle this from the start you may do so.

You will need to install the development headers for opencascade - it
is not part of the BRL-CAD dependency repository - and add the necessary
find_package logic in BRL-CAD's libgcv plugin to locate and use it.
open2open you can integrate as you like - for this initial phase we
are not finalizing where and how it will be maintained, so just integrate
as convenient to do the real work - the FCStd->g importer (we'll eventually
want export as well, but there are other issues involved there so for
the moment please focus on import.)

open2open containes some example FCStd input files - use those for
input testing.  open2open also has an API to read
them, which is another reason staging through 3dm might be a quicker initial
path to getting something working, but you are free to use or avoid that
path depending on what makes sense for conversion fidelity and ease of
integration.


