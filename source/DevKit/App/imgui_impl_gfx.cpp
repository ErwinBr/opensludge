/*
 * The renderer backend of Dear ImGui over gfx::Renderer (imgui_impl_gfx.h).
 *
 * We copy the vertices and indices of all the draw lists of a frame into
 * one mesh, and draw each command of each list from it. ImDrawVert has the
 * layout of gfx::Vertex2D, so we copy them as they are.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "renderer.h"
#include "imgui_impl_gfx.h"

static_assert (sizeof (ImDrawVert) == sizeof (gfx::Vertex2D), "ImDrawVert must be laid out as gfx::Vertex2D");
static_assert (offsetof (ImDrawVert, pos) == offsetof (gfx::Vertex2D, x), "ImDrawVert must be laid out as gfx::Vertex2D");
static_assert (offsetof (ImDrawVert, uv) == offsetof (gfx::Vertex2D, u), "ImDrawVert must be laid out as gfx::Vertex2D");
static_assert (offsetof (ImDrawVert, col) == offsetof (gfx::Vertex2D, color), "ImDrawVert must be laid out as gfx::Vertex2D");
static_assert (sizeof (ImDrawIdx) == sizeof (unsigned short), "ImGui must draw with 16-bit indices");

namespace {

gfx::Renderer * renderer = NULL;
std::vector<gfx::Vertex2D> vertices;
std::vector<unsigned short> indices;
std::vector<unsigned char> updatePixels;

// We reset nothing: each draw sets all that it needs.
void resetRenderState (const ImDrawList *, const ImDrawCmd *) {}

gfx::Texture * textureOf (ImTextureID id) {
	return (gfx::Texture *) (intptr_t) id;
}

void updateTexture (ImTextureData * tex) {
	if (tex->Status == ImTextureStatus_WantCreate) {
		// We keep the textures of ImGui in RGBA. Alpha8 would need a
		// program that reads it as white.
		IM_ASSERT (tex->Format == ImTextureFormat_RGBA32);
		gfx::Texture * t = renderer->createTexture (tex->Width, tex->Height, gfx::TEX_RGBA8, tex->GetPixels ());
		renderer->setTextureFilter (t, gfx::FILTER_LINEAR, gfx::FILTER_LINEAR);
		renderer->setTextureWrap (t, gfx::WRAP_CLAMP, gfx::WRAP_CLAMP);
		tex->SetTexID (ImGui_ImplGfx_TextureID (t));
		tex->SetStatus (ImTextureStatus_OK);
	} else if (tex->Status == ImTextureStatus_WantUpdates) {
		gfx::Texture * t = textureOf (tex->TexID);
		for (int i = 0; i < tex->Updates.Size; i ++) {
			const ImTextureRect & r = tex->Updates[i];
			// The renderers take the pixels of a rectangle tightly packed.
			size_t rowBytes = (size_t) r.w * tex->BytesPerPixel;
			updatePixels.resize (rowBytes * r.h);
			for (int y = 0; y < r.h; y ++) {
				memcpy (&updatePixels[y * rowBytes], tex->GetPixelsAt (r.x, r.y + y), rowBytes);
			}
			renderer->updateTexture (t, r.x, r.y, r.w, r.h, updatePixels.data ());
		}
		tex->SetStatus (ImTextureStatus_OK);
	} else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
		renderer->destroyTexture (textureOf (tex->TexID));
		tex->SetTexID (ImTextureID_Invalid);
		tex->SetStatus (ImTextureStatus_Destroyed);
	}
}

} // namespace

bool ImGui_ImplGfx_Init (gfx::Renderer * r) {
	ImGuiIO & io = ImGui::GetIO ();
	IM_ASSERT (io.BackendRendererUserData == NULL);
	renderer = r;
	io.BackendRendererName = "imgui_impl_gfx";
	io.BackendRendererUserData = r;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
	ImGui::GetPlatformIO ().DrawCallback_ResetRenderState = resetRenderState;
	return true;
}

void ImGui_ImplGfx_Shutdown () {
	ImGuiPlatformIO & platform = ImGui::GetPlatformIO ();
	for (int i = 0; i < platform.Textures.Size; i ++) {
		ImTextureData * tex = platform.Textures[i];
		if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid) {
			renderer->destroyTexture (textureOf (tex->TexID));
			tex->SetTexID (ImTextureID_Invalid);
			tex->SetStatus (ImTextureStatus_Destroyed);
		}
	}
	platform.DrawCallback_ResetRenderState = NULL;
	ImGuiIO & io = ImGui::GetIO ();
	io.BackendRendererName = NULL;
	io.BackendRendererUserData = NULL;
	io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
	renderer = NULL;
}

ImTextureID ImGui_ImplGfx_TextureID (gfx::Texture * texture) {
	return (ImTextureID) (intptr_t) texture;
}

void imageOfTarget (gfx::Texture * texture, const ImVec2 & size) {
	// Row 0 of a target is its bottom row.
	ImGui::Image (ImTextureRef (ImGui_ImplGfx_TextureID (texture)), size, ImVec2 (0, 1), ImVec2 (1, 0));
}

void ImGui_ImplGfx_RenderDrawData (ImDrawData * drawData, int width, int height) {
	if (drawData->Textures) {
		for (int i = 0; i < drawData->Textures->Size; i ++) {
			ImTextureData * tex = (*drawData->Textures)[i];
			if (tex->Status != ImTextureStatus_OK) updateTexture (tex);
		}
	}
	if (width <= 0 || height <= 0 || drawData->TotalVtxCount == 0) return;

	vertices.resize (drawData->TotalVtxCount);
	indices.resize (drawData->TotalIdxCount);
	size_t vtx = 0, idx = 0;
	for (int n = 0; n < drawData->CmdLists.Size; n ++) {
		const ImDrawList * list = drawData->CmdLists[n];
		memcpy (&vertices[vtx], list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof (ImDrawVert));
		memcpy (&indices[idx], list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof (ImDrawIdx));
		vtx += list->VtxBuffer.Size;
		idx += list->IdxBuffer.Size;
	}
	gfx::Mesh * mesh = renderer->createFrameMesh (vertices.data (), (int) vertices.size (), indices.data (), (int) indices.size ());

	// From the points of ImGui, with y going down, to the target.
	ImVec2 pos = drawData->DisplayPos, size = drawData->DisplaySize;
	gfx::DrawUniforms uniforms;
	memset (uniforms.matrix, 0, sizeof (uniforms.matrix));
	uniforms.matrix[0] = 2.0f / size.x;
	uniforms.matrix[5] = -2.0f / size.y;
	uniforms.matrix[10] = 1.0f;
	uniforms.matrix[12] = -1.0f - 2.0f * pos.x / size.x;
	uniforms.matrix[13] = 1.0f + 2.0f * pos.y / size.y;
	uniforms.matrix[15] = 1.0f;
	gfx::PipelineState state (gfx::PROG_VERTEX_COLOR, gfx::BLEND_ALPHA);
	float scaleX = width / size.x, scaleY = height / size.y;

	renderer->setViewport (0, 0, width, height);
	vtx = idx = 0;
	for (int n = 0; n < drawData->CmdLists.Size; n ++) {
		const ImDrawList * list = drawData->CmdLists[n];
		for (int c = 0; c < list->CmdBuffer.Size; c ++) {
			const ImDrawCmd * cmd = &list->CmdBuffer[c];
			if (cmd->UserCallback) {
				if (cmd->UserCallback != resetRenderState) cmd->UserCallback (list, cmd);
				renderer->setViewport (0, 0, width, height);
				continue;
			}
			// The clip rectangle in pixels, from the bottom of the target.
			float x1 = (cmd->ClipRect.x - pos.x) * scaleX, y1 = (cmd->ClipRect.y - pos.y) * scaleY;
			float x2 = (cmd->ClipRect.z - pos.x) * scaleX, y2 = (cmd->ClipRect.w - pos.y) * scaleY;
			if (x1 < 0) x1 = 0;
			if (y1 < 0) y1 = 0;
			if (x2 > width) x2 = (float) width;
			if (y2 > height) y2 = (float) height;
			if (x2 <= x1 || y2 <= y1) continue;
			renderer->setScissor ((int) x1, height - (int) y2, (int) (x2 - x1), (int) (y2 - y1));
			renderer->drawMesh (state, uniforms, textureOf (cmd->GetTexID ()), mesh,
				(int) (idx + cmd->IdxOffset), (int) cmd->ElemCount, (int) (vtx + cmd->VtxOffset));
		}
		vtx += list->VtxBuffer.Size;
		idx += list->IdxBuffer.Size;
	}
	renderer->setScissor (0, 0, 0, 0);
}
