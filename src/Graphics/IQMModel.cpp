
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    IQMModel.cpp
// Description: Loader for Inter-Quake Model (IQM v2) files, with CPU-side
//              skeletal animation (frame interpolation + vertex skinning)
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
#include "IQMModel.h"
#include <cmath>
#include <cstring>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Constants / Helpers
//
// -----------------------------------------------------------------------------
namespace
{
constexpr const char* IQM_MAGIC   = "INTERQUAKEMODEL";
constexpr unsigned    IQM_VERSION = 2;

enum IQMVertexArrayType : uint32_t
{
	IQM_POSITION     = 0,
	IQM_TEXCOORD     = 1,
	IQM_NORMAL       = 2,
	IQM_TANGENT      = 3,
	IQM_BLENDINDEXES = 4,
	IQM_BLENDWEIGHTS = 5,
	IQM_COLOR        = 6,
};

enum IQMVertexArrayFormat : uint32_t
{
	IQM_BYTE   = 0,
	IQM_UBYTE  = 1,
	IQM_SHORT  = 2,
	IQM_USHORT = 3,
	IQM_INT    = 4,
	IQM_UINT   = 5,
	IQM_HALF   = 6,
	IQM_FLOAT  = 7,
	IQM_DOUBLE = 8,
};

constexpr uint32_t IQM_LOOP = 1;

// Header field indices (each a uint32 following the 16 byte magic)
enum HeaderField
{
	H_VERSION = 0,
	H_FILESIZE,
	H_FLAGS,
	H_NUM_TEXT,
	H_OFS_TEXT,
	H_NUM_MESHES,
	H_OFS_MESHES,
	H_NUM_VERTEXARRAYS,
	H_NUM_VERTEXES,
	H_OFS_VERTEXARRAYS,
	H_NUM_TRIANGLES,
	H_OFS_TRIANGLES,
	H_OFS_ADJACENCY,
	H_NUM_JOINTS,
	H_OFS_JOINTS,
	H_NUM_POSES,
	H_OFS_POSES,
	H_NUM_ANIMS,
	H_OFS_ANIMS,
	H_NUM_FRAMES,
	H_NUM_FRAMECHANNELS,
	H_OFS_FRAMES,
	H_OFS_BOUNDS,
	H_NUM_COMMENT,
	H_OFS_COMMENT,
	H_NUM_EXTENSIONS,
	H_OFS_EXTENSIONS,

	H_COUNT
};
constexpr unsigned HEADER_SIZE = 16 + H_COUNT * 4;

// Simple bounds-checked little-endian reader over a block of memory
class Reader
{
public:
	Reader(const uint8_t* data, size_t size) : data_{ data }, size_{ size } {}

	bool inRange(size_t offset, size_t count, size_t elem_size) const
	{
		if (elem_size != 0 && count > (SIZE_MAX / elem_size))
			return false;
		size_t bytes = count * elem_size;
		return offset <= size_ && bytes <= size_ - offset;
	}

	uint32_t u32(size_t ofs) const
	{
		return uint32_t(data_[ofs]) | (uint32_t(data_[ofs + 1]) << 8) | (uint32_t(data_[ofs + 2]) << 16)
			   | (uint32_t(data_[ofs + 3]) << 24);
	}
	int32_t  i32(size_t ofs) const { return static_cast<int32_t>(u32(ofs)); }
	uint16_t u16(size_t ofs) const { return uint16_t(data_[ofs]) | (uint16_t(data_[ofs + 1]) << 8); }
	float    f32(size_t ofs) const
	{
		uint32_t v = u32(ofs);
		float    f;
		std::memcpy(&f, &v, 4);
		return f;
	}
	double f64(size_t ofs) const
	{
		uint64_t v = uint64_t(u32(ofs)) | (uint64_t(u32(ofs + 4)) << 32);
		double   d;
		std::memcpy(&d, &v, 8);
		return d;
	}
	uint8_t u8(size_t ofs) const { return data_[ofs]; }

	// Reads a null-terminated string from the text block
	string text(size_t ofs_text, size_t num_text, uint32_t index) const
	{
		if (index >= num_text)
			return {};
		size_t start = ofs_text + index;
		size_t end   = start;
		size_t limit = ofs_text + num_text;
		while (end < limit && data_[end] != 0)
			++end;
		return string(reinterpret_cast<const char*>(data_ + start), end - start);
	}

private:
	const uint8_t* data_;
	size_t         size_;
};

unsigned formatSize(uint32_t format)
{
	switch (format)
	{
	case IQM_BYTE:
	case IQM_UBYTE:  return 1;
	case IQM_SHORT:
	case IQM_USHORT:
	case IQM_HALF:   return 2;
	case IQM_INT:
	case IQM_UINT:
	case IQM_FLOAT:  return 4;
	case IQM_DOUBLE: return 8;
	default:         return 0;
	}
}

float halfToFloat(uint16_t h)
{
	uint32_t sign = (h >> 15) & 1;
	uint32_t exp  = (h >> 10) & 0x1f;
	uint32_t mant = h & 0x3ff;
	float    val;
	if (exp == 0)
		val = std::ldexp(static_cast<float>(mant), -24);
	else if (exp == 31)
		val = mant ? NAN : INFINITY;
	else
		val = std::ldexp(static_cast<float>(mant | 0x400), static_cast<int>(exp) - 25);
	return sign ? -val : val;
}

// Reads a single vertex array component as a float
float readComponent(const Reader& r, size_t ofs, uint32_t format)
{
	switch (format)
	{
	case IQM_BYTE:   return static_cast<float>(static_cast<int8_t>(r.u8(ofs)));
	case IQM_UBYTE:  return static_cast<float>(r.u8(ofs));
	case IQM_SHORT:  return static_cast<float>(static_cast<int16_t>(r.u16(ofs)));
	case IQM_USHORT: return static_cast<float>(r.u16(ofs));
	case IQM_INT:    return static_cast<float>(r.i32(ofs));
	case IQM_UINT:   return static_cast<float>(r.u32(ofs));
	case IQM_HALF:   return halfToFloat(r.u16(ofs));
	case IQM_FLOAT:  return r.f32(ofs);
	case IQM_DOUBLE: return static_cast<float>(r.f64(ofs));
	default:         return 0.f;
	}
}

IQMModel::Quat normalise(IQMModel::Quat q)
{
	float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len > 0.f)
	{
		q.x /= len;
		q.y /= len;
		q.z /= len;
		q.w /= len;
	}
	else
		q = {};
	return q;
}
} // namespace


// -----------------------------------------------------------------------------
//
// IQMModel::Mat3x4 Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Builds a matrix from a rotation quaternion, translation and scale
// -----------------------------------------------------------------------------
IQMModel::Mat3x4 IQMModel::Mat3x4::fromTRS(const Quat& q, const Vec3& t, const Vec3& s)
{
	float x2 = q.x * 2.f, y2 = q.y * 2.f, z2 = q.z * 2.f;
	float xx = q.x * x2, yy = q.y * y2, zz = q.z * z2;
	float xy = q.x * y2, xz = q.x * z2, yz = q.y * z2;
	float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

	Mat3x4 m;
	m.a[0] = (1.f - (yy + zz)) * s.x;
	m.a[1] = (xy - wz) * s.y;
	m.a[2] = (xz + wy) * s.z;
	m.a[3] = t.x;
	m.b[0] = (xy + wz) * s.x;
	m.b[1] = (1.f - (xx + zz)) * s.y;
	m.b[2] = (yz - wx) * s.z;
	m.b[3] = t.y;
	m.c[0] = (xz - wy) * s.x;
	m.c[1] = (yz + wx) * s.y;
	m.c[2] = (1.f - (xx + yy)) * s.z;
	m.c[3] = t.z;
	return m;
}

// -----------------------------------------------------------------------------
// Affine matrix multiply (this * o)
// -----------------------------------------------------------------------------
IQMModel::Mat3x4 IQMModel::Mat3x4::operator*(const Mat3x4& o) const
{
	Mat3x4      r;
	const float* rows_in[3]  = { a, b, c };
	float*       rows_out[3] = { r.a, r.b, r.c };
	for (int i = 0; i < 3; ++i)
	{
		const float* m = rows_in[i];
		for (int j = 0; j < 4; ++j)
			rows_out[i][j] = m[0] * o.a[j] + m[1] * o.b[j] + m[2] * o.c[j];
		rows_out[i][3] += m[3];
	}
	return r;
}

IQMModel::Mat3x4 IQMModel::Mat3x4::operator*(float s) const
{
	Mat3x4 r;
	for (int j = 0; j < 4; ++j)
	{
		r.a[j] = a[j] * s;
		r.b[j] = b[j] * s;
		r.c[j] = c[j] * s;
	}
	return r;
}

IQMModel::Mat3x4 IQMModel::Mat3x4::operator+(const Mat3x4& o) const
{
	Mat3x4 r;
	for (int j = 0; j < 4; ++j)
	{
		r.a[j] = a[j] + o.a[j];
		r.b[j] = b[j] + o.b[j];
		r.c[j] = c[j] + o.c[j];
	}
	return r;
}

// -----------------------------------------------------------------------------
// Returns the inverse of this affine matrix (handles non-uniform scale)
// -----------------------------------------------------------------------------
IQMModel::Mat3x4 IQMModel::Mat3x4::inverse() const
{
	// Inverse of the 3x3 part via cofactors
	float c00 = b[1] * c[2] - b[2] * c[1];
	float c01 = a[2] * c[1] - a[1] * c[2];
	float c02 = a[1] * b[2] - a[2] * b[1];
	float c10 = b[2] * c[0] - b[0] * c[2];
	float c11 = a[0] * c[2] - a[2] * c[0];
	float c12 = a[2] * b[0] - a[0] * b[2];
	float c20 = b[0] * c[1] - b[1] * c[0];
	float c21 = a[1] * c[0] - a[0] * c[1];
	float c22 = a[0] * b[1] - a[1] * b[0];

	float det = a[0] * c00 + a[1] * c10 + a[2] * c20;
	if (std::fabs(det) < 1e-12f)
		return {};
	float inv = 1.f / det;

	Mat3x4 r;
	r.a[0] = c00 * inv;
	r.a[1] = c01 * inv;
	r.a[2] = c02 * inv;
	r.b[0] = c10 * inv;
	r.b[1] = c11 * inv;
	r.b[2] = c12 * inv;
	r.c[0] = c20 * inv;
	r.c[1] = c21 * inv;
	r.c[2] = c22 * inv;

	// Translation = -R^-1 * t
	r.a[3] = -(r.a[0] * a[3] + r.a[1] * b[3] + r.a[2] * c[3]);
	r.b[3] = -(r.b[0] * a[3] + r.b[1] * b[3] + r.b[2] * c[3]);
	r.c[3] = -(r.c[0] * a[3] + r.c[1] * b[3] + r.c[2] * c[3]);
	return r;
}

IQMModel::Vec3 IQMModel::Mat3x4::transform(const Vec3& v) const
{
	return { a[0] * v.x + a[1] * v.y + a[2] * v.z + a[3],
			 b[0] * v.x + b[1] * v.y + b[2] * v.z + b[3],
			 c[0] * v.x + c[1] * v.y + c[2] * v.z + c[3] };
}

// -----------------------------------------------------------------------------
// Transforms a normal vector by the cofactor (adjugate-transpose) of the 3x3
// part of the matrix. This is proportional to the inverse-transpose, so it
// remains correct with (non-uniform) joint scaling and blended matrices.
// The result is not normalised
// -----------------------------------------------------------------------------
IQMModel::Vec3 IQMModel::Mat3x4::transformNormal(const Vec3& v) const
{
	auto cross = [](const float* p, const float* q) -> Vec3
	{ return { p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0] }; };

	Vec3 r0 = cross(b, c);
	Vec3 r1 = cross(c, a);
	Vec3 r2 = cross(a, b);
	return { r0.x * v.x + r0.y * v.y + r0.z * v.z,
			 r1.x * v.x + r1.y * v.y + r1.z * v.z,
			 r2.x * v.x + r2.y * v.y + r2.z * v.z };
}


// -----------------------------------------------------------------------------
//
// IQMModel Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Clears all model data
// -----------------------------------------------------------------------------
void IQMModel::clear()
{
	valid_ = false;
	error_.clear();
	num_vertexes_ = 0;
	num_frames_   = 0;
	num_poses_    = 0;
	meshes_.clear();
	joints_.clear();
	anims_.clear();
	triangles_.clear();
	positions_.clear();
	normals_.clear();
	texcoords_.clear();
	blend_idx_.clear();
	blend_wt_.clear();
	base_frame_.clear();
	inv_base_frame_.clear();
	pose_parents_.clear();
	frames_.clear();
	out_frame_.clear();
	out_positions_.clear();
	out_normals_.clear();
	out_joint_pos_.clear();
	bbox_min_ = {};
	bbox_max_ = {};
}

// -----------------------------------------------------------------------------
// Sets the error message, clears the model and returns false
// -----------------------------------------------------------------------------
bool IQMModel::fail(string_view message)
{
	clear();
	error_ = string{ message };
	return false;
}

// -----------------------------------------------------------------------------
// Loads an IQM model from [data]. Returns false on error (see error())
// -----------------------------------------------------------------------------
bool IQMModel::open(const MemChunk& data)
{
	clear();

	if (data.size() < HEADER_SIZE)
		return fail("File too small to be an IQM model");

	Reader r(data.data(), data.size());

	// Check magic
	if (std::memcmp(data.data(), IQM_MAGIC, 16) != 0)
		return fail("Invalid IQM header");

	uint32_t h[H_COUNT];
	for (unsigned i = 0; i < H_COUNT; ++i)
		h[i] = r.u32(16 + i * 4);

	if (h[H_VERSION] != IQM_VERSION)
		return fail(fmt::format("Unsupported IQM version {} (only version 2 is supported)", h[H_VERSION]));

	const size_t ofs_text = h[H_OFS_TEXT];
	const size_t num_text = h[H_NUM_TEXT];
	if (num_text > 0 && !r.inRange(ofs_text, num_text, 1))
		return fail("IQM text block out of range");

	// --- Vertex arrays ---
	num_vertexes_ = h[H_NUM_VERTEXES];
	if (!r.inRange(h[H_OFS_VERTEXARRAYS], h[H_NUM_VERTEXARRAYS], 20))
		return fail("IQM vertex array table out of range");

	for (unsigned va = 0; va < h[H_NUM_VERTEXARRAYS]; ++va)
	{
		size_t   base   = h[H_OFS_VERTEXARRAYS] + va * 20;
		uint32_t type   = r.u32(base);
		uint32_t format = r.u32(base + 8);
		uint32_t size   = r.u32(base + 12);
		uint32_t offset = r.u32(base + 16);
		unsigned fsize  = formatSize(format);

		if (fsize == 0 || size == 0 || size > 16)
			continue;
		if (!r.inRange(offset, size_t(num_vertexes_) * size, fsize))
			return fail("IQM vertex array data out of range");

		auto read_floats = [&](vector<float>& out, unsigned want)
		{
			out.assign(size_t(num_vertexes_) * want, 0.f);
			for (unsigned v = 0; v < num_vertexes_; ++v)
				for (unsigned c = 0; c < want && c < size; ++c)
					out[v * want + c] = readComponent(r, offset + (size_t(v) * size + c) * fsize, format);
		};

		switch (type)
		{
		case IQM_POSITION: read_floats(positions_, 3); break;
		case IQM_NORMAL:   read_floats(normals_, 3); break;
		case IQM_TEXCOORD: read_floats(texcoords_, 2); break;
		case IQM_BLENDINDEXES:
		{
			blend_idx_.assign(size_t(num_vertexes_) * 4, 0);
			for (unsigned v = 0; v < num_vertexes_; ++v)
				for (unsigned c = 0; c < 4 && c < size; ++c)
					blend_idx_[v * 4 + c] = static_cast<uint8_t>(
						readComponent(r, offset + (size_t(v) * size + c) * fsize, format));
			break;
		}
		case IQM_BLENDWEIGHTS:
		{
			// Weights are normalised to 0-255 regardless of storage format
			bool is_float = (format == IQM_HALF || format == IQM_FLOAT || format == IQM_DOUBLE);
			blend_wt_.assign(size_t(num_vertexes_) * 4, 0);
			for (unsigned v = 0; v < num_vertexes_; ++v)
				for (unsigned c = 0; c < 4 && c < size; ++c)
				{
					float w = readComponent(r, offset + (size_t(v) * size + c) * fsize, format);
					if (is_float)
						w *= 255.f;
					blend_wt_[v * 4 + c] = static_cast<uint8_t>(std::clamp(w + (is_float ? 0.5f : 0.f), 0.f, 255.f));
				}
			break;
		}
		default: break;
		}
	}

	if (positions_.empty())
		return fail("IQM model has no vertex positions");

	// --- Triangles ---
	if (!r.inRange(h[H_OFS_TRIANGLES], h[H_NUM_TRIANGLES], 12))
		return fail("IQM triangle data out of range");
	triangles_.resize(size_t(h[H_NUM_TRIANGLES]) * 3);
	for (size_t i = 0; i < triangles_.size(); ++i)
	{
		triangles_[i] = r.u32(h[H_OFS_TRIANGLES] + i * 4);
		if (triangles_[i] >= num_vertexes_)
			return fail("IQM triangle references an invalid vertex");
	}

	// --- Meshes ---
	if (!r.inRange(h[H_OFS_MESHES], h[H_NUM_MESHES], 24))
		return fail("IQM mesh table out of range");
	for (unsigned m = 0; m < h[H_NUM_MESHES]; ++m)
	{
		size_t base = h[H_OFS_MESHES] + m * 24;
		Mesh   mesh;
		mesh.name           = r.text(ofs_text, num_text, r.u32(base));
		mesh.material       = r.text(ofs_text, num_text, r.u32(base + 4));
		mesh.first_vertex   = r.u32(base + 8);
		mesh.num_vertexes   = r.u32(base + 12);
		mesh.first_triangle = r.u32(base + 16);
		mesh.num_triangles  = r.u32(base + 20);
		if (size_t(mesh.first_triangle) + mesh.num_triangles > h[H_NUM_TRIANGLES])
			return fail("IQM mesh references invalid triangles");
		meshes_.push_back(mesh);
	}

	// No meshes defined but there is geometry? Treat as a single mesh
	if (meshes_.empty() && !triangles_.empty())
	{
		Mesh mesh;
		mesh.name          = "mesh";
		mesh.num_vertexes  = num_vertexes_;
		mesh.num_triangles = h[H_NUM_TRIANGLES];
		meshes_.push_back(mesh);
	}

	// --- Joints (bind pose skeleton) ---
	// v2 joint: uint name, int parent, float translate[3], rotate[4], scale[3] = 48 bytes
	if (!r.inRange(h[H_OFS_JOINTS], h[H_NUM_JOINTS], 48))
		return fail("IQM joint data out of range");
	for (unsigned j = 0; j < h[H_NUM_JOINTS]; ++j)
	{
		size_t base = h[H_OFS_JOINTS] + j * 48;
		Joint  joint;
		joint.name   = r.text(ofs_text, num_text, r.u32(base));
		joint.parent = r.i32(base + 4);
		joint.translate = { r.f32(base + 8), r.f32(base + 12), r.f32(base + 16) };
		joint.rotate    = normalise({ r.f32(base + 20), r.f32(base + 24), r.f32(base + 28), r.f32(base + 32) });
		joint.scale     = { r.f32(base + 36), r.f32(base + 40), r.f32(base + 44) };
		if (joint.parent >= static_cast<int>(j))
			joint.parent = -1; // Parents must come before children
		joints_.push_back(joint);
	}

	base_frame_.resize(joints_.size());
	inv_base_frame_.resize(joints_.size());
	for (size_t j = 0; j < joints_.size(); ++j)
	{
		auto& joint         = joints_[j];
		base_frame_[j]      = Mat3x4::fromTRS(joint.rotate, joint.translate, joint.scale);
		inv_base_frame_[j]  = base_frame_[j].inverse();
		if (joint.parent >= 0)
		{
			base_frame_[j]     = base_frame_[joint.parent] * base_frame_[j];
			inv_base_frame_[j] = inv_base_frame_[j] * inv_base_frame_[joint.parent];
		}
	}

	// --- Poses + frames ---
	num_poses_  = h[H_NUM_POSES];
	num_frames_ = h[H_NUM_FRAMES];
	// pose: int parent, uint mask, float channeloffset[10], channelscale[10] = 88 bytes
	if (!r.inRange(h[H_OFS_POSES], num_poses_, 88))
		return fail("IQM pose data out of range");

	if (num_poses_ > 0 && num_frames_ > 0)
	{
		if (num_poses_ != joints_.size())
		{
			log::warning("IQM: pose count ({}) doesn't match joint count ({})", num_poses_, joints_.size());
			if (num_poses_ > joints_.size())
				num_poses_ = static_cast<unsigned>(joints_.size());
		}

		size_t frame_ofs = h[H_OFS_FRAMES];
		if (!r.inRange(frame_ofs, size_t(num_frames_) * h[H_NUM_FRAMECHANNELS], 2))
			return fail("IQM frame data out of range");

		// Frame data size is unbounded when there are no animated channels, so
		// sanity check the frame matrix allocation (256MB max)
		if (size_t(num_frames_) * num_poses_ > (256u << 20) / sizeof(Mat3x4))
			return fail("IQM model has too many frames");

		frames_.resize(size_t(num_frames_) * num_poses_);
		size_t channel  = 0;
		size_t channels = size_t(num_frames_) * h[H_NUM_FRAMECHANNELS];

		for (unsigned f = 0; f < num_frames_; ++f)
		{
			for (unsigned p = 0; p < num_poses_; ++p)
			{
				size_t   base   = h[H_OFS_POSES] + p * 88;
				int      parent = r.i32(base);
				uint32_t mask   = r.u32(base + 4);

				float val[10];
				for (int c = 0; c < 10; ++c)
				{
					val[c] = r.f32(base + 8 + c * 4);
					if (mask & (1u << c))
					{
						if (channel >= channels)
							return fail("IQM frame data is truncated");
						val[c] += r.u16(frame_ofs + channel * 2) * r.f32(base + 48 + c * 4);
						++channel;
					}
				}

				Quat   rot = normalise({ val[3], val[4], val[5], val[6] });
				Mat3x4 m   = Mat3x4::fromTRS(rot, { val[0], val[1], val[2] }, { val[7], val[8], val[9] });

				// Concatenate each pose with the inverse base pose to avoid doing this at animation time.
				// If the joint has a parent, then it needs to be pre-concatenated with its parent's base pose.
				auto& out = frames_[size_t(f) * num_poses_ + p];
				if (parent >= 0 && parent < static_cast<int>(p))
					out = base_frame_[parent] * m * inv_base_frame_[p];
				else
					out = m * inv_base_frame_[p];
			}
		}

		// Keep pose parents for animation
		pose_parents_.resize(num_poses_);
		for (unsigned p = 0; p < num_poses_; ++p)
		{
			int parent       = r.i32(h[H_OFS_POSES] + p * 88);
			pose_parents_[p] = (parent < static_cast<int>(p)) ? parent : -1;
		}
	}
	else
	{
		num_frames_ = 0;
		num_poses_  = 0;
	}

	// --- Animations ---
	// anim: uint name, first_frame, num_frames, float framerate, uint flags = 20 bytes
	if (!r.inRange(h[H_OFS_ANIMS], h[H_NUM_ANIMS], 20))
		return fail("IQM animation data out of range");
	for (unsigned a = 0; a < h[H_NUM_ANIMS]; ++a)
	{
		size_t base = h[H_OFS_ANIMS] + a * 20;
		Anim   anim;
		anim.name        = r.text(ofs_text, num_text, r.u32(base));
		anim.first_frame = r.u32(base + 4);
		anim.num_frames  = r.u32(base + 8);
		anim.framerate   = r.f32(base + 12);
		anim.loop        = (r.u32(base + 16) & IQM_LOOP) != 0;

		// Clamp to the available frames
		if (anim.first_frame >= num_frames_)
			continue;
		anim.num_frames = std::min(anim.num_frames, num_frames_ - anim.first_frame);
		if (anim.num_frames == 0)
			continue;
		if (!(anim.framerate > 0.f) || !std::isfinite(anim.framerate))
			anim.framerate = 25.f;
		if (anim.name.empty())
			anim.name = fmt::format("anim{}", a);

		anims_.push_back(anim);
	}

	// Some exporters write frames without an anim block; expose them anyway
	if (anims_.empty() && num_frames_ > 0)
	{
		Anim anim;
		anim.name       = "(all frames)";
		anim.num_frames = num_frames_;
		anim.framerate  = 25.f;
		anim.loop       = true;
		anims_.push_back(anim);
	}

	// --- Compute bind pose bounds ---
	bbox_min_ = { positions_[0], positions_[1], positions_[2] };
	bbox_max_ = bbox_min_;
	for (size_t i = 0; i + 2 < positions_.size(); i += 3)
	{
		bbox_min_.x = std::min(bbox_min_.x, positions_[i]);
		bbox_min_.y = std::min(bbox_min_.y, positions_[i + 1]);
		bbox_min_.z = std::min(bbox_min_.z, positions_[i + 2]);
		bbox_max_.x = std::max(bbox_max_.x, positions_[i]);
		bbox_max_.y = std::max(bbox_max_.y, positions_[i + 1]);
		bbox_max_.z = std::max(bbox_max_.z, positions_[i + 2]);
	}

	// Generate flat-ish normals if none were supplied
	if (normals_.empty())
	{
		normals_.assign(positions_.size(), 0.f);
		for (size_t t = 0; t + 2 < triangles_.size(); t += 3)
		{
			const float* p0 = &positions_[triangles_[t] * 3];
			const float* p1 = &positions_[triangles_[t + 1] * 3];
			const float* p2 = &positions_[triangles_[t + 2] * 3];
			float        e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
			float        e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
			float        n[3]  = { e1[1] * e2[2] - e1[2] * e2[1],
								   e1[2] * e2[0] - e1[0] * e2[2],
								   e1[0] * e2[1] - e1[1] * e2[0] };
			for (int k = 0; k < 3; ++k)
				for (int c = 0; c < 3; ++c)
					normals_[triangles_[t + k] * 3 + c] += n[c];
		}
		for (size_t i = 0; i + 2 < normals_.size(); i += 3)
		{
			float len = std::sqrt(
				normals_[i] * normals_[i] + normals_[i + 1] * normals_[i + 1] + normals_[i + 2] * normals_[i + 2]);
			if (len > 0.f)
			{
				normals_[i] /= len;
				normals_[i + 1] /= len;
				normals_[i + 2] /= len;
			}
		}
	}

	// Sanitise blend indices against the joint/pose count
	unsigned num_bones = num_poses_ > 0 ? num_poses_ : static_cast<unsigned>(joints_.size());
	for (auto& idx : blend_idx_)
		if (idx >= num_bones)
			idx = 0;

	valid_ = true;

	// Start in the bind pose
	animate(-1, 0.f);

	return true;
}

// -----------------------------------------------------------------------------
// Poses the model for [anim] at (fractional) [frame] within that animation.
// [anim] < 0 shows the bind pose
// -----------------------------------------------------------------------------
void IQMModel::animate(int anim, float frame, bool loop)
{
	if (!valid_)
		return;

	out_positions_.resize(positions_.size());
	out_normals_.resize(normals_.size());

	bool can_animate = anim >= 0 && anim < static_cast<int>(anims_.size()) && num_poses_ > 0
					   && !blend_idx_.empty() && !blend_wt_.empty();

	if (!can_animate)
	{
		// Bind pose: vertex data as-is
		out_positions_ = positions_;
		out_normals_   = normals_;

		out_joint_pos_.resize(joints_.size());
		for (size_t j = 0; j < joints_.size(); ++j)
			out_joint_pos_[j] = base_frame_[j].translation();
		return;
	}

	const auto& a = anims_[anim];

	// Work out the two frames to blend between
	float    f      = std::max(0.f, frame);
	unsigned fi     = static_cast<unsigned>(std::floor(f));
	float    lerp   = f - static_cast<float>(fi);
	unsigned frame1 = fi % a.num_frames;
	unsigned frame2 = frame1 + 1;
	if (frame2 >= a.num_frames)
		frame2 = loop ? 0 : frame1;
	if (!loop && fi >= a.num_frames)
	{
		frame1 = frame2 = a.num_frames - 1;
		lerp            = 0.f;
	}
	frame1 += a.first_frame;
	frame2 += a.first_frame;

	const Mat3x4* m1 = &frames_[size_t(frame1) * num_poses_];
	const Mat3x4* m2 = &frames_[size_t(frame2) * num_poses_];

	// Interpolate matrices and concatenate with parents
	out_frame_.resize(num_poses_);
	for (unsigned p = 0; p < num_poses_; ++p)
	{
		Mat3x4 mat = m1[p] * (1.f - lerp) + m2[p] * lerp;
		if (pose_parents_[p] >= 0)
			out_frame_[p] = out_frame_[pose_parents_[p]] * mat;
		else
			out_frame_[p] = mat;
	}

	// Skin vertices
	for (unsigned v = 0; v < num_vertexes_; ++v)
	{
		const uint8_t* idx = &blend_idx_[size_t(v) * 4];
		const uint8_t* wt  = &blend_wt_[size_t(v) * 4];

		Mat3x4 mat;
		int    total = wt[0] + wt[1] + wt[2] + wt[3];
		if (total == 0)
		{
			// Unweighted vertex - leave in bind pose
			mat = Mat3x4{};
		}
		else
		{
			float inv_total = 1.f / static_cast<float>(total);
			mat             = out_frame_[idx[0]] * (wt[0] * inv_total);
			for (int k = 1; k < 4 && wt[k]; ++k)
				mat = mat + out_frame_[idx[k]] * (wt[k] * inv_total);
		}

		Vec3 pos = mat.transform({ positions_[v * 3], positions_[v * 3 + 1], positions_[v * 3 + 2] });
		Vec3 nrm = mat.transformNormal({ normals_[v * 3], normals_[v * 3 + 1], normals_[v * 3 + 2] });
		float len = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
		if (len > 0.f)
		{
			nrm.x /= len;
			nrm.y /= len;
			nrm.z /= len;
		}

		out_positions_[v * 3]     = pos.x;
		out_positions_[v * 3 + 1] = pos.y;
		out_positions_[v * 3 + 2] = pos.z;
		out_normals_[v * 3]       = nrm.x;
		out_normals_[v * 3 + 1]   = nrm.y;
		out_normals_[v * 3 + 2]   = nrm.z;
	}

	// Joint positions: animated joint transform = out_frame * base_frame
	out_joint_pos_.resize(num_poses_);
	for (unsigned p = 0; p < num_poses_; ++p)
		out_joint_pos_[p] = (out_frame_[p] * base_frame_[p]).translation();
}
