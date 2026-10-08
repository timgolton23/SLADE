
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ModelCanvas.cpp
// Description: ModelCanvas class. An OpenGL canvas that displays a 3D model
//              (currently IQM) with an orbit camera. Rendering is done by
//              ModelRenderer
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "ModelCanvas.h"
#include "Archive/ArchiveEntry.h"
#include "General/Misc.h"
#include "Graphics/IQMModel.h"
#include "Graphics/SImage/SImage.h"
#include "OpenGL/GLTexture.h"
#include <cmath>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
}


// -----------------------------------------------------------------------------
//
// ModelCanvas Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// ModelCanvas class constructor
// -----------------------------------------------------------------------------
ModelCanvas::ModelCanvas(wxWindow* parent) : OGLCanvas(parent, -1, false)
{
	Bind(wxEVT_LEFT_DOWN, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_RIGHT_DOWN, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_MIDDLE_DOWN, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_LEFT_UP, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_RIGHT_UP, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_MIDDLE_UP, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_MOTION, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_MOUSEWHEEL, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_LEFT_DCLICK, &ModelCanvas::onMouseEvent, this);
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { Refresh(); e.Skip(); });
}

// -----------------------------------------------------------------------------
// ModelCanvas class destructor
// -----------------------------------------------------------------------------
ModelCanvas::~ModelCanvas()
{
	clearTextures();
}

// -----------------------------------------------------------------------------
// Sets the model to display (not owned by the canvas)
// -----------------------------------------------------------------------------
void ModelCanvas::setModel(IQMModel* model)
{
	model_ = model;
	resetView();
}

// -----------------------------------------------------------------------------
// Queues textures to load for each mesh of the model. [mesh_textures] must
// have one entry per mesh (nullptr where no texture was found). Textures are
// actually created on the next draw, when the GL context is active
// -----------------------------------------------------------------------------
void ModelCanvas::setTextures(const vector<ArchiveEntry*>& mesh_textures, Palette* pal)
{
	pending_images_.clear();
	std::map<ArchiveEntry*, shared_ptr<SImage>> loaded;
	for (auto entry : mesh_textures)
	{
		shared_ptr<SImage> image;
		if (entry)
		{
			if (auto it = loaded.find(entry); it != loaded.end())
				image = it->second;
			else
			{
				image = std::make_shared<SImage>();
				if (!misc::loadImageFromEntry(image.get(), entry))
					image.reset();
				loaded[entry] = image;
			}
		}
		pending_images_.push_back(image);
	}

	if (pal)
		pending_palette_.copyPalette(pal);
	textures_pending_ = true;
	Refresh();
}

// -----------------------------------------------------------------------------
// Deletes any loaded textures
// -----------------------------------------------------------------------------
void ModelCanvas::clearTextures()
{
	if (!textures_.empty() && gl::isInitialised() && setActive())
	{
		// Meshes can share a texture, so only delete each id once
		std::sort(textures_.begin(), textures_.end());
		textures_.erase(std::unique(textures_.begin(), textures_.end()), textures_.end());
		for (auto tex : textures_)
			if (tex)
				gl::Texture::clear(tex);
	}
	textures_.clear();
}

// -----------------------------------------------------------------------------
// Resets the camera to frame the current model
// -----------------------------------------------------------------------------
void ModelCanvas::resetView()
{
	if (model_ && model_->isValid())
	{
		camera_       = ModelRenderer::frameModel(*model_);
		model_radius_ = ModelRenderer::modelRadius(*model_);
	}
	else
	{
		camera_       = {};
		model_radius_ = 50.f;
	}

	Refresh();
}

// -----------------------------------------------------------------------------
// Creates GL textures for any queued mesh textures
// -----------------------------------------------------------------------------
void ModelCanvas::loadPendingTextures()
{
	if (!textures_pending_)
		return;

	textures_pending_ = false;
	clearTextures();

	textures_.resize(pending_images_.size(), 0);
	for (size_t i = 0; i < pending_images_.size(); ++i)
	{
		auto& image = pending_images_[i];
		if (!image)
			continue;

		// Reuse the texture already created for another mesh using the same image
		for (size_t p = 0; p < i && !textures_[i]; ++p)
			if (pending_images_[p] == image)
				textures_[i] = textures_[p];

		if (!textures_[i])
			textures_[i] = gl::Texture::createFromImage(*image, &pending_palette_, gl::TexFilter::Linear, true);
	}
	pending_images_.clear();
}

// -----------------------------------------------------------------------------
// Draws the canvas content
// -----------------------------------------------------------------------------
void ModelCanvas::draw()
{
	const wxSize size = GetSize() * GetContentScaleFactor();

	if (model_ && model_->isValid())
	{
		loadPendingTextures();
		ModelRenderer::render(*model_, textures_, camera_, options_, size.x, size.y);

		// The renderer binds textures directly, so resync SLADE's bound texture tracking
		gl::Texture::bind(0);
	}
	else
	{
		glViewport(0, 0, std::max(size.x, 1), std::max(size.y, 1));
		glClearColor(0.16f, 0.17f, 0.19f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	SwapBuffers();
}


// -----------------------------------------------------------------------------
//
// ModelCanvas Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Handles mouse input for the orbit camera
// -----------------------------------------------------------------------------
void ModelCanvas::onMouseEvent(wxMouseEvent& e)
{
	auto pos = e.GetPosition();

	if (e.ButtonDown())
	{
		SetFocus();
		if (!HasCapture())
			CaptureMouse();
		mouse_prev_ = pos;
	}
	else if (e.ButtonUp())
	{
		if (HasCapture() && !e.LeftIsDown() && !e.RightIsDown() && !e.MiddleIsDown())
			ReleaseMouse();
	}
	else if (e.ButtonDClick(wxMOUSE_BTN_LEFT))
	{
		resetView();
	}
	else if (e.Dragging())
	{
		int dx = pos.x - mouse_prev_.x;
		int dy = pos.y - mouse_prev_.y;

		if (e.LeftIsDown() && !e.ShiftDown())
		{
			// Orbit
			camera_.yaw -= dx * 0.5f;
			camera_.pitch = std::clamp(camera_.pitch + dy * 0.5f, -89.f, 89.f);
		}
		else
		{
			// Pan in the view plane
			float cy = std::cos(camera_.yaw * DEG2RAD), sy = std::sin(camera_.yaw * DEG2RAD);
			float cp = std::cos(camera_.pitch * DEG2RAD), sp = std::sin(camera_.pitch * DEG2RAD);
			// Right and up vectors of the camera
			float right[3] = { -sy, cy, 0.f };
			float up[3]    = { -sp * cy, -sp * sy, cp };
			float scale    = camera_.distance * 0.0015f;
			for (int i = 0; i < 3; ++i)
				camera_.target[i] += (-right[i] * dx + up[i] * dy) * scale;
		}

		mouse_prev_ = pos;
		Refresh();
	}
	else if (e.GetEventType() == wxEVT_MOUSEWHEEL)
	{
		float factor = e.GetWheelRotation() > 0 ? 0.85f : 1.f / 0.85f;
		camera_.distance = std::clamp(camera_.distance * factor, model_radius_ * 0.05f, model_radius_ * 50.f);
		Refresh();
	}

	e.Skip(e.ButtonDown()); // Let focus handling happen
}
