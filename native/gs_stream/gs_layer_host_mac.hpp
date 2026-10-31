// SPDX-License-Identifier: MIT
// macOS: hosts the GS process picture layer in the game window (gs_layer_host_mac.mm). Main thread only.
#pragma once

#include <cstdint>

struct SDL_Window;

namespace gt4::gs_stream {
// Shows the remote Core Animation context `context_id` (gs_stream::presentation_id()) over the window's content.
// Hosting the same id again does nothing; a new id (a new GS process) replaces the old one.
bool host_presentation(SDL_Window *window, std::uint64_t context_id);
void remove_presentation();
} // namespace gt4::gs_stream
