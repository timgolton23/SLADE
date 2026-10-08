
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ModelEntryPanel.cpp
// Description: ModelEntryPanel class. Views 3D model entries (IQM) with
//              animation playback controls
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
#include "ModelEntryPanel.h"
#include "App.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveDir.h"
#include "Archive/ArchiveEntry.h"
#include "Archive/EntryType/EntryType.h"
#include "General/UI.h"
#include "Graphics/IQMModel.h"
#include "MainEditor/MainEditor.h"
#include "UI/Canvas/ModelCanvas.h"
#include "UI/Controls/SIconButton.h"
#include "UI/WxUtils.h"
#include "Utility/StringUtils.h"
#include <cmath>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Bool, model_autoplay, true, CVar::Flag::Save)
CVAR(Bool, model_loop, true, CVar::Flag::Save)
CVAR(Bool, model_show_grid, true, CVar::Flag::Save)
CVAR(Bool, model_show_skeleton, false, CVar::Flag::Save)

namespace
{
// Number of slider steps per animation frame (allows smooth scrubbing)
constexpr int SLIDER_STEPS_PER_FRAME = 20;

const float SPEEDS[]      = { 0.1f, 0.25f, 0.5f, 1.f, 1.5f, 2.f, 4.f };
const char* SPEED_NAMES[] = { "0.1x", "0.25x", "0.5x", "1x", "1.5x", "2x", "4x" };
constexpr int SPEED_DEFAULT = 3;
} // namespace


// -----------------------------------------------------------------------------
//
// ModelEntryPanel Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// ModelEntryPanel class constructor
// -----------------------------------------------------------------------------
ModelEntryPanel::ModelEntryPanel(wxWindow* parent) :
	EntryPanel(parent, "model"),
	model_{ new IQMModel() },
	timer_{ this }
{
	// No editing - hide save/revert toolbar
	toolbar_->Show(false);

	// 3D view
	canvas_ = new ModelCanvas(this);
	canvas_->SetToolTip(
		wxS("Left-drag: rotate\nRight/middle-drag (or Shift+left-drag): pan\nWheel: zoom\nDouble-click: reset "
			"view\nSpace: play/pause, Left/Right: step frame"));
	sizer_main_->Add(canvas_, wxSizerFlags(1).Expand());

	// --- Row 1: animation selection + display options ---
	auto row_opts = new wxBoxSizer(wxHORIZONTAL);
	sizer_main_->Add(row_opts, wxSizerFlags().Expand().Border(wxTOP, ui::pad()));

	row_opts->Add(new wxStaticText(this, -1, wxS("Animation:")), wxSizerFlags().CenterVertical());
	choice_anim_ = new wxChoice(this, -1);
	choice_anim_->SetMinSize({ ui::scalePx(200), -1 });
	row_opts->Add(choice_anim_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));

	row_opts->Add(new wxStaticText(this, -1, wxS("Speed:")), wxSizerFlags().CenterVertical().Border(wxLEFT, ui::padLarge()));
	choice_speed_ = new wxChoice(this, -1);
	for (auto name : SPEED_NAMES)
		choice_speed_->Append(wxString::FromUTF8(name));
	choice_speed_->SetSelection(SPEED_DEFAULT);
	row_opts->Add(choice_speed_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));

	cb_loop_ = new wxCheckBox(this, -1, wxS("Loop"));
	cb_loop_->SetValue(model_loop);
	row_opts->Add(cb_loop_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::padLarge()));

	row_opts->AddStretchSpacer();

	cb_wireframe_ = new wxCheckBox(this, -1, wxS("Wireframe"));
	row_opts->Add(cb_wireframe_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));
	cb_skeleton_ = new wxCheckBox(this, -1, wxS("Skeleton"));
	cb_skeleton_->SetValue(model_show_skeleton);
	row_opts->Add(cb_skeleton_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));
	cb_grid_ = new wxCheckBox(this, -1, wxS("Grid"));
	cb_grid_->SetValue(model_show_grid);
	row_opts->Add(cb_grid_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));

	// --- Row 2: transport + scrubber ---
	auto row_play = new wxBoxSizer(wxHORIZONTAL);
	sizer_main_->Add(row_play, wxSizerFlags().Expand().Border(wxTOP, ui::pad()));

	btn_play_  = new SIconButton(this, "play", "Play", 24);
	btn_pause_ = new SIconButton(this, "pause", "Pause", 24);
	btn_stop_  = new SIconButton(this, "stop", "Stop (return to first frame)", 24);
	btn_prev_  = new SIconButton(this, "prev", "Previous frame", 24);
	btn_next_  = new SIconButton(this, "next", "Next frame", 24);
	row_play->Add(btn_play_, wxSizerFlags().CenterVertical());
	row_play->Add(btn_pause_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::px(ui::Size::PadMinimum)));
	row_play->Add(btn_stop_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::px(ui::Size::PadMinimum)));
	row_play->Add(btn_prev_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));
	row_play->Add(btn_next_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::px(ui::Size::PadMinimum)));

	slider_frame_ = new wxSlider(this, -1, 0, 0, 1);
	row_play->Add(slider_frame_, wxSizerFlags(1).CenterVertical().Border(wxLEFT, ui::pad()));

	label_frame_ = new wxStaticText(this, -1, wxS("Frame 0 / 0"));
	label_frame_->SetMinSize({ ui::scalePx(190), -1 });
	row_play->Add(label_frame_, wxSizerFlags().CenterVertical().Border(wxLEFT, ui::pad()));

	// --- Row 3: model info ---
	label_info_ = new wxStaticText(this, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	sizer_main_->Add(label_info_, wxSizerFlags().Expand().Border(wxTOP, ui::pad()));

	// Bind events
	btn_play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { play(); });
	btn_pause_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { pause(); });
	btn_stop_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { stop(); });
	btn_prev_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { step(-1); });
	btn_next_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { step(1); });
	slider_frame_->Bind(wxEVT_SLIDER, &ModelEntryPanel::onSliderFrame, this);
	choice_anim_->Bind(wxEVT_CHOICE, &ModelEntryPanel::onAnimChanged, this);
	choice_speed_->Bind(wxEVT_CHOICE, &ModelEntryPanel::onSpeedChanged, this);
	cb_loop_->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&)
		{
			model_loop = cb_loop_->GetValue();
			setFrame(cur_frame_); // Re-clamp/re-pose
			updateControls();
		});
	cb_wireframe_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { canvas_->setWireframe(cb_wireframe_->GetValue()); });
	cb_skeleton_->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&)
		{
			model_show_skeleton = cb_skeleton_->GetValue();
			canvas_->setShowSkeleton(model_show_skeleton);
		});
	cb_grid_->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&)
		{
			model_show_grid = cb_grid_->GetValue();
			canvas_->setShowGrid(model_show_grid);
		});
	canvas_->Bind(wxEVT_KEY_DOWN, &ModelEntryPanel::onKeyDown, this);
	Bind(wxEVT_TIMER, &ModelEntryPanel::onTimer, this);

	canvas_->setShowSkeleton(model_show_skeleton);
	canvas_->setShowGrid(model_show_grid);

	updateControls();
	wxWindowBase::Layout();
}

// -----------------------------------------------------------------------------
// ModelEntryPanel class destructor
// -----------------------------------------------------------------------------
ModelEntryPanel::~ModelEntryPanel()
{
	timer_.Stop();
	canvas_->setModel(nullptr);
}

// -----------------------------------------------------------------------------
// Loads [entry] into the panel
// -----------------------------------------------------------------------------
bool ModelEntryPanel::loadEntry(ArchiveEntry* entry)
{
	pause();
	canvas_->setModel(nullptr);
	canvas_->clearTextures();
	choice_anim_->Clear();
	cur_anim_  = -1;
	cur_frame_ = 0.f;

	if (!model_->open(entry->data()))
	{
		global::error = model_->error();
		label_info_->SetLabel(WX_FMT("Unable to load model: {}", model_->error()));
		updateControls();
		canvas_->Refresh();
		return false;
	}

	// Populate animations
	choice_anim_->Append(wxS("(Bind pose)"));
	for (auto& anim : model_->anims())
		choice_anim_->Append(
			WX_FMT("{} ({} frames @ {:g} fps{})", anim.name, anim.num_frames, anim.framerate, anim.loop ? ", loop" : ""));

	canvas_->setModel(model_.get());
	loadTextures(entry);
	updateInfo();

	// Start on the first animation if there is one
	if (!model_->anims().empty())
	{
		setAnimation(0);
		if (model_autoplay)
			play();
	}
	else
		setAnimation(-1);

	return true;
}

// -----------------------------------------------------------------------------
// Called when the entry is closed (eg. another entry was selected). Stops
// playback so the timer doesn't keep animating a hidden panel
// -----------------------------------------------------------------------------
void ModelEntryPanel::closeEntry()
{
	pause();
	EntryPanel::closeEntry();
}

// -----------------------------------------------------------------------------
// Returns a string with extended info for the status bar
// -----------------------------------------------------------------------------
string ModelEntryPanel::statusString()
{
	if (!model_->isValid())
		return {};

	if (cur_anim_ < 0)
		return "Bind pose";

	auto& anim = model_->anims()[cur_anim_];
	return fmt::format("{} - frame {}/{}", anim.name, static_cast<int>(cur_frame_) + 1, anim.num_frames);
}

// -----------------------------------------------------------------------------
// Finds an image entry for each mesh's material and passes them to the canvas
// -----------------------------------------------------------------------------
void ModelEntryPanel::loadTextures(ArchiveEntry* entry)
{
	auto archive = entry->parent();
	if (!archive)
		return;

	auto is_image = [](ArchiveEntry* e)
	{
		if (!e)
			return false;
		if (e->type() == EntryType::unknownType())
			EntryType::detectEntryType(*e);
		return e->type()->editor() == "gfx";
	};

	string model_dir = entry->path(); // eg. "/models/imp/"

	vector<ArchiveEntry*> textures;
	for (auto& mesh : model_->meshes())
	{
		ArchiveEntry* found = nullptr;
		string        mat   = mesh.material;
		std::replace(mat.begin(), mat.end(), '\\', '/');

		if (!mat.empty())
		{
			// 1. Relative to the model's directory (with or without extension)
			auto dir = archive->dirAtPath(model_dir);
			if (dir)
			{
				auto e = archive->entryAtPath(model_dir + mat);
				if (!is_image(e))
					e = dir->entry(strutil::Path::fileNameOf(mat, false), true);
				if (is_image(e))
					found = e;
			}

			// 2. As a full path from the archive root
			if (!found)
			{
				auto e = archive->entryAtPath(mat);
				if (is_image(e))
					found = e;
			}

			// 3. Anywhere in the archive with a matching name (prefer images in the model's dir tree)
			if (!found)
			{
				Archive::SearchOptions opt;
				opt.match_name     = strutil::Path::fileNameOf(mat, false);
				opt.ignore_ext     = true;
				opt.search_subdirs = true;
				for (auto e : archive->findAll(opt))
				{
					if (!is_image(e))
						continue;
					if (!found || strutil::startsWith(e->path(), model_dir))
						found = e;
					if (strutil::startsWith(e->path(), model_dir))
						break;
				}
			}
		}

		textures.push_back(found);
	}

	canvas_->setTextures(textures, maineditor::currentPalette(entry));
}

// -----------------------------------------------------------------------------
// Sets the current animation ([index] < 0 = bind pose)
// -----------------------------------------------------------------------------
void ModelEntryPanel::setAnimation(int index)
{
	if (index >= static_cast<int>(model_->anims().size()))
		index = -1;

	cur_anim_ = index;

	updating_ui_ = true;
	choice_anim_->SetSelection(index + 1);
	updating_ui_ = false;

	if (index < 0)
		pause();

	setFrame(0.f);
	updateControls();
}

// -----------------------------------------------------------------------------
// Sets the current (fractional) frame, poses the model and updates the UI
// -----------------------------------------------------------------------------
void ModelEntryPanel::setFrame(float frame)
{
	if (!model_->isValid())
		return;

	if (cur_anim_ >= 0)
	{
		float len = animLength();
		if (loopEnabled() && len > 0.f)
		{
			frame = std::fmod(frame, len);
			if (frame < 0.f)
				frame += len;
		}
		else
			frame = std::clamp(frame, 0.f, len);
	}
	else
		frame = 0.f;

	cur_frame_ = frame;
	model_->animate(cur_anim_, cur_frame_, loopEnabled());

	// Update slider
	updating_ui_ = true;
	slider_frame_->SetValue(static_cast<int>(std::lround(cur_frame_ * SLIDER_STEPS_PER_FRAME)));
	updating_ui_ = false;

	updateFrameLabel();
	canvas_->Refresh();
	updateStatus();
}

// -----------------------------------------------------------------------------
// Starts animation playback
// -----------------------------------------------------------------------------
void ModelEntryPanel::play()
{
	if (cur_anim_ < 0 || !model_->isValid())
		return;

	// Restart from the beginning if we're sitting at the end of a non-looping anim
	if (!loopEnabled() && cur_frame_ >= animLength())
		setFrame(0.f);

	playing_   = true;
	last_tick_ = app::runTimer();
	timer_.Start(15);
	updateControls();
}

// -----------------------------------------------------------------------------
// Pauses animation playback
// -----------------------------------------------------------------------------
void ModelEntryPanel::pause()
{
	playing_ = false;
	timer_.Stop();
	updateControls();
}

// -----------------------------------------------------------------------------
// Stops playback and returns to the first frame
// -----------------------------------------------------------------------------
void ModelEntryPanel::stop()
{
	pause();
	setFrame(0.f);
}

// -----------------------------------------------------------------------------
// Pauses and steps [frames] whole frames forward/back
// -----------------------------------------------------------------------------
void ModelEntryPanel::step(int frames)
{
	if (cur_anim_ < 0)
		return;

	pause();
	float target = frames > 0 ? std::floor(cur_frame_ + 0.001f) + frames : std::ceil(cur_frame_ - 0.001f) + frames;
	if (!loopEnabled())
		target = std::clamp(target, 0.f, animLength());
	setFrame(target);
}

// -----------------------------------------------------------------------------
// Returns true if the current animation should loop
// -----------------------------------------------------------------------------
bool ModelEntryPanel::loopEnabled() const
{
	return cb_loop_->GetValue();
}

// -----------------------------------------------------------------------------
// Returns the playable length of the current animation in frames.
// Looping anims run [0, n) with the last frame blending back to the first;
// non-looping anims run [0, n-1]
// -----------------------------------------------------------------------------
float ModelEntryPanel::animLength() const
{
	if (cur_anim_ < 0 || cur_anim_ >= static_cast<int>(model_->anims().size()))
		return 0.f;

	auto n = static_cast<float>(model_->anims()[cur_anim_].num_frames);
	if (n <= 1.f)
		return 0.f;
	return loopEnabled() ? n : n - 1.f;
}

// -----------------------------------------------------------------------------
// Returns the framerate of the current animation
// -----------------------------------------------------------------------------
float ModelEntryPanel::animFramerate() const
{
	if (cur_anim_ < 0 || cur_anim_ >= static_cast<int>(model_->anims().size()))
		return 25.f;
	return model_->anims()[cur_anim_].framerate;
}

// -----------------------------------------------------------------------------
// Enables/disables controls depending on the current state
// -----------------------------------------------------------------------------
void ModelEntryPanel::updateControls()
{
	bool valid    = model_->isValid();
	bool has_anim = valid && cur_anim_ >= 0;
	bool can_move = has_anim && animLength() > 0.f;

	choice_anim_->Enable(valid && choice_anim_->GetCount() > 1);
	choice_speed_->Enable(has_anim);
	cb_loop_->Enable(has_anim);
	cb_skeleton_->Enable(valid && model_->hasSkeleton());
	btn_play_->Enable(can_move && !playing_);
	btn_pause_->Enable(can_move && playing_);
	btn_stop_->Enable(can_move);
	btn_prev_->Enable(can_move);
	btn_next_->Enable(can_move);

	updating_ui_ = true;
	int max = std::max(1, static_cast<int>(std::lround(animLength() * SLIDER_STEPS_PER_FRAME)));
	slider_frame_->SetRange(0, max);
	slider_frame_->SetPageSize(SLIDER_STEPS_PER_FRAME);
	slider_frame_->SetLineSize(SLIDER_STEPS_PER_FRAME);
	slider_frame_->SetValue(std::min(max, static_cast<int>(std::lround(cur_frame_ * SLIDER_STEPS_PER_FRAME))));
	slider_frame_->Enable(can_move);
	updating_ui_ = false;

	updateFrameLabel();
}

// -----------------------------------------------------------------------------
// Updates the frame/time label
// -----------------------------------------------------------------------------
void ModelEntryPanel::updateFrameLabel()
{
	if (!model_->isValid() || cur_anim_ < 0)
	{
		label_frame_->SetLabel(wxS("Bind pose"));
		return;
	}

	auto& anim  = model_->anims()[cur_anim_];
	float fps   = animFramerate();
	float total = static_cast<float>(anim.num_frames) / fps;
	label_frame_->SetLabel(WX_FMT(
		"Frame {} / {}   {:.2f}s / {:.2f}s",
		static_cast<int>(std::floor(cur_frame_)) + 1,
		anim.num_frames,
		cur_frame_ / fps,
		total));
}

// -----------------------------------------------------------------------------
// Updates the model info label
// -----------------------------------------------------------------------------
void ModelEntryPanel::updateInfo()
{
	if (!model_->isValid())
	{
		label_info_->SetLabel(wxEmptyString);
		return;
	}

	vector<string> materials;
	for (auto& mesh : model_->meshes())
		if (!mesh.material.empty()
			&& std::find(materials.begin(), materials.end(), mesh.material) == materials.end())
			materials.push_back(mesh.material);

	string mats;
	for (size_t i = 0; i < materials.size(); ++i)
		mats += (i ? ", " : "") + materials[i];

	label_info_->SetLabel(WX_FMT(
		"IQM: {} mesh(es), {} vertices, {} triangles, {} joints, {} animation(s), {} frames{}{}",
		model_->meshes().size(),
		model_->numVertices(),
		model_->triangles().size() / 3,
		model_->joints().size(),
		model_->anims().size(),
		model_->numFrames(),
		mats.empty() ? "" : " | Materials: ",
		mats));
	label_info_->SetToolTip(label_info_->GetLabel());
}


// -----------------------------------------------------------------------------
//
// ModelEntryPanel Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when the playback timer ticks
// -----------------------------------------------------------------------------
void ModelEntryPanel::onTimer(wxTimerEvent& e)
{
	if (!playing_)
		return;

	long  now     = app::runTimer();
	float elapsed = static_cast<float>(now - last_tick_) / 1000.f;
	last_tick_    = now;

	float next = cur_frame_ + elapsed * animFramerate() * speed_;

	// Reached the end of a non-looping animation
	if (!loopEnabled() && next >= animLength())
	{
		setFrame(animLength());
		pause();
		return;
	}

	setFrame(next);
}

// -----------------------------------------------------------------------------
// Called when the frame slider is moved (scrubbing)
// -----------------------------------------------------------------------------
void ModelEntryPanel::onSliderFrame(wxCommandEvent& e)
{
	if (updating_ui_)
		return;

	// Keep playing from the new position if currently playing
	last_tick_ = app::runTimer();
	setFrame(static_cast<float>(slider_frame_->GetValue()) / SLIDER_STEPS_PER_FRAME);
}

// -----------------------------------------------------------------------------
// Called when the animation selection is changed
// -----------------------------------------------------------------------------
void ModelEntryPanel::onAnimChanged(wxCommandEvent& e)
{
	if (updating_ui_)
		return;

	bool was_playing = playing_;
	setAnimation(choice_anim_->GetSelection() - 1);
	if (was_playing || (cur_anim_ >= 0 && model_autoplay))
		play();
}

// -----------------------------------------------------------------------------
// Called when the playback speed is changed
// -----------------------------------------------------------------------------
void ModelEntryPanel::onSpeedChanged(wxCommandEvent& e)
{
	int sel = choice_speed_->GetSelection();
	if (sel >= 0 && sel < static_cast<int>(std::size(SPEEDS)))
		speed_ = SPEEDS[sel];
}

// -----------------------------------------------------------------------------
// Called when a key is pressed in the 3D view
// -----------------------------------------------------------------------------
void ModelEntryPanel::onKeyDown(wxKeyEvent& e)
{
	switch (e.GetKeyCode())
	{
	case WXK_SPACE:
		if (playing_)
			pause();
		else
			play();
		break;
	case WXK_LEFT:  step(-1); break;
	case WXK_RIGHT: step(1); break;
	case WXK_HOME:  stop(); break;
	case WXK_UP:
		if (choice_anim_->IsEnabled())
		{
			setAnimation(std::max(-1, cur_anim_ - 1));
			if (cur_anim_ >= 0 && model_autoplay)
				play();
		}
		break;
	case WXK_DOWN:
		if (choice_anim_->IsEnabled() && cur_anim_ + 1 < static_cast<int>(model_->anims().size()))
		{
			setAnimation(cur_anim_ + 1);
			if (model_autoplay)
				play();
		}
		break;
	default: e.Skip(); break;
	}
}
