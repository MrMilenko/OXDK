// lld-link segfaults linking very small binaries against XDK libs.
// Adding a second obj file with some data avoids the crash.

int _lld_workaround[1024] = {1};
