# No Makefile change is required in the current repository.
#
# The repository already contains the generic rule:
#
#   %.x: Machines/%.o $(MINI_OT) $(SHAREDLIB)
#       $(CXX) -o $@ $(CFLAGS) $^ $(LDLIBS) $(SHAREDLIB)
#
# Therefore, after placing:
#
#   Machines/kona-reduced-depth-cong-bench.cpp
#
# this is sufficient:
#
#   make -j8 kona-reduced-depth-cong-bench.x
#
# Keep this file only as a note; do not paste an extra target into Makefile.
