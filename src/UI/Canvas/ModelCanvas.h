#pragma once

#include "OGLCanvas.h"
#include "OpenGL/ModelRenderer.h"

namespace slade
{
class IQMModel;
class ArchiveEntry;
class SImage;

// -----------------------------------------------------------------------------
// ModelCanvas
//
// OpenGL canvas that displays a (skeletally animated) 3D model with an orbit
// camera. Left-drag rotates, right/middle-drag (or shift+left-drag) pans,
// mouse wheel zooms and double-click resets the view.
// -----------------------------------------------------------------------------
class ModelCanvas : public OGLCanvas
{
public:
	ModelCanvas(wxWindow* parent);
	~ModelCanvas() override;

	void setModel(IQMModel* model);
	void setTextures(const vector<ArchiveEntry*>& mesh_textures, Palette* pal);
	void clearTextures();
	void resetView();

	void setWireframe(bool wf)
	{
		options_.wireframe = wf;
		Refresh();
	}
	void setShowSkeleton(bool show)
	{
		options_.show_skeleton = show;
		Refresh();
	}
	void setShowGrid(bool show)
	{
		options_.show_grid = show;
		Refresh();
	}

	void draw() override;

private:
	IQMModel*              model_ = nullptr;
	ModelRenderer::Camera  camera_;
	ModelRenderer::Options options_;
	float                  model_radius_ = 50.f;

	// Textures (one GL texture id per mesh, 0 = none). Images are decoded when
	// set and uploaded lazily on draw, when the GL context is active
	vector<unsigned>           textures_;
	vector<shared_ptr<SImage>> pending_images_; // One per mesh (may be null/shared)
	Palette                    pending_palette_;
	bool                       textures_pending_ = false;

	wxPoint mouse_prev_;

	void loadPendingTextures();

	// Events
	void onMouseEvent(wxMouseEvent& e);
};
} // namespace slade
