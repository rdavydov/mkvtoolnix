/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  r_matroska.cpp

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version \$Id: r_matroska.cpp,v 1.3 2003/04/17 12:35:45 mosu Exp $
    \brief Matroska reader
    \author Moritz Bunkus         <moritz @ bunkus.org>
*/

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exception>
#include <typeinfo>

extern "C" {                    // for BITMAPINFOHEADER
#include "avilib.h"
}

#include "common.h"
#include "pr_generic.h"
#include "r_matroska.h"
#include "p_vorbis.h"
#include "p_video.h"
#include "p_pcm.h"
#include "p_textsubs.h"
#include "p_mp3.h"
#include "p_ac3.h"

#include "EbmlHead.h"
#include "EbmlSubHead.h"
#include "EbmlStream.h"
#include "EbmlContexts.h"
#include "FileKax.h"
#include "KaxSegment.h"
#include "KaxContexts.h"
#include "KaxTracks.h"
#include "KaxInfo.h"
#include "KaxInfoData.h"
#include "KaxCluster.h"
#include "KaxClusterData.h"
#include "StdIOCallback.h"
#include "KaxTrackAudio.h"
#include "KaxTrackVideo.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

using namespace std;
using namespace LIBMATROSKA_NAMESPACE;

/*
 * Probes a file by simply comparing the first four bytes to the EBML
 * head signature.
 */
int mkv_reader_c::probe_file(FILE *file, int64_t size) {
  unsigned char data[4];
  
  if (size < 4)
    return 0;
  if (fseek(file, 0, SEEK_SET) != 0)
    return 0;
  if (fread(data, 1, 4, file) != 4) {
    fseek(file, 0, SEEK_SET);
    return 0;
  }
  fseek(file, 0, SEEK_SET);
  if ((data[0] != 0x1A) || (data[1] != 0x45) ||
      (data[2] != 0xDF) || (data[3] != 0xA3))
    return 0;
  return 1;
}

mkv_reader_c::mkv_reader_c(track_info_t *nti) throw (error_c):
  generic_reader_c(nti) {
  tracks = NULL;
  num_tracks = 0;

  fprintf(stdout, "WARNING! Matroska files cannot be processed at the "
          "moment.\n");

  if (!read_headers())
    throw error_c("matroska_reader: Failed to read the headers.");

  create_packetizers();
}

mkv_reader_c::~mkv_reader_c() {
  int i;
  
  for (i = 0; i < num_tracks; i++)
    if (tracks[i] != NULL) {
      if (tracks[i]->private_data != NULL)
        free(tracks[i]->private_data);
      free(tracks[i]);
    }
  
  if (es != NULL)
    delete es;
  if (saved_l1 != NULL)
    delete saved_l1;
  if (in != NULL)
    delete in;
  if (segment != NULL)
    delete segment;
}

int mkv_reader_c::packets_available() {
  int i;

  for (i = 0; i < num_tracks; i++)
    if (tracks[i]->ok && (!tracks[i]->packetizer->packet_available()))
      return 0;

  if (num_tracks == 0)
    return 0;

  return 1;
}

void mkv_reader_c::handle_subtitles(mkv_track_t *t, KaxBlock &block) {
  int len;
  u_int32_t block_nr;
  char *s1, *buffer;
  long duration;
  int64_t start;

  for (block_nr = 0; block_nr < block.NumberFrames(); block_nr++) {
    DataBuffer &data = block.GetBuffer(block_nr);
    len = data.Size();
    buffer = (char *)malloc(len + 1);
    if (buffer == NULL) {
      printf("[mkv] Could not allocate memory for subtitles.\n");
      return;
    }
    memcpy(buffer, data.Buffer(), len);
    buffer[len] = 0;

    // Extract the duration (which is the first line).
    s1 = buffer;
    while ((*s1 != 0) && (*s1 != '\n') && (*s1 != '\r'))
      s1++;
    if (*s1 == 0)
      return;

    *s1 = 0;
    s1++;
    duration = strtol(buffer, NULL, 10); // in ms
    while ((*s1 == '\n') || (*s1 == '\r'))
      s1++;

    start = cluster_tc + block.Timecod();
    t->packetizer->process((unsigned char *)s1, 0, start, duration);

    free(buffer);
  }
}

mkv_track_t *mkv_reader_c::new_mkv_track() {
  mkv_track_t *t;
  
  t = (mkv_track_t *)malloc(sizeof(mkv_track_t));
  if (t != NULL) {
    memset(t, 0, sizeof(mkv_track_t));
    tracks = (mkv_track_t **)realloc(tracks, (num_tracks + 1) *
                                     sizeof(mkv_track_t *));
    if (tracks == NULL)
      return NULL;
    tracks[num_tracks] = t;
    num_tracks++;
  }
  
  return t;
}

mkv_track_t *mkv_reader_c::find_track_by_num(u_int32_t n, mkv_track_t *c) {
  int i;
  
  for (i = 0; i < num_tracks; i++)
    if ((tracks[i] != NULL) && (tracks[i]->tnum == n) &&
        (tracks[i] != c))
      return tracks[i];
  
  return NULL;
}

void mkv_reader_c::verify_tracks() {
  int tnum, i;
  unsigned char *c;
  u_int32_t u, offset, length;
  mkv_track_t *t;
  BITMAPINFOHEADER *bih;
  WAVEFORMATEX *wfe;
  
  for (tnum = 0; tnum < num_tracks; tnum++) {
    t = tracks[tnum];
    switch (t->type) {
      case 'v':                 // video track
        if (t->codec_id == NULL)
          continue;
        if (!strcmp(t->codec_id, MKV_MSVCM)) {
          if ((t->private_data == NULL) ||
              (t->private_size < sizeof(BITMAPINFOHEADER))) {
            printf("[mkv] WARNING: CodecID for track %u is '" MKV_MSVCM "', "
                   "but there was no BITMAPINFOHEADER struct present. "
                   "Therefore we don't have a FourCC to identify the video "
                   "codec used.\n", t->tnum);
            continue;
          } else {
            t->ms_compat = 1;

            bih = (BITMAPINFOHEADER *)t->private_data;

            u = get_uint32(&bih->bi_width);
            if (t->v_width != u) {
              printf("[mkv] WARNING: (MS compatibility mode, track %u) "
                     "Matrosa says video width is %u, but the "
                     "BITMAPINFOHEADER says %u.\n", t->tnum, t->v_width, u);
              if (t->v_width == 0)
                t->v_width = u;
            }

            u = get_uint32(&bih->bi_height);
            if (t->v_height != u) {
              printf("[mkv] WARNING: (MS compatibility mode, track %u) "
                     "Matrosa video height is %u, but the BITMAPINFOHEADER "
                     "says %u.\n", t->tnum, t->v_height, u);
              if (t->v_height == 0)
                t->v_height = u;
            }

            memcpy(t->v_fourcc, &bih->bi_compression, 4);

            if (t->v_frate == 0.0) {
              printf("[mkv] ERROR: (MS compatibility mode, track %u) "
                     "No VideoFrameRate element was found.\n", t->tnum);
              continue;
            }
          }
        } else {
          printf("[mkv] Native CodecIDs for video tracks are not supported "
                 "yet (track %u).\n", t->tnum);
          continue;
        }

        if (t->v_width == 0) {
          printf("[mkv] The width for track %u was not set.\n", t->tnum);
          continue;
        }
        if (t->v_height == 0) {
          printf("[mkv] The height for track %u was not set.\n", t->tnum);
          continue;
        }

        // This track seems to be ok.
        t->ok = 1;

        break;

      case 'a':                 // audio track
        if (t->codec_id == NULL)
          continue;
        if (!strcmp(t->codec_id, MKV_MSACM)) {
          if ((t->private_data == NULL) ||
              (t->private_size < sizeof(WAVEFORMATEX))) {
            printf("[mkv] WARNING: CodecID for track %u is '" MKV_MSACM "', "
                   "but there was no WAVEFORMATEX struct present. "
                   "Therefore we don't have a format ID to identify the audio "
                   "codec used.\n", t->tnum);
            continue;
          } else {
            t->ms_compat = 1;

            wfe = (WAVEFORMATEX *)t->private_data;
            u = get_uint32(&wfe->n_samples_per_sec);
            if (((u_int32_t)t->a_sfreq) != u) {
              printf("[mkv] WARNING: (MS compatibility mode for track %u) "
                     "Matroska says that there are %u samples per second, "
                     "but WAVEFORMATEX says that there are %u.\n", t->tnum,
                     (u_int32_t)t->a_sfreq, u);
              if (t->a_sfreq == 0.0)
                t->a_sfreq = (float)u;
            }

            u = get_uint16(&wfe->n_channels);
            if (t->a_channels != u) {
              printf("[mkv] WARNING: (MS compatibility mode for track %u) "
                     "Matroska says that there are %u channels, but the "
                     "WAVEFORMATEX says that there are %u.\n", t->tnum,
                     t->a_channels, u);
              if (t->a_channels == 0)
                t->a_channels = u;
            }

            u = get_uint16(&wfe->w_bits_per_sample);
            if (t->a_channels != u) {
              printf("[mkv] WARNING: (MS compatibility mode for track %u) "
                     "Matroska says that there are %u bits per sample, "
                     "but the WAVEFORMATEX says that there are %u.\n", t->tnum,
                     t->a_bps, u);
              if (t->a_bps == 0)
                t->a_bps = u;
            }
            
            t->a_formattag = get_uint16(&wfe->w_format_tag);
          }
        } else {
          if (!strcmp(t->codec_id, "A_MPEGLAYER3"))
            t->a_formattag = 0x0055;
          else if (!strcmp(t->codec_id, "A_DOL_AC3"))
            t->a_formattag = 0x2000;
          else if (!strcmp(t->codec_id, "A_PCM16IN"))
            t->a_formattag = 0x0001;
          else if (!strcmp(t->codec_id, "A_VORBIS")) {
            if (t->private_data == NULL) {
              printf("[mkv] WARNING: CodecID for track %u is 'A_VORBIS', "
                     "but there are no header packets present.", t->tnum);
              continue;
            }

            c = (unsigned char *)t->private_data;
            if (c[0] != 3) {
              printf("[mkv] Vorbis track does not contain valid headers.\n");
              continue;
            }

            offset = 1;
            for (i = 0; i < 2; i++) {
              length = 0;
              while ((c[offset] == (unsigned char )255) &&
                     (length < t->private_size)) {
                length += 255;
                offset++;
              }
              if (offset >= (t->private_size - 1)) {
                printf("[mkv] Vorbis track does not contain valid headers.\n");
                continue;
              }
              length += c[offset];
              offset++;
              t->headers[i] = &c[offset];
              t->header_sizes[i] = length;
              offset += length;
            }

            t->headers[2] = &c[offset];
            t->header_sizes[2] = t->private_size - offset;

            t->a_formattag = 0xFFFE;
          } else {
            printf("[mkv] Unknown/unsupported audio codec ID '%s' for track "
                   "%u.\n", t->codec_id, t->tnum);
            continue;
          }
        }

        if (t->a_sfreq == 0.0) {
          printf("[mkv] The sampling frequency was not set for track %u.\n",
                 t->tnum);
          continue;
        }

        if (t->a_channels == 0) {
          printf("[mkv] The number of channels was not set for track %u.\n",
                 t->tnum);
          continue;
        }

        if (t->a_formattag == 0) {
          printf("[mkv] The audio format tag was not set for track %u.\n",
                 t->tnum);
          continue;
        }

        // This track seems to be ok.
        t->ok = 1;

        break;

      case 's':                 // Text subtitles do not need any data
        t->ok = 1;              // except the CodecID.
        break;

      default:                  // unknown track type!? error in demuxer...
        printf("[mkv] Error: matroska_reader: unknown demuxer type for track "
               "%u: '%c'\n", t->tnum, t->type);
        continue;
    }

    if (t->ok)
      printf("[mkv] Track %u seems to be ok.\n", t->tnum);
  }
}

int mkv_reader_c::read_headers() {
  int upper_lvl_el, exit_loop;
  // Elements for different levels
  EbmlElement *l0 = NULL, *l1 = NULL, *l2 = NULL, *l3 = NULL, *l4 = NULL;
  mkv_track_t *track;

  try {
    // Create the interface between MPlayer's IO system and
    // libmatroska's IO system.
    in = new StdIOCallback(ti->fname, MODE_READ);
    if (in == NULL)
      die("new");

    es = new EbmlStream(*in);
    if (es == NULL)
      die("new");
    
    // Find the EbmlHead element. Must be the first one.
    l0 = es->FindNextID(EbmlHead::ClassInfos, 0xFFFFFFFFL, false);
    if (l0 == NULL) {
      fprintf(stderr, "Error: matroska_reader: no EBML head found.\n");
      return 0;
    }

    // Don't verify its data for now.
    l0->SkipData(static_cast<EbmlStream &>(*es), l0->Generic().Context);
    delete l0;
    fprintf(stdout, "matroska_reader: Found the head...\n");
    
    // Next element must be a segment
    l0 = es->FindNextID(KaxSegment::ClassInfos, 0xFFFFFFFFL, false);
    if (l0 == NULL) {
      fprintf(stdout, "matroska_reader: but no segment :(\n");
      return 0;
    }
    if (!(EbmlId(*l0) == KaxSegment::ClassInfos.GlobalId)) {
      fprintf(stdout, "matroska_reader: but no segment :(\n");
      return 0;
    }
    fprintf(stdout, "matroska_reader: + a segment...\n");
    
    segment = l0;

    upper_lvl_el = 0;
    exit_loop = 0;
    // We've got our segment, so let's find the tracks
    l1 = es->FindNextID(l0->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                        true);
    while (l1 != NULL) {
      if ((upper_lvl_el != 0) || exit_loop)
        break;

      if (EbmlId(*l1) == KaxInfo::ClassInfos.GlobalId) {
        // General info about this Matroska file
        fprintf(stdout, "matroska_reader: |+ segment information...\n");
        
        l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                            true);
        while (l2 != NULL) {
          if ((upper_lvl_el != 0) || exit_loop)
            break;

          if (EbmlId(*l2) == KaxTimecodeScale::ClassInfos.GlobalId) {
            KaxTimecodeScale &ktc_scale = *static_cast<KaxTimecodeScale *>(l2);
            tc_scale = uint64(ktc_scale);
            fprintf(stdout, "matroska_reader: | + timecode scale: %llu\n",
                    tc_scale);
          } else
            fprintf(stdout, "matroska_reader: | + unknown element@2: %s, "
                    "ule %d\n", typeid(*l2).name(), upper_lvl_el);

          if (upper_lvl_el > 0) {	// we're coming from l3
            upper_lvl_el--;
            delete l2;
            l2 = l3;
            if (upper_lvl_el > 0)
              break;
          } else {
            l2->SkipData(static_cast<EbmlStream &>(*es),
                         l2->Generic().Context);
            delete l2;
            l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el,
                                0xFFFFFFFFL, true);
          }
        }

      } else if (EbmlId(*l1) == KaxTracks::ClassInfos.GlobalId) {
        // Yep, we've found our KaxTracks element. Now find all tracks
        // contained in this segment.
        fprintf(stdout, "matroska_reader: |+ segment tracks...\n");
        
        l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                            true);
        while (l2 != NULL) {
          if ((upper_lvl_el != 0) || exit_loop)
            break;
          
          if (EbmlId(*l2) == KaxTrackEntry::ClassInfos.GlobalId) {
            // We actually found a track entry :) We're happy now.
            fprintf(stdout, "matroska_reader: | + a track...\n");
            
            track = new_mkv_track();
            if (track == NULL)
              return 0;
            
            l3 = es->FindNextID(l2->Generic().Context, upper_lvl_el,
                                0xFFFFFFFFL, true);
            while (l3 != NULL) {
              if (upper_lvl_el != 0)
                break;
              
              // Now evaluate the data belonging to this track
              if (EbmlId(*l3) == KaxTrackNumber::ClassInfos.GlobalId) {
                KaxTrackNumber &tnum = *static_cast<KaxTrackNumber *>(l3);
                tnum.ReadData(es->I_O());
                fprintf(stdout, "matroska_reader: |  + Track number %d\n",
                        uint8(tnum));
                track->tnum = uint8(tnum);
                if (find_track_by_num(track->tnum, track) != NULL)
                  fprintf(stdout, "matroska_reader: |  + WARNING: There's "
                          "more than one track with the number %u.\n",
                          track->tnum);

              } else if (EbmlId(*l3) == KaxTrackType::ClassInfos.GlobalId) {
                KaxTrackType &ttype = *static_cast<KaxTrackType *>(l3);
                ttype.ReadData(es->I_O());
                fprintf(stdout, "matroska_reader: |  + Track type: ");

                switch (uint8(ttype)) {
                  case track_audio:
                    printf("Audio\n");
                    track->type = 'a';
                    break;
                  case track_video:
                    printf("Video\n");
                    track->type = 'v';
                    break;
                  case track_subtitle:
                    printf("Subtitle\n");
                    track->type = 's';
                    break;
                  default:
                    printf("unknown\n");
                    track->type = '?';
                    break;
                }

              } else if (EbmlId(*l3) == KaxTrackAudio::ClassInfos.GlobalId) {
                fprintf(stdout, "matroska_reader: |  + Audio track\n");
                l4 = es->FindNextID(l3->Generic().Context, upper_lvl_el,
                                    0xFFFFFFFFL, true);
                while (l4 != NULL) {
                  if (upper_lvl_el != 0)
                    break;
                
                  if (EbmlId(*l4) ==
                      KaxAudioSamplingFreq::ClassInfos.GlobalId) {
                    KaxAudioSamplingFreq &freq =
                      *static_cast<KaxAudioSamplingFreq*>(l4);
                    freq.ReadData(es->I_O());
                    track->a_sfreq = float(freq);
                    fprintf(stdout, "matroska_reader: |   + Sampling "
                            "frequency: %f\n", track->a_sfreq);

                  } else if (EbmlId(*l4) ==
                             KaxAudioChannels::ClassInfos.GlobalId) {
                    KaxAudioChannels &channels =
                      *static_cast<KaxAudioChannels*>(l4);
                    channels.ReadData(es->I_O());
                    track->a_channels = uint8(channels);
                    fprintf(stdout, "matroska_reader: |   + Channels: %u\n",
                           track->a_channels);

                  } else if (EbmlId(*l4) ==
                             KaxAudioBitDepth::ClassInfos.GlobalId) {
                    KaxAudioBitDepth &bps =
                      *static_cast<KaxAudioBitDepth*>(l4);
                    bps.ReadData(es->I_O());
                    track->a_bps = uint8(bps);
                    fprintf(stdout, "matroska_reader: |   + Bit depth: %u\n",
                           track->a_bps);

                  } else
                    fprintf(stdout, "matroska_reader: |   + unknown "
                            "element@4: %s\n", typeid(*l4).name());

                  if (upper_lvl_el > 0) {
									  assert(1 == 0);	// this should never happen
                  } else {
                    l4->SkipData(static_cast<EbmlStream &>(*es),
                                 l4->Generic().Context);
                    delete l4;
                    l4 = es->FindNextID(l3->Generic().Context, upper_lvl_el,
                                        0xFFFFFFFFL, true);
                  }
                } // while (l4 != NULL)

              } else if (EbmlId(*l3) == KaxTrackVideo::ClassInfos.GlobalId) {
                fprintf(stdout, "matroska_reader: |  + Video track\n");
                l4 = es->FindNextID(l3->Generic().Context, upper_lvl_el,
                                    0xFFFFFFFFL, true);
                while (l4 != NULL) {
                  if (upper_lvl_el != 0)
                    break;

                  if (EbmlId(*l4) == KaxVideoPixelWidth::ClassInfos.GlobalId) {
                    KaxVideoPixelWidth &width =
                      *static_cast<KaxVideoPixelWidth *>(l4);
                    width.ReadData(es->I_O());
                    track->v_width = uint16(width);
                    fprintf(stdout, "matroska_reader: |   + Pixel width: "
                            "%u\n", track->v_width);

                  } else if (EbmlId(*l4) ==
                             KaxVideoPixelHeight::ClassInfos.GlobalId) {
                    KaxVideoPixelHeight &height =
                      *static_cast<KaxVideoPixelHeight *>(l4);
                    height.ReadData(es->I_O());
                    track->v_height = uint16(height);
                    fprintf(stdout, "matroska_reader: |   + Pixel height: "
                            "%u\n", track->v_height);

                  } else if (EbmlId(*l4) ==
                             KaxVideoFrameRate::ClassInfos.GlobalId) {
                    KaxVideoFrameRate &framerate =
                      *static_cast<KaxVideoFrameRate *>(l4);
                    framerate.ReadData(es->I_O());
                    track->v_frate = float(framerate);
                    fprintf(stdout, "matroska_reader: |   + Frame rate: "
                            "%f\n", float(framerate));

                  } else
                    fprintf(stdout, "matroska_reader: |   + unknown "
                            "element@4: %s\n", typeid(*l4).name());

                  if (upper_lvl_el > 0) {
									  assert(1 == 0);	// this should never happen
                  } else {
                    l4->SkipData(static_cast<EbmlStream &>(*es),
                                 l4->Generic().Context);
                    delete l4;
                    l4 = es->FindNextID(l3->Generic().Context, upper_lvl_el,
                                        0xFFFFFFFFL, true);
                  }
                } // while (l4 != NULL)

              } else if (EbmlId(*l3) == KaxCodecID::ClassInfos.GlobalId) {
                KaxCodecID &codec_id = *static_cast<KaxCodecID*>(l3);
                codec_id.ReadData(es->I_O());
                fprintf(stdout, "matroska_reader: |  + Codec ID: %s\n",
                        &binary(codec_id));
                track->codec_id = strdup((char *)&binary(codec_id));

              } else if (EbmlId(*l3) == KaxCodecPrivate::ClassInfos.GlobalId) {
                KaxCodecPrivate &c_priv = *static_cast<KaxCodecPrivate*>(l3);
                c_priv.ReadData(es->I_O());
                fprintf(stdout, "matroska_reader: |  + CodecPrivate, length "
                        "%llu\n", c_priv.GetSize());
                track->private_size = c_priv.GetSize();
                if (track->private_size > 0) {
                  track->private_data = malloc(track->private_size);
                  if (track->private_data == NULL)
                    return 0;
                  memcpy(track->private_data, c_priv.GetBuffer(),
                         track->private_size);
                }

              } else
                fprintf(stdout, "matroska_reader: |  + unknown element@3: "
                        "%s\n", typeid(*l3).name());
              if (upper_lvl_el > 0) {	// we're coming from l4
                upper_lvl_el--;
                delete l3;
                l3 = l4;
                if (upper_lvl_el > 0)
                  break;
              } else {
                l3->SkipData(static_cast<EbmlStream &>(*es),
                             l3->Generic().Context);
                delete l3;
                l3 = es->FindNextID(l2->Generic().Context, upper_lvl_el,
                                    0xFFFFFFFFL, true);
              }
            } // while (l3 != NULL)

          } else
            fprintf(stdout, "matroska_reader: | + unknown element@2: %s, "
                    "ule %d\n", typeid(*l2).name(), upper_lvl_el);
          if (upper_lvl_el > 0) {	// we're coming from l3
            upper_lvl_el--;
            delete l2;
            l2 = l3;
            if (upper_lvl_el > 0)
              break;
          } else {
            l2->SkipData(static_cast<EbmlStream &>(*es),
                         l2->Generic().Context);
            delete l2;
            l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el,
                                0xFFFFFFFFL, true);
          }
        } // while (l2 != NULL)

      } else if (EbmlId(*l1) == KaxCluster::ClassInfos.GlobalId) {
        fprintf(stdout, "matroska_reader: |+ found cluster, headers are "
                "parsed completely :)\n");
        saved_l1 = l1;
        exit_loop = 1;

      } else
        fprintf(stdout, "matroska_reader: |+ unknown element@1: %s, ule "
                "%d\n", typeid(*l1).name(), upper_lvl_el);
      
      if (exit_loop)      // we've found the first cluster, so get out
        break;

      if (upper_lvl_el > 0) {		// we're coming from l2
        upper_lvl_el--;
        delete l1;
        l1 = l2;
        if (upper_lvl_el > 0)
          break;
      } else {
        l1->SkipData(static_cast<EbmlStream &>(*es), l1->Generic().Context);
        delete l1;
        l1 = es->FindNextID(l0->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                            true);
      }
    } // while (l1 != NULL)
    
  } catch (exception &ex) {
    fprintf(stdout, "Error: matroska_reader: caught exception\n");
    return 0;
  }

  if (!exit_loop)               // We have NOT found a cluster!
    return 0;

  if (tc_scale == 0)
    tc_scale = MKVD_TIMECODESCALE;

  verify_tracks();

  return 1;
}

void mkv_reader_c::create_packetizers() {
  int i;
  mkv_track_t *t;
  char old_fourcc[5];

  for (i = 0; i < num_tracks; i++) {
    t = tracks[i];

    ti->private_data = (unsigned char *)t->private_data;
    ti->private_size = t->private_size;

    if (t->ok && demuxing_requested(t)) {
      switch (t->type) {

        case 'v':
          memcpy(old_fourcc, ti->fourcc, 5);
          if (ti->fourcc[0] == 0)
            memcpy(ti->fourcc, t->v_fourcc, 5);
          t->packetizer = new video_packetizer_c(t->v_frate, t->v_width,
                                                 t->v_height, 24, 1, ti);
          memcpy(ti->fourcc, old_fourcc, 5);
          break;

        case 'a':
          
          if (t->a_formattag == 0x0001)
            t->packetizer = new pcm_packetizer_c((unsigned long)t->a_sfreq,
                                                 t->a_channels, t->a_bps, ti);
          else if (t->a_formattag == 0x0055)
            t->packetizer = new mp3_packetizer_c((unsigned long)t->a_sfreq,
                                                 t->a_channels, ti);
          else if (t->a_formattag == 0x2000)
            t->packetizer = new ac3_packetizer_c((unsigned long)t->a_sfreq,
                                                 t->a_channels, ti);
          else if (t->a_formattag == 0xFFFE)
            t->packetizer = new vorbis_packetizer_c(t->headers[0],
                                                    t->header_sizes[0],
                                                    t->headers[1],
                                                    t->header_sizes[1],
                                                    t->headers[2],
                                                    t->header_sizes[2], ti);
          else {
            fprintf(stderr, "Error: matroska_reader: Unsupported track type "
                    "for track %d.\n", t->tnum);
            exit(1);
          }
          break;

        default:
          fprintf(stderr, "Error: matroska_reader: Unsupported track type "
                  "for track %d.\n", t->tnum);
          exit(1);
          break;
      }
    }
  }
}

// void mkv_reader_c::handle_block(KaxBlock &block) {
//                     pts = ((block.Timecod() + cluster_tc)) /
//                       tc_scale * ((float)MKVD_TIMECODESCALE / 1000.0);
// }

int mkv_reader_c::read() {
  mkv_track_t *t;
  int upper_lvl_el, exit_loop, found_data, i;
  // Elements for different levels
  EbmlElement *l0 = NULL, *l1 = NULL, *l2 = NULL, *l3 = NULL, *l4 = NULL;
  int64_t pts;

  if (num_tracks == 0)
    return 0;
  if (packets_available())
    return EMOREDATA;

  l0 = segment;

  if (saved_l1 == NULL) {
    printf("[mkv] No saved element left - error? End of stream?\n");
    return 0;
  }

  exit_loop = 0;
  upper_lvl_el = 0;
  l1 = saved_l1;
  saved_l1 = NULL;
  found_data = 0;
  try {
    while (l1 != NULL)  {
//       printf("[mkv] [fb] 1  : %s\n", typeid(*l1).name());
      if ((upper_lvl_el != 0) || exit_loop)
        break;

      if (EbmlId(*l1) == KaxCluster::ClassInfos.GlobalId) {
//         if (found_data) {
//           saved_l1 = l1;
//           break;
//         }
//         printf("[mkv] Cluster...\n");

        if (saved_l2 != NULL) {
          l2 = saved_l2;
          saved_l2 = NULL;
        } else
          l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                              true);
        while (l2 != NULL) {
//           printf("[mkv] [fb]  2 : %s\n", typeid(*l2).name());
          if (upper_lvl_el != 0)
            break;

          if ((found_data >= 1) && packets_available()) {
            saved_l2 = l2;
            saved_l1 = l1;
            exit_loop = 1;
            break;
          }

          if (EbmlId(*l2) == KaxClusterTimecode::ClassInfos.GlobalId) {
            KaxClusterTimecode &ctc = *static_cast<KaxClusterTimecode *>(l2);
            ctc.ReadData(es->I_O());
            cluster_tc = uint32(ctc);
//             printf("[mkv]  Cluster timecode: %u\n", cluster_tc);

          } else if (EbmlId(*l2) == KaxBlockGroup::ClassInfos.GlobalId) {
//             printf("[mkv]  Block group\n");

            l3 = es->FindNextID(l2->Generic().Context, upper_lvl_el,
                                0xFFFFFFFFL, false);
            while (l3 != NULL) {
//               printf("[mkv] [fb]   3: %s\n", typeid(*l3).name());
              if (upper_lvl_el > 0)
                break;

              if (EbmlId(*l3) == KaxBlock::ClassInfos.GlobalId) {
//                 printf("[mkv]   Block data\n");
                KaxBlock &block = *static_cast<KaxBlock *>(l3);
                block.ReadData(es->I_O());

//                 t = find_track_by_num(block.TrackNum());
                if ((t != NULL) && demuxing_requested(t)) {
                  for (i = 0; i < (int)block.NumberFrames(); i++) {
                    DataBuffer &data = block.GetBuffer(i);
//                     printf("[mkv] block %d tc: %10u\n", i, block.Timecod() +
//                            cluster_tc);
//                     handle_data(block);
                    found_data++;
                  }
                }
              } else if (!(EbmlId(*l3) ==
                           KaxBlockVirtual::ClassInfos.GlobalId))
                 printf("[mkv]   Uknown element@3: %s\n", typeid(*l3).name());

              if (upper_lvl_el > 0) {		// we're coming from l4
                upper_lvl_el--;
                delete l3;
                l3 = l4;
                if (upper_lvl_el > 0)
                  break;
              } else {
//                 calls++;
//                 printf("[mkv] [calls] %llu\n", calls);
                l3->SkipData(static_cast<EbmlStream &>(*es),
                             l3->Generic().Context);
                delete l3;
                l3 = es->FindNextID(l2->Generic().Context, upper_lvl_el,
                                    0xFFFFFFFFL, true);
//                 if (l3) printf("[mkv] [fb]?  3: %s\n", typeid(*l3).name());
              }
            } // while (l3 != NULL)
          }

          if (upper_lvl_el > 0) {		// we're coming from l3
            upper_lvl_el--;
            delete l2;
            l2 = l3;
            if (upper_lvl_el > 0)
              break;
          } else {
            l2->SkipData(static_cast<EbmlStream &>(*es),
                         l2->Generic().Context);
            delete l2;
            l2 = es->FindNextID(l1->Generic().Context, upper_lvl_el,
                                0xFFFFFFFFL, true);
//             if (l2) printf("[mkv] [fb]? 2 : %s\n", typeid(*l2).name());
          }
        } // while (l2 != NULL)
      } else
         printf("[mkv] Unknown element@1: %s\n", typeid(*l1).name());

      if (exit_loop)
        break;

      if (upper_lvl_el > 0) {		// we're coming from l2
        upper_lvl_el--;
        delete l1;
        l1 = l2;
        if (upper_lvl_el > 0)
          break;
      } else {
        l1->SkipData(static_cast<EbmlStream &>(*es), l1->Generic().Context);
        delete l1;
        l1 = es->FindNextID(l0->Generic().Context, upper_lvl_el, 0xFFFFFFFFL,
                            true);
//         if (l1) printf("[mkv] [fb]?1  : %s\n", typeid(*l1).name());
      }
    } // while (l1 != NULL)
  } catch (exception ex) {
    printf("[mkv] exception caught\n");
    return 0;
  }

  if (found_data)
    return 1;

  return 0;
}

/*
 * Checks whether the user wants a certain stream extracted or not.
 */
int mkv_reader_c::demuxing_requested(mkv_track_t *t) {
  unsigned char *tracks;
  int i;

  if (t->type == 'v')
    tracks = ti->vtracks;
  else if ((t->type == 'a') || (t->type == 'V'))
    tracks = ti->atracks;
  else if (t->type == 's')
    tracks = ti->stracks;
  else
    die("internal bug - unknown stream type");

  if (tracks == NULL)
    return 1;

  for (i = 0; i < strlen((char *)tracks); i++)
    if (tracks[i] == t->tnum)
      return 1;

  return 0;
}

packet_t *mkv_reader_c::get_packet() {
  generic_packetizer_c *winner;
  mkv_track_t *t;
  int i;
  
  winner = NULL;
  
  for (i = 0; i < num_tracks; i++) {
    t = tracks[i];
    if (winner == NULL) {
      if (t->packetizer->packet_available())
        winner = t->packetizer;
    } else if (winner->packet_available() &&
               (winner->get_smallest_timestamp() >
                t->packetizer->get_smallest_timestamp()))
      winner = t->packetizer;
  }
  
  if (winner != NULL)
    return winner->get_packet();
  else
    return NULL;
}

int mkv_reader_c::display_priority() {
  int i;
  
  for (i = 0; i < num_tracks; i++)
    if (tracks[i]->type == 'v')
      return DISPLAYPRIORITY_MEDIUM;

  return DISPLAYPRIORITY_LOW;
}

static char wchar[] = "-\\|/-\\|/-";

void mkv_reader_c::display_progress() {
  int i;

  for (i = 0; i < num_tracks; i++)  
    if (tracks[i]->type == 'v') {
      fprintf(stdout, "progress: %llu frames\r", tracks[i]->units_processed);
      fflush(stdout);
      return;
    }

  fprintf(stdout, "working... %c\r", wchar[act_wchar]);
  act_wchar++;
  if (act_wchar == strlen(wchar))
    act_wchar = 0;
  fflush(stdout);
}

