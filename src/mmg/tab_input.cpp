/*
  mkvmerge GUI -- utility for splicing together matroska files
      from component media subtypes

  tab_input.cpp

  Written by Moritz Bunkus <moritz@bunkus.org>
  Parts of this code were written by Florian Wager <flo.wagner@gmx.de>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version $Id$
    \brief "input" tab
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#include <errno.h>
#include <string.h>

#include "wx/wxprec.h"

#include "wx/wx.h"
#include "wx/notebook.h"
#include "wx/listctrl.h"
#include "wx/statline.h"

#include "common.h"
#include "extern_data.h"
#include "iso639.h"
#include "mmg.h"
#include "mmg_dialog.h"
#include "tab_input.h"

using namespace std;

wxArrayString sorted_iso_codes;
wxArrayString sorted_charsets;
bool title_was_present = false;

tab_input::tab_input(wxWindow *parent):
  wxPanel(parent, -1, wxDefaultPosition, wxSize(100, 400),
          wxTAB_TRAVERSAL) {
  uint32_t i, j;
  bool found;
  wxString language;
  wxArrayString popular_languages;

  new wxStaticText(this, wxID_STATIC, wxS("Input files:"), wxPoint(5, 5),
                   wxDefaultSize, 0);
  lb_input_files =
    new wxListBox(this, ID_LB_INPUTFILES, wxPoint(5, 24), wxSize(360, 60), 0);

  b_add_file =
    new wxButton(this, ID_B_ADDFILE, wxS("add"), wxPoint(375, 24),
                 wxSize(50, -1));
  b_remove_file =
    new wxButton(this, ID_B_REMOVEFILE, wxS("remove"), wxPoint(375, 56),
                 wxSize(50, -1));
  b_remove_file->Enable(false);
  b_file_up =
    new wxButton(this, ID_B_INPUTUP, wxS("up"), wxPoint(435, 24),
                 wxSize(50, -1));
  b_file_up->Enable(false);
  b_file_down =
    new wxButton(this, ID_B_INPUTDOWN, wxS("down"), wxPoint(435, 56),
                 wxSize(50, -1));
  b_file_down->Enable(false);

  new wxStaticText(this, wxID_STATIC, wxS("File options:"), wxPoint(5, 90),
                   wxDefaultSize, 0);
  cb_no_chapters =
    new wxCheckBox(this, ID_CB_NOCHAPTERS, wxS("No chapters"),
                   wxPoint(90, 90 + YOFF2), wxDefaultSize, 0);
  cb_no_chapters->SetValue(false);
  cb_no_chapters->SetToolTip(wxS("Do not copy chapters from this file. Only "
                                 "applies to Matroska files."));
  cb_no_chapters->Enable(false);
  cb_no_attachments =
    new wxCheckBox(this, ID_CB_NOATTACHMENTS, wxS("No attachments"),
                   wxPoint(195, 90 + YOFF2), wxDefaultSize, 0);
  cb_no_attachments->SetValue(false);
  cb_no_attachments->SetToolTip(wxS("Do not copy attachments from this file. "
                                    "Only applies to Matroska files."));
  cb_no_attachments->Enable(false);
  cb_no_tags =
    new wxCheckBox(this, ID_CB_NOTAGS, wxS("No tags"),
                   wxPoint(315, 90 + YOFF2), wxDefaultSize, 0);
  cb_no_tags->SetValue(false);
  cb_no_tags->SetToolTip(wxS("Do not copy tags from this file. Only "
                             "applies to Matroska files."));
  cb_no_tags->Enable(false);

  new wxStaticText(this, wxID_STATIC, wxS("Tracks:"), wxPoint(5, 110),
                   wxDefaultSize, 0);
  clb_tracks =
    new wxCheckListBox(this, ID_CLB_TRACKS, wxPoint(5, 130), wxSize(420, 100),
                       0, NULL, 0);
  clb_tracks->Enable(false);
  b_track_up =
    new wxButton(this, ID_B_TRACKUP, wxS("up"), wxPoint(435, 130),
                 wxSize(50, -1));
  b_track_up->Enable(false);
  b_track_down =
    new wxButton(this, ID_B_TRACKDOWN, wxS("down"), wxPoint(435, 162),
                 wxSize(50, -1));
  b_track_down->Enable(false);
  new wxStaticText(this, wxID_STATIC, wxS("Track options:"), wxPoint(5, 235),
                   wxDefaultSize, 0);
  new wxStaticText(this, wxID_STATIC, wxS("Language:"), wxPoint(5, 255),
                   wxDefaultSize, 0);

  if (sorted_iso_codes.Count() == 0) {
    for (i = 0; iso639_languages[i].iso639_2_code != NULL; i++) {
      if (iso639_languages[i].english_name == NULL)
        language = iso639_languages[i].iso639_2_code;
      else
        language.Printf("%s (%s)", iso639_languages[i].iso639_2_code,
                        iso639_languages[i].english_name);
      sorted_iso_codes.Add(language);
    }
    sorted_iso_codes.Sort();

    for (i = 0; iso639_languages[i].iso639_2_code != NULL; i++) {
      if (!is_popular_language_code(iso639_languages[i].iso639_2_code))
        continue;
      for (j = 0, found = false; j < popular_languages.Count(); j++)
        if (extract_language_code(popular_languages[j]) ==
            iso639_languages[i].iso639_2_code) {
          found = true;
          break;
        }
      if (!found) {
        language.Printf("%s (%s)", iso639_languages[i].iso639_2_code,
                        iso639_languages[i].english_name);
        popular_languages.Add(language);
      }
    }
    popular_languages.Sort();

    sorted_iso_codes.Insert("---common---", 0);
    for (i = 0; i < popular_languages.Count(); i++)
      sorted_iso_codes.Insert(popular_languages[i], i + 1);
    sorted_iso_codes.Insert("---all---", i + 1);
  }

  cob_language =
    new wxComboBox(this, ID_CB_LANGUAGE, wxS(""), wxPoint(90, 255 + YOFF),
                   wxSize(130, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
  cob_language->SetToolTip(wxS("Language for this track. Select one of the "
                               "ISO639-2 language codes."));
  cob_language->Append(wxS("none"));
  for (i = 0; i < sorted_iso_codes.Count(); i++)
    cob_language->Append(sorted_iso_codes[i]);

  new wxStaticText(this, wxID_STATIC, wxS("Delay (in ms):"),
                   wxPoint(255, 255), wxDefaultSize, 0);
  tc_delay =
    new wxTextCtrl(this, ID_TC_DELAY, wxS(""), wxPoint(355, 255 + YOFF),
                   wxSize(130, -1), 0);
  tc_delay->SetToolTip(wxS("Delay this track by a couple of ms. Can be "
                           "negative. Only applies to audio and subtitle "
                           "tracks. Some audio formats cannot be delayed at "
                           "the moment."));
  new wxStaticText(this, wxID_STATIC, wxS("Track name:"), wxPoint(5, 280),
                   wxDefaultSize, 0);
  tc_track_name =
    new wxTextCtrl(this, ID_TC_TRACKNAME, wxS(""), wxPoint(90, 280 + YOFF),
                   wxSize(130, -1), 0);
  tc_track_name->SetToolTip(wxS("Name for this track, e.g. \"director's "
                                "comments\"."));
  new wxStaticText(this, wxID_STATIC, wxS("Stretch by:"), wxPoint(255, 280),
                   wxDefaultSize, 0);
  tc_stretch =
    new wxTextCtrl(this, ID_TC_STRETCH, wxS(""), wxPoint(355, 280 + YOFF),
                   wxSize(130, -1), 0);
  tc_stretch->SetToolTip(wxS("Stretch the audio or subtitle track by a "
                             "factor. This should be a positive floating "
                             "point number. Not all formats can be stretched "
                             "at the moment."));
  new wxStaticText(this, wxID_STATIC, wxS("Cues:"), wxPoint(5, 305),
                   wxDefaultSize, 0);

  cob_cues =
    new wxComboBox(this, ID_CB_CUES, wxS(""), wxPoint(90, 305 + YOFF),
                   wxSize(130, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
  cob_cues->SetToolTip(wxS("Selects for which blocks mkvmerge will produce "
                           "index entries ( = cue entries). \"default\" is a "
                           "good choice for almost all situations."));
  cob_cues->Append(wxS("default"));
  cob_cues->Append(wxS("only for I frames"));
  cob_cues->Append(wxS("for all frames"));
  cob_cues->Append(wxS("none"));
  new wxStaticText(this, wxID_STATIC, wxS("Subtitle charset:"),
                   wxPoint(255, 305 + YOFF), wxDefaultSize, 0);
  cob_sub_charset =
    new wxComboBox(this, ID_CB_SUBTITLECHARSET, wxS(""),
                   wxPoint(355, 305 + YOFF), wxSize(130, -1), 0, NULL,
                   wxCB_DROPDOWN | wxCB_READONLY);
  cob_sub_charset->SetToolTip(wxS("Selects the character set a subtitle file "
                                  "was written with. Only needed for non-UTF "
                                  "files that mkvmerge does not detect "
                                  "correctly."));
  cob_sub_charset->Append(wxS("default"));

  if (sorted_charsets.Count() == 0) {
    for (i = 0; sub_charsets[i] != NULL; i++)
      sorted_charsets.Add(sub_charsets[i]);
    sorted_charsets.Sort();
  }

  for (i = 0; i < sorted_charsets.Count(); i++)
    cob_sub_charset->Append(sorted_charsets[i]);

  new wxStaticText(this, -1, wxS("FourCC:"), wxPoint(5, 330),
                   wxDefaultSize, 0);
  cob_fourcc =
    new wxComboBox(this, ID_CB_FOURCC, wxS(""), wxPoint(90, 330 + YOFF),
                   wxSize(130, -1), 0, NULL, wxCB_DROPDOWN);
  cob_fourcc->Append(wxS(""));
  cob_fourcc->Append(wxS("DIVX"));
  cob_fourcc->Append(wxS("DIV3"));
  cob_fourcc->Append(wxS("DX50"));
  cob_fourcc->Append(wxS("XVID"));
  cob_fourcc->SetToolTip(wxS("Forces the FourCC of the video track to this "
                             "value. Note that this only works for video "
                             "tracks that use the AVI compatibility mode "
                             "or for QuickTime video tracks. This option "
                             "CANNOT be used to change Matroska's CodecID."));

  new wxStaticText(this, -1, wxS("Compression:"), wxPoint(255, 330),
                   wxDefaultSize, 0);
  cob_compression =
    new wxComboBox(this, ID_CB_COMPRESSION, wxS(""), wxPoint(355, 330 + YOFF),
                   wxSize(130, -1), 0, NULL, wxCB_DROPDOWN | wxCB_READONLY);
  cob_compression->Append(wxS(""));
  cob_compression->Append(wxS("none"));
  cob_compression->Append(wxS("zlib"));
  if (capabilities[wxS("BZ2")] == wxS("true"))
    cob_compression->Append(wxS("bz2"));
  if (capabilities[wxS("LZO")] == wxS("true"))
    cob_compression->Append(wxS("lzo"));
  cob_compression->SetToolTip(wxS("Sets the compression used for VobSub "
                                  "subtitles. If nothing is chosen then the "
                                  "VobSubs will be automatically compressed "
                                  "with zlib. 'none' results is files that "
                                  "are a lot larger."));

  rb_aspect_ratio =
    new wxRadioButton(this, ID_RB_ASPECTRATIO, wxS("Aspect ratio:"),
                      wxPoint(5, 355), wxDefaultSize, wxRB_GROUP);
  rb_aspect_ratio->SetValue(true);
  cob_aspect_ratio =
    new wxComboBox(this, ID_CB_ASPECTRATIO, wxS(""), wxPoint(110, 355 + YOFF),
                   wxSize(110, -1), 0, NULL, wxCB_DROPDOWN);
  cob_aspect_ratio->Append(wxS(""));
  cob_aspect_ratio->Append(wxS("4/3"));
  cob_aspect_ratio->Append(wxS("1.66"));
  cob_aspect_ratio->Append(wxS("16/9"));
  cob_aspect_ratio->Append(wxS("1.85"));
  cob_aspect_ratio->Append(wxS("2.00"));
  cob_aspect_ratio->Append(wxS("2.21"));
  cob_aspect_ratio->Append(wxS("2.35"));
  cob_aspect_ratio->SetToolTip(wxS("Sets the display aspect ratio of the "
                                   "track. The format can be either 'a/b' in "
                                   "which case both numbers must be integer "
                                   "(e.g. 16/9) or just a single floting "
                                   "point number 'f' (e.g. 2.35)."));

  new wxStaticText(this, wxID_STATIC, wxS("or"), wxPoint(220, 360),
                   wxSize(35, -1), wxALIGN_CENTER);

  rb_display_dimensions =
    new wxRadioButton(this, ID_RB_DISPLAYDIMENSIONS,
                      wxS("Display width/height:"), wxPoint(255, 355),
                      wxDefaultSize);
  rb_display_dimensions->SetValue(false);
  tc_display_width =
    new wxTextCtrl(this, ID_TC_DISPLAYWIDTH, "", wxPoint(405, 355 + YOFF),
                   wxSize(35, -1));
  tc_display_width->SetToolTip(wxS("Sets the display width of the track."
                                   "The height must be set as well, or this "
                                   "field will be ignored."));
  new wxStaticText(this, wxID_STATIC, "x", wxPoint(440, 360),
                   wxSize(10, -1), wxALIGN_CENTER);
  tc_display_height =
    new wxTextCtrl(this, ID_TC_DISPLAYHEIGHT, "", wxPoint(450, 355 + YOFF),
                   wxSize(35, -1));
  tc_display_height->SetToolTip(wxS("Sets the display height of the track."
                                    "The width must be set as well, or this "
                                    "field will be ignored."));

  cb_default =
    new wxCheckBox(this, ID_CB_MAKEDEFAULT, wxS("Make default track"),
                   wxPoint(5, 380), wxSize(200, -1), 0);
  cb_default->SetValue(false);
  cb_default->SetToolTip(wxS("Make this track the default track for its type "
                             "(audio, video, subtitles). Players should "
                             "prefer tracks with the default track flag "
                             "set."));
  cb_aac_is_sbr =
    new wxCheckBox(this, ID_CB_AACISSBR, wxS("AAC is SBR/HE-AAC/AAC+"),
                   wxPoint(255, 380), wxSize(200, -1), 0);
  cb_aac_is_sbr->SetValue(false);
  cb_aac_is_sbr->SetToolTip(wxS("This track contains SBR AAC/HE-AAC/AAC+ data."
                                " Only needed for AAC input files, because SBR"
                                " AAC cannot be detected automatically for "
                                "these files. Not needed for AAC tracks read "
                                "from MP4 or Matroska files."));

  new wxStaticText(this, wxID_STATIC, wxS("Tags:"), wxPoint(5, 405),
                   wxDefaultSize, 0);
  tc_tags =
    new wxTextCtrl(this, ID_TC_TAGS, wxS(""), wxPoint(90, 405 + YOFF),
                   wxSize(280, -1));
  b_browse_tags =
    new wxButton(this, ID_B_BROWSETAGS, wxS("Browse"),
                 wxPoint(390, 405 + YOFF), wxDefaultSize, 0);

  new wxStaticText(this, wxID_STATIC, wxS("Timecodes:"), wxPoint(5, 430),
                   wxDefaultSize, 0);
  tc_timecodes =
    new wxTextCtrl(this, ID_TC_TIMECODES, wxS(""), wxPoint(90, 430 + YOFF),
                   wxSize(280, -1));
  tc_timecodes->SetToolTip(wxS("mkvmerge can read and use timecodes from an "
                               "external text file. This feature is a very "
                               "advanced feature. Almost all users should "
                               "leave this entry empty."));
  b_browse_timecodes =
    new wxButton(this, ID_B_BROWSE_TIMECODES, wxS("Browse"),
                 wxPoint(390, 430 + YOFF), wxDefaultSize, 0);
  b_browse_timecodes->SetToolTip(wxS("mkvmerge can read and use timecodes "
                                     "from an external text file. This "
                                     "feature is a very advanced feature. "
                                     "Almost all users should leave this "
                                     "entry empty."));

  no_track_mode();
  selected_file = -1;
  selected_track = -1;

  value_copy_timer.SetOwner(this, ID_T_INPUTVALUES);
  value_copy_timer.Start(333);
}

void
tab_input::no_track_mode() {
  cob_language->Enable(false);
  tc_delay->Enable(false);
  tc_track_name->Enable(false);
  tc_stretch->Enable(false);
  cob_cues->Enable(false);
  cob_sub_charset->Enable(false);
  cb_default->Enable(false);
  cb_aac_is_sbr->Enable(false);
  tc_tags->Enable(false);
  b_browse_tags->Enable(false);
  cob_aspect_ratio->Enable(false);
  tc_display_width->Enable(false);
  tc_display_height->Enable(false);
  rb_aspect_ratio->Enable(false);
  rb_display_dimensions->Enable(false);
  cob_fourcc->Enable(false);
  cob_compression->Enable(false);
  tc_timecodes->Enable(false);
  b_browse_timecodes->Enable(false);
}

void
tab_input::audio_track_mode(wxString ctype) {
  wxString lctype;

  lctype = ctype.Lower();
  cob_language->Enable(true);
  tc_delay->Enable(true);
  tc_track_name->Enable(true);
  tc_stretch->Enable(true);
  cob_cues->Enable(true);
  cob_sub_charset->Enable(false);
  cb_default->Enable(true);
  cb_aac_is_sbr->Enable((lctype.Find("aac") >= 0) ||
                        (lctype.Find("mp4a") >= 0));
  tc_tags->Enable(true);
  b_browse_tags->Enable(true);
  cob_aspect_ratio->Enable(false);
  tc_display_width->Enable(false);
  tc_display_height->Enable(false);
  rb_aspect_ratio->Enable(false);
  rb_display_dimensions->Enable(false);
  cob_fourcc->Enable(false);
  cob_compression->Enable(false);
  tc_timecodes->Enable(true);
  b_browse_timecodes->Enable(true);
}

void
tab_input::video_track_mode(wxString) {
  cob_language->Enable(true);
  tc_delay->Enable(false);
  tc_track_name->Enable(true);
  tc_stretch->Enable(false);
  cob_cues->Enable(true);
  cob_sub_charset->Enable(false);
  cb_default->Enable(true);
  cb_aac_is_sbr->Enable(false);
  tc_tags->Enable(true);
  b_browse_tags->Enable(true);
  rb_aspect_ratio->Enable(true);
  rb_display_dimensions->Enable(true);
  cob_fourcc->Enable(true);
  cob_compression->Enable(false);
  tc_timecodes->Enable(true);
  b_browse_timecodes->Enable(true);
}

void
tab_input::subtitle_track_mode(wxString ctype) {
  wxString lctype;

  lctype = ctype.Lower();
  cob_language->Enable(true);
  tc_delay->Enable(true);
  tc_track_name->Enable(true);
  tc_stretch->Enable(true);
  cob_cues->Enable(true);
  cob_sub_charset->Enable(lctype.Find("vobsub") < 0);
  cb_default->Enable(true);
  cb_aac_is_sbr->Enable(false);
  tc_tags->Enable(true);
  b_browse_tags->Enable(true);
  cob_aspect_ratio->Enable(false);
  tc_display_width->Enable(false);
  tc_display_height->Enable(false);
  rb_aspect_ratio->Enable(false);
  rb_display_dimensions->Enable(false);
  cob_fourcc->Enable(false);
  cob_compression->Enable(lctype.Find("vobsub") >= 0);
  tc_timecodes->Enable(true);
  b_browse_timecodes->Enable(true);
}

void
tab_input::enable_ar_controls(mmg_track_t *track) {
  bool ar_enabled;

  ar_enabled = !track->display_dimensions_selected;
  cob_aspect_ratio->Enable(ar_enabled);
  tc_display_width->Enable(!ar_enabled);
  tc_display_height->Enable(!ar_enabled);
  rb_aspect_ratio->SetValue(ar_enabled);
  rb_display_dimensions->SetValue(!ar_enabled);
}

void
tab_input::on_add_file(wxCommandEvent &evt) {
  mmg_file_t file;
  wxString file_name, name, command, id, type, exact, media_files;
  wxArrayString output, errors;
  vector<wxString> args, pair;
  int result, pos;
  unsigned int i, k;

  media_files = wxS("Media files (*.aac;*.ac3;*.ass;*.avi;*.dts;");
  if (capabilities[wxS("FLAC")] == wxS("true"))
    media_files += wxS("*.flac;*.idx;");
  media_files += wxS("*.m4a;*.mp2;*.mp3;*.mka;"
                     "*.mkv;*.mov;*.mp4;*.ogm;*.ogg;"
                     "*.ra;*.ram;*.rm;*.rmvb;*.rv;"
                     "*.srt;*.ssa;"
                     "*.wav)|"
                     "*.aac;*.ac3;*.ass;*.avi;*.dts;*.flac;"
                     "*.idx;*.mp2;*.mp3;*.mka;"
                     "*.mkv;*.mov;"
                     "*.mp4;*.ogm;*.ogg;"
                     "*.ra;*.ram;*.rm;*.rmvb;*.rv;"
                     "*.srt;*.ssa;*.wav|"
                     "AAC (Advanced Audio Coding) (*.aac;*.m4a;*.mp4)|"
                     "*.aac;*.m4a;*.mp4|"
                     "A/52 (aka AC3) (*.ac3)|*.ac3|"
                     "AVI (Audio/Video Interleaved) (*.avi)|*.avi|"
                     "DTS (Digital Theater System) (*.dts)|*.dts|");
  if (capabilities[wxS("FLAC")] == wxS("true"))
    media_files += wxS("FLAC (Free Lossless Audio Codec) (*.flac;*.ogg)|"
                       "*.flac;*.ogg|");
  media_files += wxS("MPEG audio files (*.mp2;*.mp3)|*.mp2;*.mp3|"
                     "Matroska A/V files (*.mka;*.mkv)|*.mka;*.mkv|"
                     "QuickTime/MP4 A/V (*.mov;*.mp4)|*.mov;*.mp4|"
                     "Audio/Video embedded in OGG (*.ogg;*.ogm)|*.ogg;*.ogm|"
                     "RealMedia Files (*.ra;*.ram;*.rm;*.rmvb;*.rv)|"
                     "*.ra;*.ram;*.rm;*.rmvb;*.rv|"
                     "SRT text subtitles (*.srt)|*.srt|"
                     "SSA/ASS text subtitles (*.ssa;*.ass)|*.ssa;*.ass|"
                     "VobSub subtitles (*.idx)|*.idx|"
                     "WAVE (uncompressed PCM) (*.wav)|*.wav|" ALLFILES);
  wxFileDialog dlg(NULL, "Choose an input file", last_open_dir, "",
                   media_files, wxOPEN);

  if(dlg.ShowModal() == wxID_OK) {
    last_open_dir = dlg.GetDirectory();
    file_name = dlg.GetPath();

    command = "\"" + mkvmerge_path + "\" --identify-verbose \"" + file_name +
      "\"";
    result = wxExecute(command, output, errors);
    if ((result < 0) || (result > 1)) {
      name.Printf("'mkvmerge -i' failed. Return code: %d\n\n", result);
      for (i = 0; i < output.Count(); i++)
        name += break_line(output[i]) + "\n";
      name += "\n";
      for (i = 0; i < errors.Count(); i++)
        name += break_line(errors[i]) + "\n";
      wxMessageBox(name, "'mkvmerge -i' failed", wxOK | wxCENTER |
                   wxICON_ERROR);
      return;
    } else if (result > 0) {
      name.Printf("'mkvmerge -i' failed. Return code: %d. Errno: %d (%s).\n"
                  "Make sure that you've selected a mkvmerge executable"
                  "on the 'settings' tab.", result, errno, strerror(errno));
      wxMessageBox(name, "'mkvmerge -i' failed", wxOK | wxCENTER |
                   wxICON_ERROR);
      return;
    }

    memset(&file, 0, sizeof(mmg_file_t));
    file.tracks = new vector<mmg_track_t>;
    file.title = new wxString;

    for (i = 0; i < output.Count(); i++) {
      if (output[i].Find("Track") == 0) {
        wxString info;
        mmg_track_t track;

        memset(&track, 0, sizeof(mmg_track_t));
        id = output[i].AfterFirst(' ').AfterFirst(' ').BeforeFirst(':');
        type = output[i].AfterFirst(':').BeforeFirst('(').Mid(1).RemoveLast();
        exact = output[i].AfterFirst('(').BeforeFirst(')');
        info = output[i].AfterFirst('[').BeforeLast(']');
        if (type == "audio")
          track.type = 'a';
        else if (type == "video")
          track.type = 'v';
        else if (type == "subtitles")
          track.type = 's';
        else
          track.type = '?';
        parse_int(id, track.id);
        track.ctype = new wxString(exact);
        track.enabled = true;
        track.language = new wxString("none");
        track.sub_charset = new wxString("default");
        track.cues = new wxString("default");
        track.track_name = new wxString("");
        track.delay = new wxString("");
        track.stretch = new wxString("");
        track.tags = new wxString("");
        track.aspect_ratio = new wxString("");
        track.dwidth = new wxString("");
        track.dheight = new wxString("");
        track.fourcc = new wxString("");
        track.compression = new wxString("");
        track.timecodes = new wxString("");

        if (info.length() > 0) {
          args = split(info, " ");
          for (k = 0; k < args.size(); k++) {
            pair = split(args[k], ":", 2);
            if (pair.size() != 2)
              continue;
            if (pair[0] == "track_name") {
              *track.track_name = from_utf8(unescape(pair[1]));
              track.track_name_was_present = true;
            } else if (pair[0] == "language")
              *track.language = unescape(pair[1]);
          }
        }

        file.tracks->push_back(track);

      } else if ((pos = output[i].Find("container:")) > 0) {
        wxString container, info;

        container = output[i].Mid(pos + 11).BeforeFirst(' ');
        info = output[i].Mid(pos + 11).AfterFirst('[').BeforeLast(']');
        if (container == "AAC")
          file.container = TYPEAAC;
        else if (container == "AC3")
          file.container = TYPEAC3;
        else if (container == "AVI")
          file.container = TYPEAVI;
        else if (container == "DTS")
          file.container = TYPEDTS;
        else if (container == "Matroska")
          file.container = TYPEMATROSKA;
        else if (container == "MP2/MP3")
          file.container = TYPEMP3;
        else if (container == "Ogg/OGM")
          file.container = TYPEOGM;
        else if (container == "Quicktime/MP4")
          file.container = TYPEQTMP4;
        else if (container == "RealMedia")
          file.container = TYPEREAL;
        else if (container == "SRT")
          file.container = TYPESRT;
        else if (container == "SSA/ASS")
          file.container = TYPESSA;
        else if (container == "VobSub")
          file.container = TYPEVOBSUB;
        else if (container == "WAV")
          file.container = TYPEWAV;
        else
          file.container = TYPEUNKNOWN;

        if (info.length() > 0) {
          args = split(info, " ");
          for (k = 0; k < args.size(); k++) {
            pair = split(args[k], ":", 2);
            if ((pair.size() == 2) && (pair[0] == "title")) {
              *file.title = from_utf8(unescape(pair[1]));
              title_was_present = true;
            }
          }
        }
      }
    }

    if (file.tracks->size() == 0) {
      delete file.tracks;
      wxMessageBox("The input file '" + dlg.GetPath() + "' does not contain "
                   "any tracks.", "No tracks found");
      return;
    }

    name = dlg.GetFilename();
    name += " (";
    name += last_open_dir;
    name += ")";
    lb_input_files->Append(name);

    file.file_name = new wxString(dlg.GetPath());
    mdlg->set_title_maybe(*file.title);
    mdlg->set_output_maybe(*file.file_name);
    files.push_back(file);
  }
}

void
tab_input::on_remove_file(wxCommandEvent &evt) {
  mmg_file_t *f;
  mmg_track_t *t;
  vector<mmg_file_t>::iterator eit;
  uint32_t i;

  if (selected_file == -1)
    return;

  f = &files[selected_file];
  for (i = 0; i < f->tracks->size(); i++) {
    t = &(*f->tracks)[i];
    delete t->ctype;
    delete t->language;
    delete t->sub_charset;
    delete t->cues;
    delete t->track_name;
    delete t->delay;
    delete t->stretch;
    delete t->tags;
    delete t->aspect_ratio;
    delete t->dwidth;
    delete t->dheight;
    delete t->fourcc;
    delete t->compression;
    delete t->timecodes;
  }
  delete f->tracks;
  delete f->file_name;
  delete f->title;
  eit = files.begin();
  eit += selected_file;
  files.erase(eit);
  lb_input_files->Delete(selected_file);
  selected_file = -1;
  cb_no_chapters->Enable(false);
  cb_no_attachments->Enable(false);
  cb_no_tags->Enable(false);
  b_remove_file->Enable(false);
  b_file_up->Enable(false);
  b_file_down->Enable(false);
  b_track_up->Enable(false);
  b_track_down->Enable(false);
  clb_tracks->Enable(false);
  no_track_mode();
}

void
tab_input::on_move_file_up(wxCommandEvent &evt) {
  wxString s;
  mmg_file_t f;

  if (selected_file < 1)
    return;

  f = files[selected_file - 1];
  files[selected_file - 1] = files[selected_file];
  files[selected_file] = f;
  s = lb_input_files->GetString(selected_file - 1);
  lb_input_files->SetString(selected_file - 1,
                            lb_input_files->GetString(selected_file));
  lb_input_files->SetString(selected_file, s);
  lb_input_files->SetSelection(selected_file - 1);
  selected_file--;
  b_file_up->Enable(selected_file > 0);
  b_file_down->Enable(true);
}

void
tab_input::on_move_file_down(wxCommandEvent &evt) {
  wxString s;
  mmg_file_t f;

  if ((selected_file < 0) || (selected_file >= (files.size() - 1)))
    return;

  f = files[selected_file + 1];
  files[selected_file + 1] = files[selected_file];
  files[selected_file] = f;
  s = lb_input_files->GetString(selected_file + 1);
  lb_input_files->SetString(selected_file + 1,
                            lb_input_files->GetString(selected_file));
  lb_input_files->SetString(selected_file, s);
  lb_input_files->SetSelection(selected_file + 1);
  selected_file++;
  b_file_up->Enable(true);
  b_file_down->Enable(selected_file < (files.size() - 1));
}

void
tab_input::on_move_track_up(wxCommandEvent &evt) {
  wxString s;
  mmg_track_t t;
  mmg_file_t *f;

  if (selected_file < 0)
    return;
  f = &files[selected_file];
  if (selected_track < 1)
    return;

  t = (*f->tracks)[selected_track - 1];
  (*f->tracks)[selected_track - 1] = (*f->tracks)[selected_track];
  (*f->tracks)[selected_track] = t;
  s = clb_tracks->GetString(selected_track - 1);
  clb_tracks->SetString(selected_track - 1,
                        clb_tracks->GetString(selected_track));
  clb_tracks->SetString(selected_track, s);
  clb_tracks->SetSelection(selected_track - 1);
  clb_tracks->Check(selected_track - 1,
                    (*f->tracks)[selected_track - 1].enabled);
  clb_tracks->Check(selected_track, (*f->tracks)[selected_track].enabled);
  selected_track--;
  b_track_up->Enable(selected_track > 0);
  b_track_down->Enable(true);
}

void
tab_input::on_move_track_down(wxCommandEvent &evt) {
  wxString s;
  mmg_track_t t;
  mmg_file_t *f;

  if (selected_file < 0)
    return;
  f = &files[selected_file];
  if ((selected_track < 0) || (selected_track >= (f->tracks->size() - 1)))
    return;

  t = (*f->tracks)[selected_track + 1];
  (*f->tracks)[selected_track + 1] = (*f->tracks)[selected_track];
  (*f->tracks)[selected_track] = t;
  s = clb_tracks->GetString(selected_track + 1);
  clb_tracks->SetString(selected_track + 1,
                        clb_tracks->GetString(selected_track));
  clb_tracks->SetString(selected_track, s);
  clb_tracks->SetSelection(selected_track + 1);
  clb_tracks->Check(selected_track + 1,
                    (*f->tracks)[selected_track + 1].enabled);
  clb_tracks->Check(selected_track, (*f->tracks)[selected_track].enabled);
  selected_track++;
  b_track_up->Enable(true);
  b_track_down->Enable(selected_track < (f->tracks->size() - 1));
}

void
tab_input::on_file_selected(wxCommandEvent &evt) {
  uint32_t i;
  int new_sel;
  mmg_file_t *f;
  mmg_track_t *t;
  wxString label;

  b_remove_file->Enable(true);
  selected_file = -1;
  new_sel = lb_input_files->GetSelection();
  b_file_up->Enable(new_sel > 0);
  b_file_down->Enable(new_sel < (files.size() - 1));
  f = &files[new_sel];
  if (f->container == TYPEMATROSKA) {
    cb_no_chapters->Enable(true);
    cb_no_attachments->Enable(true);
    cb_no_tags->Enable(true);
    cb_no_chapters->SetValue(f->no_chapters);
    cb_no_attachments->SetValue(f->no_attachments);
    cb_no_tags->SetValue(f->no_tags);
  } else {
    cb_no_chapters->Enable(false);
    cb_no_attachments->Enable(false);
    cb_no_tags->Enable(false);
    cb_no_chapters->SetValue(false);
    cb_no_attachments->SetValue(false);
    cb_no_tags->SetValue(false);
  }

  clb_tracks->Clear();
  for (i = 0; i < f->tracks->size(); i++) {
    string format;

    t = &(*f->tracks)[i];
    fix_format("%s (ID %lld, type: %s)", format);
    label.Printf(format.c_str(), t->ctype->c_str(), t->id,
                 t->type == 'a' ? "audio" :
                 t->type == 'v' ? "video" :
                 t->type == 's' ? "subtitles" : "unknown");
    clb_tracks->Append(label);
    clb_tracks->Check(i, t->enabled);
  }

  clb_tracks->Enable(true);
  selected_track = -1;
  selected_file = new_sel;
  clb_tracks->SetSelection(0);
  on_track_selected(evt);
}

void
tab_input::on_nochapters_clicked(wxCommandEvent &evt) {
  if (selected_file != -1)
    files[selected_file].no_chapters = cb_no_chapters->GetValue();
}

void
tab_input::on_noattachments_clicked(wxCommandEvent &evt) {
  if (selected_file != -1)
    files[selected_file].no_attachments = cb_no_attachments->GetValue();
}

void
tab_input::on_notags_clicked(wxCommandEvent &evt) {
  if (selected_file != -1)
    files[selected_file].no_tags = cb_no_tags->GetValue();
}

void
tab_input::on_track_selected(wxCommandEvent &evt) {
  mmg_file_t *f;
  mmg_track_t *t;
  int new_sel;
  uint32_t i;
  wxString lang;

  if (selected_file == -1)
    return;

  selected_track = -1;
  new_sel = clb_tracks->GetSelection();
  f = &files[selected_file];
  t = &(*f->tracks)[new_sel];

  b_track_up->Enable(new_sel > 0);
  b_track_down->Enable(new_sel < (f->tracks->size() - 1));

  if (t->type == 'a')
    audio_track_mode(*t->ctype);
  else if (t->type == 'v') {
    video_track_mode(*t->ctype);
    enable_ar_controls(t);
  } else if (t->type == 's')
    subtitle_track_mode(*t->ctype);

  lang = *t->language;
  for (i = 0; i < sorted_iso_codes.Count(); i++)
    if (extract_language_code(sorted_iso_codes[i]) == lang) {
      lang = sorted_iso_codes[i];
      break;
    }
  cob_language->SetValue(lang);
  tc_track_name->SetValue(*t->track_name);
  cob_cues->SetValue(*t->cues);
  tc_delay->SetValue(*t->delay);
  tc_stretch->SetValue(*t->stretch);
  cob_sub_charset->SetValue(*t->sub_charset);
  tc_tags->SetValue(*t->tags);
  cb_default->SetValue(t->default_track);
  cb_aac_is_sbr->SetValue(t->aac_is_sbr);
  cob_aspect_ratio->SetValue(*t->aspect_ratio);
  tc_display_width->SetValue(*t->dwidth); 
  tc_display_height->SetValue(*t->dheight);
  selected_track = new_sel;
  cob_compression->SetValue(*t->compression);
  tc_timecodes->SetValue(*t->timecodes);
  cob_fourcc->SetValue(*t->fourcc);
  tc_track_name->SetFocus();
}

void
tab_input::on_track_enabled(wxCommandEvent &evt) {
  uint32_t i;
  mmg_file_t *f;

  if (selected_file == -1)
    return;

  f = &files[selected_file];
  for (i = 0; i < f->tracks->size(); i++)
    (*f->tracks)[i].enabled = clb_tracks->IsChecked(i);
}

void
tab_input::on_default_track_clicked(wxCommandEvent &evt) {
  uint32_t i, k;
  mmg_track_t *t;

  if ((selected_file == -1) || (selected_track == -1))
    return;

  t = &(*files[selected_file].tracks)[selected_track];
  t->default_track = cb_default->GetValue();
  if (cb_default->GetValue())
    for (i = 0; i < files.size(); i++) {
      if (i != selected_file)
        for (k = 0; k < files[i].tracks->size(); k++)
          if ((k != selected_track) &&
              ((*files[i].tracks)[k].type == t->type))
            (*files[i].tracks)[k].default_track = false;
    }
}

void
tab_input::on_aac_is_sbr_clicked(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  (*files[selected_file].tracks)[selected_track].aac_is_sbr =
    cb_aac_is_sbr->GetValue();
}

void
tab_input::on_language_selected(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].language =
    cob_language->GetStringSelection();
}

void
tab_input::on_cues_selected(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].cues =
    cob_cues->GetStringSelection();
}

void
tab_input::on_subcharset_selected(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].sub_charset =
    cob_sub_charset->GetStringSelection();
}

void
tab_input::on_browse_tags(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  wxFileDialog dlg(NULL, wxS("Choose a tag file"), last_open_dir, wxS(""),
                   wxS("Tag files (*.xml;*.txt)|*.xml;*.txt|" ALLFILES),
                   wxOPEN);
  if(dlg.ShowModal() == wxID_OK) {
    last_open_dir = dlg.GetDirectory();
    *(*files[selected_file].tracks)[selected_track].tags = dlg.GetPath();
    tc_tags->SetValue(dlg.GetPath());
  }
}

void
tab_input::on_browse_timecodes_clicked(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  wxFileDialog dlg(NULL, wxS("Choose a timecodes file"), last_open_dir,
                   wxS(""), wxS("Tag files (*.txt)|*.txt|" ALLFILES), wxOPEN);
  if(dlg.ShowModal() == wxID_OK) {
    last_open_dir = dlg.GetDirectory();
    *(*files[selected_file].tracks)[selected_track].timecodes = dlg.GetPath();
    tc_timecodes->SetValue(dlg.GetPath());
  }
}

void
tab_input::on_tags_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].tags =
    tc_tags->GetValue();
}

void
tab_input::on_timecodes_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].timecodes =
    tc_timecodes->GetValue();
}

void
tab_input::on_delay_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].delay =
    tc_delay->GetValue();
}

void
tab_input::on_stretch_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].stretch =
    tc_stretch->GetValue();
}

void
tab_input::on_track_name_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].track_name =
    tc_track_name->GetValue();
}

void
tab_input::on_aspect_ratio_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].aspect_ratio =
    cob_aspect_ratio->GetStringSelection();
}

void
tab_input::on_fourcc_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].fourcc =
    cob_fourcc->GetStringSelection();
}

void
tab_input::on_compression_selected(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].compression =
    cob_compression->GetStringSelection();
}

void
tab_input::on_display_width_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].dwidth =
    tc_display_width->GetValue();
}

void
tab_input::on_display_height_changed(wxCommandEvent &evt) {
  if ((selected_file == -1) || (selected_track == -1))
    return;

  *(*files[selected_file].tracks)[selected_track].dheight =
    tc_display_height->GetValue();
}

void
tab_input::on_aspect_ratio_selected(wxCommandEvent &evt) {
  mmg_track_t *track;

  if ((selected_file == -1) || (selected_track == -1))
    return;

  track = &(*files[selected_file].tracks)[selected_track];
  track->display_dimensions_selected = false;
  enable_ar_controls(track);
}

void
tab_input::on_display_dimensions_selected(wxCommandEvent &evt) {
  mmg_track_t *track;

  if ((selected_file == -1) || (selected_track == -1))
    return;

  track = &(*files[selected_file].tracks)[selected_track];
  track->display_dimensions_selected = true;
  enable_ar_controls(track);
}

void
tab_input::on_value_copy_timer(wxTimerEvent &evt) {
  mmg_track_t *t;

  if ((selected_file == -1) || (selected_track == -1))
    return;

  t = &(*files[selected_file].tracks)[selected_track];
  *t->aspect_ratio = cob_aspect_ratio->GetValue();
  *t->fourcc = cob_fourcc->GetValue();
}

void
tab_input::save(wxConfigBase *cfg) {
  uint32_t fidx, tidx;
  mmg_file_t *f;
  mmg_track_t *t;
  wxString s;

  cfg->SetPath(wxS("/input"));
  cfg->Write(wxS("number_of_files"), (int)files.size());
  for (fidx = 0; fidx < files.size(); fidx++) {
    f = &files[fidx];
    s.Printf(wxS("file %u"), fidx);
    cfg->SetPath(s);
    cfg->Write(wxS("file_name"), *f->file_name);
    cfg->Write(wxS("container"), f->container);
    cfg->Write(wxS("no_chapters"), f->no_chapters);
    cfg->Write(wxS("no_attachments"), f->no_attachments);
    cfg->Write(wxS("no_tags"), f->no_tags);

    cfg->Write(wxS("number_of_tracks"), (int)f->tracks->size());
    for (tidx = 0; tidx < f->tracks->size(); tidx++) {
      string format;

      t = &(*f->tracks)[tidx];
      s.Printf(wxS("track %u"), tidx);
      cfg->SetPath(s);
      s.Printf(wxS("%c"), t->type);
      cfg->Write(wxS("type"), s);
      fix_format(wxS("%lld"), format);
      s.Printf(format.c_str(), t->id);
      cfg->Write(wxS("id"), s);
      cfg->Write(wxS("enabled"), t->enabled);
      cfg->Write(wxS("content_type"), *t->ctype);
      cfg->Write(wxS("default_track"), t->default_track);
      cfg->Write(wxS("aac_is_sbr"), t->aac_is_sbr);
      cfg->Write(wxS("language"), *t->language);
      cfg->Write(wxS("track_name"), *t->track_name);
      cfg->Write(wxS("cues"), *t->cues);
      cfg->Write(wxS("delay"), *t->delay);
      cfg->Write(wxS("stretch"), *t->stretch);
      cfg->Write(wxS("sub_charset"), *t->sub_charset);
      cfg->Write(wxS("tags"), *t->tags);
      cfg->Write(wxS("timecodes"), *t->timecodes);
      cfg->Write(wxS("display_dimensions_selected"),
                 t->display_dimensions_selected);
      cfg->Write(wxS("aspect_ratio"), *t->aspect_ratio);
      cfg->Write(wxS("display_width"), *t->dwidth);
      cfg->Write(wxS("display_height"), *t->dheight);
      cfg->Write(wxS("fourcc"), *t->fourcc);
      cfg->Write(wxS("compression"), *t->compression);

      cfg->SetPath(wxS(".."));
    }

    cfg->SetPath(wxS(".."));
  }
}

void
tab_input::load(wxConfigBase *cfg) {
  uint32_t fidx, tidx;
  mmg_file_t *f, fi;
  mmg_track_t *t, tr;
  wxString s, c, id;
  int num_files, num_tracks;

  clb_tracks->Clear();
  lb_input_files->Clear();
  no_track_mode();
  selected_file = -1;
  selected_track = -1;
  b_remove_file->Enable(false);

  for (fidx = 0; fidx < files.size(); fidx++) {
    f = &files[fidx];

    delete f->file_name;
    delete f->title;
    for (tidx = 0; tidx < f->tracks->size(); tidx++) {
      t = &(*f->tracks)[tidx];

      delete t->ctype;
      delete t->language;
      delete t->track_name;
      delete t->cues;
      delete t->delay;
      delete t->sub_charset;
      delete t->tags;
      delete t->stretch;
      delete t->aspect_ratio;
      delete t->dwidth;
      delete t->dheight;
      delete t->fourcc;
      delete t->compression;
      delete t->timecodes;
    }
    delete f->tracks;
  }
  files.clear();

  cfg->SetPath(wxS("/input"));
  if (!cfg->Read(wxS("number_of_files"), &num_files) || (num_files < 0))
    return;

  for (fidx = 0; fidx < (uint32_t)num_files; fidx++) {
    s.Printf(wxS("file %u"), fidx);
    cfg->SetPath(s);
    if (!cfg->Read(wxS("number_of_tracks"), &num_tracks) || (num_tracks < 0)) {
      cfg->SetPath(wxS(".."));
      continue;
    }
    if (!cfg->Read(wxS("file_name"), &s)) {
      cfg->SetPath(wxS(".."));
      continue;
    }
    fi.file_name = new wxString(s);
    cfg->Read(wxS("title"), &s);
    fi.title = new wxString(s);
    cfg->Read(wxS("container"), &fi.container);
    fi.no_chapters = false;
    cfg->Read(wxS("no_chapters"), &fi.no_chapters);
    fi.no_attachments = false;
    cfg->Read(wxS("no_attachments"), &fi.no_attachments);
    fi.no_tags = false;
    cfg->Read(wxS("no_tags"), &fi.no_tags);
    fi.tracks = new vector<mmg_track_t>;

    for (tidx = 0; tidx < (uint32_t)num_tracks; tidx++) {
      s.Printf(wxS("track %u"), tidx);
      cfg->SetPath(s);
      if (!cfg->Read(wxS("type"), &c) || (c.Length() != 1) ||
          !cfg->Read(wxS("id"), &id)) {
        cfg->SetPath(wxS(".."));
        continue;
      }
      tr.type = c.c_str()[0];
      if (((tr.type != 'a') && (tr.type != 'v') && (tr.type != 's')) ||
          !parse_int(id.c_str(), tr.id)) {
        cfg->SetPath(wxS(".."));
        continue;
      }
      cfg->Read(wxS("enabled"), &tr.enabled);
      cfg->Read(wxS("content_type"), &s);
      tr.ctype = new wxString(s);
      tr.default_track = false;
      cfg->Read(wxS("default_track"), &tr.default_track);
      tr.aac_is_sbr = false;
      cfg->Read(wxS("aac_is_sbr"), &tr.aac_is_sbr);
      cfg->Read(wxS("language"), &s);
      tr.language = new wxString(s);
      cfg->Read(wxS("track_name"), &s);
      tr.track_name = new wxString(s);
      cfg->Read(wxS("cues"), &s);
      tr.cues = new wxString(s);
      cfg->Read(wxS("delay"), &s);
      tr.delay = new wxString(s);
      cfg->Read(wxS("stretch"), &s);
      tr.stretch = new wxString(s);
      cfg->Read(wxS("sub_charset"), &s);
      tr.sub_charset = new wxString(s);
      cfg->Read(wxS("tags"), &s);
      tr.tags = new wxString(s);
      cfg->Read(wxS("display_dimensions_selected"),
                &tr.display_dimensions_selected);
      cfg->Read(wxS("aspect_ratio"), &s);
      tr.aspect_ratio = new wxString(s);
      cfg->Read(wxS("display_width"), &s);
      tr.dwidth = new wxString(s);
      cfg->Read(wxS("display_height"), &s);
      tr.dheight = new wxString(s);
      cfg->Read(wxS("fourcc"), &s);
      tr.fourcc = new wxString(s);
      cfg->Read(wxS("compression"), &s);
      tr.compression = new wxString(s);
      cfg->Read(wxS("timecodes"), &s);
      tr.timecodes = new wxString(s);

      fi.tracks->push_back(tr);
      cfg->SetPath(wxS(".."));
    }

    if (fi.tracks->size() == 0) {
      delete fi.tracks;
      delete fi.file_name;
    } else {
      s = fi.file_name->BeforeLast(PSEP);
      c = fi.file_name->AfterLast(PSEP);
      lb_input_files->Append(c + wxS(" (") + s + wxS(")"));
      files.push_back(fi);
    }

    cfg->SetPath(wxS(".."));
  }
}

bool
tab_input::validate_settings() {
  uint32_t fidx, tidx, i;
  mmg_file_t *f;
  mmg_track_t *t;
  bool tracks_selected, dot_present, ok;
  int64_t dummy_i;
  string s, format;
  wxString sid;

  tracks_selected = false;
  for (fidx = 0; fidx < files.size(); fidx++) {
    f = &files[fidx];

    for (tidx = 0; tidx < f->tracks->size(); tidx++) {
      t = &(*f->tracks)[tidx];
      if (!t->enabled)
        continue;

      tracks_selected = true;
      fix_format("%lld", format);
      sid.Printf(format.c_str(), t->id);

      s = t->delay->c_str();
      strip(s);
      if ((s.length() > 0) && !parse_int(s.c_str(), dummy_i)) {
        wxMessageBox(wxS("The delay setting for track nr. ") + sid +
                     wxS(" in file '") + *f->file_name + wxS("' is invalid."),
                     wxS("mkvmerge GUI: error"),
                     wxOK | wxCENTER | wxICON_ERROR);
        return false;
      }

      s = t->stretch->c_str();
      strip(s);
      if (s.length() > 0) {
        dot_present = false;
        i = 0;
        while (i < s.length()) {
          if (isdigit(s[i]) ||
              (!dot_present && ((s[i] == wxC('.')) || (s[i] == wxC(','))))) {
            if ((s[i] == wxC('.')) || (s[i] == wxC(',')))
              dot_present = true;
            i++;
          } else {
            wxMessageBox(wxS("The stretch setting for track nr. ") + sid +
                         wxS(" in file '") + *f->file_name +
                         wxS("' is invalid."), wxS("mkvmerge GUI: error"),
                         wxOK | wxCENTER | wxICON_ERROR);
            return false;
          }
        }
      }

      s = t->fourcc->c_str();
      strip(s);
      if ((s.length() > 0) && (s.length() != 4)) {
        wxMessageBox(wxS("The FourCC setting for track nr. ") + sid +
                     wxS(" in file '") + *f->file_name +
                     wxS("' is not excatly four characters long."),
                     wxS("mkvmerge GUI: error"),
                     wxOK | wxCENTER | wxICON_ERROR);
        return false;
      }

      s = t->aspect_ratio->c_str();
      strip(s);
      if (s.length() > 0) {
        dot_present = false;
        i = 0;
        ok = true;
        while (i < s.length()) {
          if (isdigit(s[i]) ||
              (!dot_present && ((s[i] == wxC('.')) || (s[i] == wxC(','))))) {
            if ((s[i] == wxC('.')) || (s[i] == wxC(',')))
              dot_present = true;
            i++;
          } else {
            ok = false;
            break;
          }
        }

        if (!ok) {
          dot_present = false;
          i = 0;
          ok = true;
          while (i < s.length()) {
            if (isdigit(s[i]) ||
                (!dot_present && (s[i] == wxC('/')))) {
              if (s[i] == wxC('/'))
                dot_present = true;
              i++;
            } else {
              ok = false;
              break;
            }
          }
        }

        if (!ok) {
          wxMessageBox(wxS("The aspect ratio setting for track nr. ") + sid +
                       wxS(" in file '") + *f->file_name +
                       wxS("' is invalid."), wxS("mkvmerge GUI: error"),
                       wxOK | wxCENTER | wxICON_ERROR);
          return false;
        }
      }
    }
  }

  if (!tracks_selected) {
    wxMessageBox(wxS("You have not yet selected any input file and/or no "
                     "tracks."),
                 wxS("mkvmerge GUI: error"), wxOK | wxCENTER | wxICON_ERROR);
    return false;
  }

  return true;
}

IMPLEMENT_CLASS(tab_input, wxPanel);
BEGIN_EVENT_TABLE(tab_input, wxPanel)
  EVT_BUTTON(ID_B_ADDFILE, tab_input::on_add_file)
  EVT_BUTTON(ID_B_REMOVEFILE, tab_input::on_remove_file) 
  EVT_BUTTON(ID_B_INPUTUP, tab_input::on_move_file_up)
  EVT_BUTTON(ID_B_INPUTDOWN, tab_input::on_move_file_down)
  EVT_BUTTON(ID_B_TRACKUP, tab_input::on_move_track_up)
  EVT_BUTTON(ID_B_TRACKDOWN, tab_input::on_move_track_down)
  EVT_BUTTON(ID_B_BROWSETAGS, tab_input::on_browse_tags)
  EVT_BUTTON(ID_B_BROWSE_TIMECODES, tab_input::on_browse_timecodes_clicked)
  EVT_TEXT(ID_TC_TAGS, tab_input::on_tags_changed)
  EVT_TEXT(ID_TC_TIMECODES, tab_input::on_timecodes_changed)

  EVT_LISTBOX(ID_LB_INPUTFILES, tab_input::on_file_selected)
  EVT_LISTBOX(ID_CLB_TRACKS, tab_input::on_track_selected)
  EVT_CHECKLISTBOX(ID_CLB_TRACKS, tab_input::on_track_enabled)

  EVT_CHECKBOX(ID_CB_NOCHAPTERS, tab_input::on_nochapters_clicked)
  EVT_CHECKBOX(ID_CB_NOATTACHMENTS, tab_input::on_noattachments_clicked)
  EVT_CHECKBOX(ID_CB_NOTAGS, tab_input::on_notags_clicked)
  EVT_CHECKBOX(ID_CB_MAKEDEFAULT, tab_input::on_default_track_clicked)
  EVT_CHECKBOX(ID_CB_AACISSBR, tab_input::on_aac_is_sbr_clicked)

  EVT_COMBOBOX(ID_CB_LANGUAGE, tab_input::on_language_selected)
  EVT_COMBOBOX(ID_CB_CUES, tab_input::on_cues_selected)
  EVT_COMBOBOX(ID_CB_SUBTITLECHARSET, tab_input::on_subcharset_selected)
  EVT_COMBOBOX(ID_CB_ASPECTRATIO, tab_input::on_aspect_ratio_changed)
  EVT_COMBOBOX(ID_CB_FOURCC, tab_input::on_fourcc_changed)
  EVT_TEXT(ID_CB_SUBTITLECHARSET, tab_input::on_subcharset_selected)
  EVT_TEXT(ID_TC_DELAY, tab_input::on_delay_changed)
  EVT_TEXT(ID_TC_STRETCH, tab_input::on_stretch_changed)
  EVT_TEXT(ID_TC_TRACKNAME, tab_input::on_track_name_changed)
  EVT_RADIOBUTTON(ID_RB_ASPECTRATIO, tab_input::on_aspect_ratio_selected)
  EVT_RADIOBUTTON(ID_RB_DISPLAYDIMENSIONS,
                  tab_input::on_display_dimensions_selected)
  EVT_TEXT(ID_TC_DISPLAYWIDTH, tab_input::on_display_width_changed)
  EVT_TEXT(ID_TC_DISPLAYHEIGHT, tab_input::on_display_height_changed)
  EVT_COMBOBOX(ID_CB_COMPRESSION, tab_input::on_compression_selected)

  EVT_TIMER(ID_T_INPUTVALUES, tab_input::on_value_copy_timer)

END_EVENT_TABLE();
