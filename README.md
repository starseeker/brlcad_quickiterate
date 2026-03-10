brlcad/src/remrt is one of our last remaining major non-WIN32 portability
pain points.  Please study it, and see if it is possible to replicate
its feature set portably using C++17.  If not possible using C++17 alone,
suggest what would be the best options beyond C++17 itself to get a portable
version working.

While you're reworking it, see if you can add authentication mechanisms to
ensure that the client/child communication mechanisms between server and
remote processes are all from the same session - i.e., prevent accidental
garbage communications from other programs feeding into the same tcp ports.
You can use openssl if that's helpful, but please make it an optional
dependency and fall back on something simpler if it's not available.
