# Contributing

## The SDK headers are a promise

`sdk/include/shard/*.h` is the C ABI : plugins built by other people, with other compilers, against an older revision, call these structs and tables. A change there is reviewed for compatibility before anything else :

* only additions inside a major revision : a function at the end of a table, a field at the end of a struct, a new interface. Never reorder, resize or remove ;
* every addition raises `SHARD_ABI_MINOR`, the interface's minor in `query_interface` and `sdk/VERSION`, and goes into the table of revisions of `sdk/README.md` ;
* `tests/plugins/abi_layout_tests.cpp` is updated in the same change, on purpose : its failure is the signal that a layout moved ;
* `tests/plugin_fixtures/abi_1_0` is never edited : the plugin built against it has to keep loading.

What is not ready to be frozen goes into a `shard.experimental.*` interface (`shard_experimental.h`) : major 0, no promise, refused to signed native plugins, not pinned, not counted in `SHARD_ABI_MINOR`. Moving it to a frozen interface later is an ordinary addition.

The comments of these headers are the [API reference](api_reference.md) : `scripts/gen_api_reference.py` (or the `ShardApiReference` target) writes the page from them, and the test `Docs.ApiReference` fails when the committed page is not what the headers give, or when a header announces another revision than the one the engine offers. Write the comments for the reader of that page : the first comment of an interface's header starts with `Interface "shard.x" M.m : what it is`, a comment just before a struct describes it, `---- Title ----` makes a heading, and backticks are kept as they are.

The C++ SDK (`sdk/include/shard/cpp`) is compiled into each plugin : it can change freely, as long as it only uses the C ABI.
