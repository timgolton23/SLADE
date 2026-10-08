
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ModelRenderer.cpp
// Description: ModelRenderer class. Draws a posed IQMModel using the OpenGL
//              fixed-function (compatibility) pipeline
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
#include "ModelRenderer.h"
#include "Graphics/IQMModel.h"
#include "OpenGL.h"
#include <cmath>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Constants / Helpers
//
// -----------------------------------------------------------------------------
namespace
{
constexpr float DEG2RAD = 3.14159265358979323846f / 180.f;
constexpr float FOV_Y   = 45.f;

// Multiplies the current matrix by a perspective projection (as gluPerspective)
void perspective(float fovy, float aspect, float znear, float zfar)
{
	float f = 1.f / std::tan(fovy * 0.5f * DEG2RAD);
	// Column-major
	float m[16] = { f / aspect, 0.f, 0.f, 0.f,
					0.f, f, 0.f, 0.f,
					0.f, 0.f, (zfar + znear) / (znear - zfar), -1.f,
					0.f, 0.f, (2.f * zfar * znear) / (znear - zfar), 0.f };
	glMultMatrixf(m);
}

// Multiplies the current matrix by a view transform (as gluLookAt)
void lookAt(const float eye[3], const float centre[3], const float up[3])
{
	float f[3] = { centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2] };
	float fl   = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	for (auto& v : f)
		v /= fl;

	// s = f x up
	float s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
	float sl   = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	for (auto& v : s)
		v /= sl;

	// u = s x f
	float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };

	float m[16] = { s[0], u[0], -f[0], 0.f, s[1], u[1], -f[1], 0.f, s[2], u[2], -f[2], 0.f, 0.f, 0.f, 0.f, 1.f };
	glMultMatrixf(m);
	glTranslatef(-eye[0], -eye[1], -eye[2]);
}
} // namespace


// -----------------------------------------------------------------------------
//
// ModelRenderer Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Returns the radius of [model]'s bind pose bounding sphere
// -----------------------------------------------------------------------------
float ModelRenderer::modelRadius(const IQMModel& model)
{
	auto  mn = model.boundsMin();
	auto  mx = model.boundsMax();
	float dx = mx.x - mn.x;
	float dy = mx.y - mn.y;
	float dz = mx.z - mn.z;
	return std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 0.01f);
}

// -----------------------------------------------------------------------------
// Returns a camera that frames [model]'s bind pose bounds
// -----------------------------------------------------------------------------
ModelRenderer::Camera ModelRenderer::frameModel(const IQMModel& model)
{
	Camera cam;
	auto   mn     = model.boundsMin();
	auto   mx     = model.boundsMax();
	cam.target[0] = (mn.x + mx.x) * 0.5f;
	cam.target[1] = (mn.y + mx.y) * 0.5f;
	cam.target[2] = (mn.z + mx.z) * 0.5f;

	// Distance so the bounding sphere fits the vertical fov, plus a margin
	cam.distance = modelRadius(model) / std::sin(FOV_Y * 0.5f * DEG2RAD) * 0.9f;
	return cam;
}

// -----------------------------------------------------------------------------
// Renders [model] in its current pose
// -----------------------------------------------------------------------------
void ModelRenderer::render(
	const IQMModel&         model,
	const vector<unsigned>& textures,
	const Camera&           camera,
	const Options&          options,
	int                     width,
	int                     height)
{
	width  = std::max(width, 1);
	height = std::max(height, 1);

	// Save GL state (SLADE shares one context between every canvas)
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	glViewport(0, 0, width, height);
	glClearColor(0.16f, 0.17f, 0.19f, 1.f);
	glClearDepth(1.0);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if (model.isValid())
	{
		float radius = modelRadius(model);

		// Projection
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		// Clip planes scale with the model and camera distance to keep depth precision
		float znear = std::max(camera.distance * 0.01f, radius * 0.001f);
		float zfar  = camera.distance + radius * 12.f;
		perspective(FOV_Y, static_cast<float>(width) / static_cast<float>(height), znear, zfar);

		// Light, fixed relative to the camera (set before the view transform)
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		GLfloat light_pos[]  = { -0.4f, 0.6f, 1.f, 0.f };
		GLfloat light_diff[] = { 0.9f, 0.9f, 0.9f, 1.f };
		GLfloat light_amb[]  = { 0.45f, 0.45f, 0.47f, 1.f };
		glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
		glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diff);
		glLightfv(GL_LIGHT0, GL_AMBIENT, light_amb);

		// Camera: Z up, orbiting the target
		float cy     = std::cos(camera.yaw * DEG2RAD);
		float sy     = std::sin(camera.yaw * DEG2RAD);
		float cp     = std::cos(camera.pitch * DEG2RAD);
		float sp     = std::sin(camera.pitch * DEG2RAD);
		float eye[3] = { camera.target[0] + camera.distance * cp * cy,
						 camera.target[1] + camera.distance * cp * sy,
						 camera.target[2] + camera.distance * sp };
		float up[3]  = { 0.f, 0.f, 1.f };
		lookAt(eye, camera.target, up);

		// Common state
		glDisable(GL_CULL_FACE);
		glDisable(GL_FOG);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_LIGHTING);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);

		if (options.show_grid)
		{
			glDepthMask(GL_FALSE);
			drawGrid(model, camera);
			glDepthMask(GL_TRUE);
		}

		// Model
		const auto& positions = model.posedPositions();
		const auto& normals   = model.posedNormals();
		const auto& texcoords = model.texCoords();
		const auto& tris      = model.triangles();
		const auto& meshes    = model.meshes();
		bool        has_uv    = model.hasTexCoords();

		if (options.wireframe)
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glColor4f(0.75f, 0.85f, 1.f, 1.f);
		}
		else
		{
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glEnable(GL_LIGHTING);
			glEnable(GL_LIGHT0);
			glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
			glEnable(GL_COLOR_MATERIAL);
			glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
			glEnable(GL_NORMALIZE);
			glShadeModel(GL_SMOOTH);
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.5f);
		}

		for (size_t m = 0; m < meshes.size(); ++m)
		{
			const auto& mesh = meshes[m];
			unsigned    tex  = (!options.wireframe && has_uv && m < textures.size()) ? textures[m] : 0;

			if (tex)
			{
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, tex);
				glColor4f(1.f, 1.f, 1.f, 1.f);
			}
			else
			{
				glDisable(GL_TEXTURE_2D);
				if (!options.wireframe)
					glColor4f(0.72f, 0.72f, 0.74f, 1.f);
			}

			size_t start = size_t(mesh.first_triangle) * 3;
			size_t end   = std::min(start + size_t(mesh.num_triangles) * 3, tris.size());

			glBegin(GL_TRIANGLES);
			for (size_t t = start; t < end; ++t)
			{
				uint32_t v = tris[t];
				if (tex)
					glTexCoord2f(texcoords[v * 2], texcoords[v * 2 + 1]);
				glNormal3f(normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
				glVertex3f(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]);
			}
			glEnd();
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_LIGHTING);
		glDisable(GL_ALPHA_TEST);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		if (options.show_skeleton)
			drawSkeleton(model);
	}

	// Restore GL state
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glPopAttrib();
}

// -----------------------------------------------------------------------------
// Draws a reference grid on the ground (XY) plane beneath the model, plus
// X (red) / Y (green) / Z (blue) axes at the origin
// -----------------------------------------------------------------------------
void ModelRenderer::drawGrid(const IQMModel& model, const Camera& camera)
{
	float radius = modelRadius(model);

	// Pick a 'nice' power-of-10 grid step relative to the model size
	float step = std::pow(10.f, std::floor(std::log10(radius)));
	if (radius / step < 3.f)
		step *= 0.5f;
	int   lines = 10;
	float ext   = step * lines;
	float z     = model.boundsMin().z;
	float cx    = std::round(camera.target[0] / step) * step;
	float cy    = std::round(camera.target[1] / step) * step;

	glLineWidth(1.f);
	glBegin(GL_LINES);
	for (int i = -lines; i <= lines; ++i)
	{
		float o = i * step;
		if (i == 0)
			glColor4f(0.5f, 0.5f, 0.55f, 0.8f);
		else
			glColor4f(0.35f, 0.35f, 0.4f, 0.5f);
		glVertex3f(cx + o, cy - ext, z);
		glVertex3f(cx + o, cy + ext, z);
		glVertex3f(cx - ext, cy + o, z);
		glVertex3f(cx + ext, cy + o, z);
	}
	glEnd();

	float axis = step * 2.f;
	glLineWidth(2.f);
	glBegin(GL_LINES);
	glColor3f(0.9f, 0.25f, 0.25f);
	glVertex3f(0.f, 0.f, z);
	glVertex3f(axis, 0.f, z);
	glColor3f(0.25f, 0.85f, 0.25f);
	glVertex3f(0.f, 0.f, z);
	glVertex3f(0.f, axis, z);
	glColor3f(0.3f, 0.45f, 1.f);
	glVertex3f(0.f, 0.f, z);
	glVertex3f(0.f, 0.f, z + axis);
	glEnd();
	glLineWidth(1.f);
}

// -----------------------------------------------------------------------------
// Draws the posed skeleton (bones + joints) on top of the model
// -----------------------------------------------------------------------------
void ModelRenderer::drawSkeleton(const IQMModel& model)
{
	if (!model.hasSkeleton())
		return;

	const auto& joints = model.joints();
	const auto& pos    = model.posedJoints();

	glDisable(GL_DEPTH_TEST);
	glLineWidth(2.f);
	glBegin(GL_LINES);
	glColor3f(1.f, 0.8f, 0.2f);
	for (size_t j = 0; j < pos.size() && j < joints.size(); ++j)
	{
		int parent = joints[j].parent;
		if (parent < 0 || parent >= static_cast<int>(pos.size()))
			continue;
		glVertex3f(pos[parent].x, pos[parent].y, pos[parent].z);
		glVertex3f(pos[j].x, pos[j].y, pos[j].z);
	}
	glEnd();

	glPointSize(5.f);
	glBegin(GL_POINTS);
	glColor3f(1.f, 0.4f, 0.1f);
	for (auto& p : pos)
		glVertex3f(p.x, p.y, p.z);
	glEnd();
	glPointSize(1.f);
	glLineWidth(1.f);
}
