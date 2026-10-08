#pragma once

#include "EntryPanel.h"

namespace slade
{
class IQMModel;
class ModelCanvas;
class SIconButton;

// -----------------------------------------------------------------------------
// ModelEntryPanel
//
// Entry panel for viewing 3D models (currently IQM). Shows the model in a 3D
// view with animation playback controls (play/pause/stop, frame stepping, a
// frame scrubber, animation selection and speed).
// -----------------------------------------------------------------------------
class ModelEntryPanel : public EntryPanel
{
public:
	ModelEntryPanel(wxWindow* parent);
	~ModelEntryPanel() override;

	string statusString() override;
	void   closeEntry() override;

protected:
	bool loadEntry(ArchiveEntry* entry) override;

private:
	unique_ptr<IQMModel> model_;
	ModelCanvas*         canvas_ = nullptr;

	// Playback state
	int   cur_anim_   = -1;
	float cur_frame_  = 0.f; // Fractional frame within the current animation
	bool  playing_    = false;
	long  last_tick_  = 0;
	float speed_      = 1.f;
	bool  updating_ui_ = false;

	// Controls
	wxChoice*     choice_anim_      = nullptr;
	SIconButton*  btn_play_         = nullptr;
	SIconButton*  btn_pause_        = nullptr;
	SIconButton*  btn_stop_         = nullptr;
	SIconButton*  btn_prev_         = nullptr;
	SIconButton*  btn_next_         = nullptr;
	wxSlider*     slider_frame_     = nullptr;
	wxStaticText* label_frame_      = nullptr;
	wxChoice*     choice_speed_     = nullptr;
	wxCheckBox*   cb_loop_          = nullptr;
	wxCheckBox*   cb_wireframe_     = nullptr;
	wxCheckBox*   cb_skeleton_      = nullptr;
	wxCheckBox*   cb_grid_          = nullptr;
	wxStaticText* label_info_       = nullptr;
	wxTimer       timer_;

	void   setAnimation(int index);
	void   setFrame(float frame);
	void   play();
	void   pause();
	void   stop();
	void   step(int frames);
	void   updateControls();
	void   updateFrameLabel();
	void   updateInfo();
	void   loadTextures(ArchiveEntry* entry);
	bool   loopEnabled() const;
	float  animLength() const;
	float  animFramerate() const;

	// Events
	void onTimer(wxTimerEvent& e);
	void onSliderFrame(wxCommandEvent& e);
	void onAnimChanged(wxCommandEvent& e);
	void onSpeedChanged(wxCommandEvent& e);
	void onKeyDown(wxKeyEvent& e);
};
} // namespace slade
