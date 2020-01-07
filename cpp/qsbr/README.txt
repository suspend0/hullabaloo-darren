Quiescent-State Based Reclamation
============

This class (partially) implements a QSBR system.  It still
has some bugs but it's getting there.  If you run this,
**ASAN will crash** the process.

The implementation allows a single writer and multiple readers.

The writer thread maintains a global epoch pointer which is
incremented as changes are made, and reader threads store
the epoch at which they were *outside* a critical section.
The writer puts items to delete on a garbage list and then
deletes them once all readers are newer than the point at
which the garbage was created.

If this seems a bit fuzzy, google QSBR & its sibling
EBR (Epoch Based Reclamation)


