The file brlcad/include/rt/db_instance.h defines a struct db_i which holds
many elements that are marked PRIVATE but are part of the public header.

We have a struct db_i_internal container now (struct db_i_internal *i in struct db_i) where we could relocate those
private elements.  It's definition is in src/librt/librt_private.h

The process of relocating the PRIVATE db_i entries to struct db_i_internal is a
mechanical one, but will involve updating a lot of code to correctly reference the
new locations.  Please execute this move and code update, doing a compile check to
verify correct updating
