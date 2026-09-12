# Platform adapter boundary

These files test host/platform mechanics separately from DragonRuby's proprietary runtime.

- `sdl_worker.*` maps the portable wait/wake worker ABI to a real SDL mutex/condition/thread and tests that stop wakes a blocked worker instead of waiting for the timeout.
- `dynlib.*` exercises native dynamic-library open/symbol/close on Windows, macOS and Linux through the portable conformance executable.
- `apple_main.*` verifies main-thread detection and links the libdispatch main-queue adapter on macOS. Executing an asynchronous callback still requires a running app event loop.
- `jni_adapter.*` is tested against a real host JVM, including attach/detach from a foreign pthread, and cross-compiled with the hosted Android NDK. App-specific Java method/class registration remains outside this generic layer.
- `steamworks_adapter.*` keeps the Steam lifecycle wrapper tiny. Public CI uses the explicit `stubs/` header only to test wrapper control flow; a real Steamworks SDK and Steam client are required for runtime validation.

None of these tests claims DragonRuby renderer, Steam client, Android activity or Apple application lifecycle validation. Those are final host-conformance gates.
