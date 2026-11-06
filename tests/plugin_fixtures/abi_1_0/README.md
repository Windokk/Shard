# ABI 1.0, frozen

`include/shard/` is a copy of `sdk/include/shard/*.h` as they were at revision 1.0 of the C ABI. Never edit it : `abi_1_0_plugin.c` is
built against it, and `tests/plugins/abi_compat_tests.cpp` checks that this plugin still loads and works in the current engine (whatever
revision 1.x its headers are at). When the ABI moves to a new major, this folder goes with the old major.
