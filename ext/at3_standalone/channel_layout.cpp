/*
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * audio channel layout utility functions
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>

#include "channel_layout.h"
#include "compat.h"
#include "mathematics.h"

struct channel_name {
    const char *name;
    const char *description;
};

static const struct channel_name channel_names[] = {
   { "FL",        "front left"            },
   { "FR",        "front right"           },
   { "FC",        "front center"          },
   { "LFE",       "low frequency"         },
   { "BL",        "back left"             },
   { "BR",        "back right"            },
   { "FLC",       "front left-of-center"  },
   { "FRC",       "front right-of-center" },
   { "BC",        "back center"           },
   { "SL",        "side left"             },
   { "SR",        "side right"            },
   { "TC",        "top center"            },
   { "TFL",       "top front left"        },
   { "TFC",       "top front center"      },
   { "TFR",       "top front right"       },
   { "TBL",       "top back left"         },
   { "TBC",       "top back center"       },
   { "TBR",       "top back right"        },
   { "DL",        "downmix left"          },
   { "DR",        "downmix right"         },
   { "WL",        "wide left"             },
   { "WR",        "wide right"            },
   { "SDL",       "surround direct left"  },
   { "SDR",       "surround direct right" },
   { "LFE2",      "low frequency 2"       },
};

static const char *get_channel_name(int channel_id)
{
    if (channel_id < 0 || channel_id >= FF_ARRAY_ELEMS(channel_names))
        return NULL;
    return channel_names[channel_id].name;
}

static const struct {
    const char *name;
    int         nb_channels;
    uint64_t     layout;
} channel_layout_map[] = {
    { "mono",        1,  AV_CH_LAYOUT_MONO },
    { "stereo",      2,  AV_CH_LAYOUT_STEREO },
};

static uint64_t get_channel_layout_single(const char *name, int name_len)
{
    int i;
    char *end;
    int64_t layout;

    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++) {
        if (strlen(channel_layout_map[i].name) == name_len &&
            !memcmp(channel_layout_map[i].name, name, name_len))
            return channel_layout_map[i].layout;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(channel_names); i++)
        if (channel_names[i].name &&
            strlen(channel_names[i].name) == name_len &&
            !memcmp(channel_names[i].name, name, name_len))
            return (int64_t)1 << i;

    errno = 0;
    i = strtol(name, &end, 10);

    if (!errno && (end + 1 - name == name_len && *end  == 'c'))
        return av_get_default_channel_layout(i);

    errno = 0;
    layout = strtoll(name, &end, 0);
    if (!errno && end - name == name_len)
        return FFMAX(layout, 0);
    return 0;
}

uint64_t av_get_channel_layout(const char *name)
{
    const char *n, *e;
    const char *name_end = name + strlen(name);
    int64_t layout = 0, layout_single;

    for (n = name; n < name_end; n = e + 1) {
        for (e = n; e < name_end && *e != '+' && *e != '|'; e++);
        layout_single = get_channel_layout_single(n, e - n);
        if (!layout_single)
            return 0;
        layout |= layout_single;
    }
    return layout;
}

int av_get_channel_layout_nb_channels(uint64_t channel_layout)
{
    return av_popcount64(channel_layout);
}

int64_t av_get_default_channel_layout(int nb_channels) {
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(channel_layout_map); i++)
        if (nb_channels == channel_layout_map[i].nb_channels)
            return channel_layout_map[i].layout;
    return 0;
}

int av_get_channel_layout_channel_index(uint64_t channel_layout,
                                        uint64_t channel)
{
    if (!(channel_layout & channel) ||
        av_get_channel_layout_nb_channels(channel) != 1)
        return AVERROR(EINVAL);
    channel_layout &= channel - 1;
    return av_get_channel_layout_nb_channels(channel_layout);
}
