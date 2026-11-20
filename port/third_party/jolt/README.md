# Jolt Physics

A rigid body physics engine, in C++17, by Jorrit Rouwe and contributors, MIT
licensed (see `LICENSE`).

Upstream: https://github.com/jrouwe/JoltPhysics, release `v5.5.0`,
`v5.5.0.tar.gz`, SHA-256
`3dae862a32c9092fca5b17f8e5d32cd57e035d30c3145c00040f13ca58a866df`.

`Jolt/` is the release's `Jolt` folder, unchanged, and `LICENSE` its
`LICENSE`, unchanged. To update, replace both from a newer release.

The desktop builds use it for the ragdolls (`display.ragdolls`:
`port/linux/game/ragdolls.c`, through the C face in
`port/linux/game/physics_jolt.cpp`). They build the units `Jolt/Jolt.cmake`
lists, with its defaults (no debug renderer, profiler, object streams or
asserts), less `Core/JobSystemThreadPool.cpp` and
`Physics/StateRecorderImpl.cpp`, which nothing uses: the C face steps the world
on the game's thread. The consoles and the dedicated server leave it out.
