#pragma once

namespace slade
{
// -----------------------------------------------------------------------------
// IQMModel
//
// Loader + CPU skeletal animation for Inter-Quake Model (.iqm, version 2)
// files, as supported by GZDoom and derivatives.
// -----------------------------------------------------------------------------
class IQMModel
{
public:
	struct Vec3
	{
		float x = 0.f, y = 0.f, z = 0.f;
	};

	struct Quat
	{
		float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
	};

	// 3x4 affine matrix (3 rows of {x, y, z, translation})
	struct Mat3x4
	{
		float a[4] = { 1.f, 0.f, 0.f, 0.f };
		float b[4] = { 0.f, 1.f, 0.f, 0.f };
		float c[4] = { 0.f, 0.f, 1.f, 0.f };

		static Mat3x4 fromTRS(const Quat& rot, const Vec3& trans, const Vec3& scale);

		Mat3x4 operator*(const Mat3x4& o) const;
		Mat3x4 operator*(float s) const;
		Mat3x4 operator+(const Mat3x4& o) const;
		Mat3x4 inverse() const;
		Vec3   transform(const Vec3& v) const;
		Vec3   transformNormal(const Vec3& v) const;
		Vec3   translation() const { return { a[3], b[3], c[3] }; }
	};

	struct Mesh
	{
		string   name;
		string   material;
		unsigned first_vertex   = 0;
		unsigned num_vertexes   = 0;
		unsigned first_triangle = 0;
		unsigned num_triangles  = 0;
	};

	struct Joint
	{
		string name;
		int    parent = -1;
		Vec3   translate;
		Quat   rotate;
		Vec3   scale{ 1.f, 1.f, 1.f };
	};

	struct Anim
	{
		string   name;
		unsigned first_frame = 0;
		unsigned num_frames  = 0;
		float    framerate   = 0.f;
		bool     loop        = false;
	};

	IQMModel()  = default;
	~IQMModel() = default;

	bool          open(const MemChunk& data);
	void          clear();
	bool          isValid() const { return valid_; }
	const string& error() const { return error_; }

	const vector<Mesh>&     meshes() const { return meshes_; }
	const vector<Joint>&    joints() const { return joints_; }
	const vector<Anim>&     anims() const { return anims_; }
	const vector<uint32_t>& triangles() const { return triangles_; }
	const vector<float>&    texCoords() const { return texcoords_; }
	bool                    hasTexCoords() const { return !texcoords_.empty(); }
	bool                    hasSkeleton() const { return !joints_.empty(); }
	unsigned                numVertices() const { return num_vertexes_; }
	unsigned                numFrames() const { return num_frames_; }
	unsigned                numPoses() const { return num_poses_; }

	// Bounds of the bind pose
	Vec3 boundsMin() const { return bbox_min_; }
	Vec3 boundsMax() const { return bbox_max_; }

	// Computes the skinned vertex positions/normals for [anim] at [frame]
	// (fractional frames are interpolated). If [anim] is negative the bind
	// pose is used. If [loop] is true the last frame blends back into the
	// first. Results are available via posedPositions/posedNormals.
	void animate(int anim, float frame, bool loop = true);

	const vector<float>& posedPositions() const { return out_positions_; }
	const vector<float>& posedNormals() const { return out_normals_; }

	// World-space joint positions for the last animate() call (for skeleton
	// display), one per joint/pose
	const vector<Vec3>& posedJoints() const { return out_joint_pos_; }

private:
	bool   valid_ = false;
	string error_;

	unsigned num_vertexes_ = 0;
	unsigned num_frames_   = 0;
	unsigned num_poses_    = 0;

	vector<Mesh>     meshes_;
	vector<Joint>    joints_;
	vector<Anim>     anims_;
	vector<uint32_t> triangles_;

	// Vertex data (unposed)
	vector<float>   positions_; // 3 per vertex
	vector<float>   normals_;   // 3 per vertex
	vector<float>   texcoords_; // 2 per vertex
	vector<uint8_t> blend_idx_; // 4 per vertex
	vector<uint8_t> blend_wt_;  // 4 per vertex

	// Skeleton
	vector<Mat3x4> base_frame_;
	vector<Mat3x4> inv_base_frame_;
	vector<int>    pose_parents_;
	vector<Mat3x4> frames_; // num_frames_ * num_poses_

	Vec3 bbox_min_;
	Vec3 bbox_max_;

	// Animation output
	vector<Mat3x4> out_frame_;
	vector<float>  out_positions_;
	vector<float>  out_normals_;
	vector<Vec3>   out_joint_pos_;

	bool fail(string_view message);
};
} // namespace slade
