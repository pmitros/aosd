/*
 *   ALSA/XOSD Volume Bar
 *
 *   Based on amixer source (by Jeroslav Kysela), and using xosd 
 *     (by Andre Renaud). 
 *
 *   What's not in amixer or xosd by Piotr Mitros
 *
 *   Copyright (c) 1999-2002 by Piotr Mitros, Jaroslav Kysela 
 *	<perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include <alsa/asoundlib.h>
#include <sys/poll.h>

#include <xosd.h>

#define LEVEL_BASIC		(1<<0)
#define LEVEL_INACTIVE		(1<<1)
#define LEVEL_ID		(1<<2)

int debugflag = 0;
char *card = "default";

xosd *osd_top;
xosd *osd_bottom;

int postinit;

snd_mixer_t *handle;

static void error(const char *fmt,...)
{
  va_list va;

  va_start(va, fmt);
  fprintf(stderr, "amixer: ");
  vfprintf(stderr, fmt, va);
  fprintf(stderr, "\n");
  va_end(va);
}

/* Fuction to convert from volume to percentage. val = volume */
static int convert_prange(int val, int min, int max)
{
  int range = max - min;
  int tmp;

  if (range == 0)
    return 0;
  val -= min;
  tmp = rint((double)val/(double)range * 100);
  return tmp;
}

static const char *get_percent(int val, int min, int max)
{
  static char str[32];
  int p;
	
  p = convert_prange(val, min, max);
  sprintf(str, "%i [%i%%]", val, p);
  return str;
}

static int show_selem(snd_mixer_selem_id_t *id, const char *space, int level)
{
  snd_mixer_selem_channel_id_t chn;
  long pmin = 0, pmax = 0;
  long pvol;
  int psw, csw;
  int pmono, mono_ok = 0;
  snd_mixer_elem_t *elem;

  /* Sum of all volumes, all maxima, all minima */
  int ptotvol=0, ptotmax=0, ptotmin=0; 
  /* For determining mute, how many ons and offs we got */
  int ons=0, offs=0; 

  elem = snd_mixer_find_selem(handle, id);
  if (!elem) {
    error("Mixer %s simple element not found", card);
    return -ENOENT;
  }

  printf("Name: %s\n", snd_mixer_selem_id_get_name(id));

  if (snd_mixer_selem_has_playback_volume(elem)) {
    printf("%sLimits: ", space);
    if (snd_mixer_selem_has_common_volume(elem)) {
      snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);
      printf("%li - %li", pmin, pmax);
    } else if (snd_mixer_selem_has_playback_volume(elem)) {
      snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);
      printf("Playback %li - %li ", pmin, pmax);
    }
    printf("\n");
  }
  pmono = snd_mixer_selem_has_playback_channel(elem, SND_MIXER_SCHN_MONO) &&
    (snd_mixer_selem_is_playback_mono(elem) || 
     (!snd_mixer_selem_has_playback_volume(elem) &&
      !snd_mixer_selem_has_playback_switch(elem)));
  if (pmono) {
    if (!mono_ok) {
      printf("%s%s: ", space, "Mono");
      mono_ok = 1;
    }
    if (snd_mixer_selem_has_common_volume(elem)) {
      snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &pvol);
      printf("%s ", get_percent(pvol, pmin, pmax));
      ptotvol+=pvol; ptotmin+=pmin; ptotmax+=pmax;
    }
    if (snd_mixer_selem_has_common_switch(elem)) {
      snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &psw);
      printf("[%s] ", psw ? "on" : "off");
      ons+=(psw==1); offs+=(psw==0);
    }
  }
  if (pmono && snd_mixer_selem_has_playback_channel(elem, SND_MIXER_SCHN_MONO)) {
    int title = 0;
    if (!mono_ok) {
      printf("%s%s: ", space, "Mono");
      mono_ok = 1;
    }
    if (!snd_mixer_selem_has_common_volume(elem)) {
      if (snd_mixer_selem_has_playback_volume(elem)) {
	printf("Playback ");
	title = 1;
	snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &pvol);
	printf("%s ", get_percent(pvol, pmin, pmax));
	ptotvol+=pvol; ptotmin+=pmin; ptotmax+=pmax;
      }
    }
    if (!snd_mixer_selem_has_common_switch(elem)) {
      if (snd_mixer_selem_has_playback_switch(elem)) {
	if (!title)
	  printf("Playback ");
	snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &psw);
	printf("[%s] ", psw ? "on" : "off");
	ons+=(psw==1); offs+=(psw==0);
      }
    }
  }
  if (pmono)
    printf("\n");
  if (!pmono) {
    for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++) {
      if (pmono || !snd_mixer_selem_has_playback_channel(elem, chn))
	continue;
      printf("%s%s: ", space, snd_mixer_selem_channel_name(chn));
      if (!pmono && snd_mixer_selem_has_common_volume(elem)) {
	snd_mixer_selem_get_playback_volume(elem, chn, &pvol);
	printf("%s ", get_percent(pvol, pmin, pmax));
	ptotvol+=pvol; ptotmin+=pmin; ptotmax+=pmax;
      }
      if (!pmono && snd_mixer_selem_has_common_switch(elem)) {
	snd_mixer_selem_get_playback_switch(elem, chn, &psw);
	printf("[%s] ", psw ? "on" : "off");
	ons+=(psw==1); offs+=(psw==0);
      }
      if (!pmono && snd_mixer_selem_has_playback_channel(elem, chn)) {
	int title = 0;
	if (!snd_mixer_selem_has_common_volume(elem)) {
	  if (snd_mixer_selem_has_playback_volume(elem)) {
	    printf("Playback ");
	    title = 1;
	    snd_mixer_selem_get_playback_volume(elem, chn, &pvol);
	    printf("%s ", get_percent(pvol, pmin, pmax));
	    ptotvol+=pvol; ptotmin+=pmin; ptotmax+=pmax;
	  }
	}
	if (!snd_mixer_selem_has_common_switch(elem)) {
	  if (snd_mixer_selem_has_playback_switch(elem)) {
	    if (!title)
	      printf("Playback ");
	    snd_mixer_selem_get_playback_switch(elem, chn, &psw);
	    printf("[%s] ", psw ? "on" : "off");
	    ons+=(psw==1); offs+=(psw==0);
	  }
	}
      }
      printf("\n");
    }
  }

  printf("Ons: %i, Offs: %i\n", ons, offs);
  printf("Volume: %i [%i/%i]\n", ptotvol, ptotmax, ptotmin);

  /* For safety, let's make sure something got set; 
     also only do it post-init */
  if((ons||offs||ptotvol||ptotmax||ptotmin) && postinit) {
    xosd_display(osd_top, 0, XOSD_string, snd_mixer_selem_id_get_name(id));
    if((offs>0)&&(ons==0))
       xosd_display(osd_top, 1, XOSD_string, "Mute");
    if((offs>0)&&(ons>0))
       xosd_display(osd_top, 1, XOSD_string, "Some Channels Mute");
    if((offs==0)&&(ons>0))
       xosd_display(osd_top, 1, XOSD_string, "");

    if(ptotmin!=ptotmax) {
      int percent=((ptotvol-ptotmin)*100)/(ptotmax-ptotmin);
      xosd_display(osd_bottom, 0, XOSD_percentage, percent);
    }
  }

  return 0;
}

static void sevents_value(snd_mixer_selem_id_t *sid)
{
  show_selem(sid, "  ", 1);
}

static void sevents_info(snd_mixer_selem_id_t *sid)
{
  printf("event info: '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
}

static void sevents_remove(snd_mixer_selem_id_t *sid)
{
  printf("event remove: '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
}

int melem_event(snd_mixer_elem_t *elem, unsigned int mask)
{
  snd_mixer_selem_id_t *sid;
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_get_id(elem, sid);
  if (mask == SND_CTL_EVENT_MASK_REMOVE) {
    sevents_remove(sid);
    return 0;
  }
  if (mask & SND_CTL_EVENT_MASK_INFO) 
    sevents_info(sid);
  if (mask & SND_CTL_EVENT_MASK_VALUE) 
    sevents_value(sid);
  return 0;
}

static void sevents_add(snd_mixer_elem_t *elem)
{
  snd_mixer_selem_id_t *sid;
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_get_id(elem, sid);
  printf("event add: '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
  snd_mixer_elem_set_callback(elem, melem_event);
}

int mixer_event(snd_mixer_t *mixer, unsigned int mask,
		snd_mixer_elem_t *elem)
{
  if (mask & SND_CTL_EVENT_MASK_ADD)
    sevents_add(elem);
  return 0;
}

static int sevents()
{
  int err;

  if ((err = snd_mixer_open(&handle, 0)) < 0) {
    error("Mixer %s open error: %s", card, snd_strerror(err));
    return err;
  }
  if ((err = snd_mixer_attach(handle, card)) < 0) {
    error("Mixer attach %s error: %s", card, snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }
  if ((err = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
    error("Mixer register error: %s", snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }
  snd_mixer_set_callback(handle, mixer_event);
  err = snd_mixer_load(handle);
  if (err < 0) {
    error("Mixer load error: %s", card, snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }

  printf("Ready to listen...\n");
  while (1) {
    int res;
    res = snd_mixer_wait(handle, -1);
    if (res >= 0) {
      printf("Poll ok: %i\n", res);
      postinit=1;
      res = snd_mixer_handle_events(handle);
      assert(res >= 0);
    }
  }
  snd_mixer_close(handle);
}

int main(int argc, char *argv[])
{
  int morehelp, level = 0;

  osd_top = xosd_init ("fixed", "LawnGreen", 3, XOSD_top, 0, 1);
  osd_bottom = xosd_init ("fixed", "LawnGreen", 3, XOSD_bottom, 0, 1);
	
  sevents();

  xosd_uninit (osd_top);
  xosd_uninit (osd_bottom);

  return 0;
}
