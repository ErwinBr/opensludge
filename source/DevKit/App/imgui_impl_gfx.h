#ifndef SLUDGE_IMGUI_IMPL_GFX_H
#define SLUDGE_IMGUI_IMPL_GFX_H

/*
 * A renderer backend of Dear ImGui that draws with the renderers of the
 * engine (gfx::Renderer), so that the Dev Kit draws its interface as the
 * engine draws a game: with SDL_GPU or OpenGL, through the same code.
 *
 * An ImTextureID is a gfx::Texture *. The textures that we draw into, the
 * views of the editors, have their rows the other way up, so show them
 * with imageOfTarget.
 */

#include "imgui.h"

namespace gfx {
class Renderer;
struct Texture;
}

bool ImGui_ImplGfx_Init (gfx::Renderer * renderer);
void ImGui_ImplGfx_Shutdown ();
// Draw into the current target of the renderer, which is width by height
// pixels.
void ImGui_ImplGfx_RenderDrawData (ImDrawData * drawData, int width, int height);

ImTextureID ImGui_ImplGfx_TextureID (gfx::Texture * texture);

// Show a texture that a gfx::RenderTarget drew into, size points large.
void imageOfTarget (gfx::Texture * texture, const ImVec2 & size);

#endif
