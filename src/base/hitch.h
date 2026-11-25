#pragma once

// Frame hitch probe: GT3R_HITCH=<ms> logs every frame longer than that, with where the time
// went (App::iterate phases; the race screen fills in its tick, reflection-cube and scene-draw
// times and the car's position). Written and read on the main thread only.
namespace gt3r::hitch {
inline double ticks_ms = 0, env_ms = 0, draw_ms = 0, collect_ms = 0;
inline int ticks = 0, smoke = 0, meshes = 0;
inline int draws = 0, mirror_draws = 0; // the main view's draw calls, the mirror's (when drawn)
inline float x = 0, z = 0, race_t = 0;
} // namespace gt3r::hitch
