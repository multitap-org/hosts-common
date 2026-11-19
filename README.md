# hosts-common

Code shared by multitap's core hosts (host-libretro, later others): dynamic
library loading, logging, file helpers. No SDL, no GUI. Also the project's
CMake helpers (`cmake/multitap.cmake`).

A library: it doesn't build on its own. A frontend (piccolo,
libretro-test-frontend, ...) adds it as a submodule and `add_subdirectory`s it
before the other libraries.

MIT License (see LICENSE).
