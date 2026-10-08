#pragma once

namespace slade
{
class IQMModel;

// -----------------------------------------------------------------------------
// ModelRenderer
//
// Draws an IQMModel (in its current pose) with the OpenGL fixed-function
// pipeline: perspective orbit camera, simple directional lighting, optional
// per-mesh textures, wireframe, skeleton and ground grid.
// Does not depend on any UI classes, so it can be used by any GL canvas.
// -----------------------------------------------------------------------------
class ModelRenderer
{
public:
	struct Camera
	{
		float yaw      = 35.f;  // Degrees around the Z (up) axis (0 = looking at the +X facing front)
		float pitch    = 20.f;   // Degrees above the ground plane
		float distance = 100.f;  // From target
		float target[3] = { 0.f, 0.f, 0.f };
	};

	struct Options
	{
		bool wireframe     = false;
		bool show_skeleton = false;
		bool show_grid     = true;
	};

	// Returns a camera that frames the bounds of [model] (bind pose)
	static Camera frameModel(const IQMModel& model);

	// Returns the radius of [model]'s bind pose bounding sphere
	static float modelRadius(const IQMModel& model);

	// Renders [model] to the current GL context's [width]x[height] viewport.
	// [textures] holds a GL texture id per mesh (0 = untextured), and may be
	// shorter than the mesh list. All GL state touched is restored afterwards
	static void render(
		const IQMModel&         model,
		const vector<unsigned>& textures,
		const Camera&           camera,
		const Options&          options,
		int                     width,
		int                     height);

private:
	static void drawGrid(const IQMModel& model, const Camera& camera);
	static void drawSkeleton(const IQMModel& model);
};
} // namespace slade
