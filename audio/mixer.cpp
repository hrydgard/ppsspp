#include <string.h>

#include "audio/mixer.h"
#include "audio/wav_read.h"
#include "base/logging.h"
#include "ext/stb_vorbis/stb_vorbis.h"
#include "file/vfs.h"

// TODO:
// * Vorbis streaming playback

struct ChannelEffectState {
	// Filter state
};

enum CLIP_TYPE {
	CT_PCM16,
	CT_SYNTHFX,
	CT_VORBIS,
	// CT_PHOENIX?
};

struct Clip {
	int type;

	short *data;
	int length;
	int num_channels; // this is NOT stereo vs mono
	int sample_rate;
	int loop_start;
	int loop_end;
};

// If current_clip == 0, the channel is free.

enum ClipPlaybackState {
	PB_STOPPED = 0,
	PB_PLAYING = 1,
};


struct Channel {
	const Clip *current_clip;
	// Playback state
	ClipPlaybackState state;
	int pos;
	PlayParams params;
	// Effect state
	ChannelEffectState effect_state;
};

struct Mixer {
	Channel *channels;
	int sample_rate;
	int num_channels;
	int num_fixed_channels;
};

Mixer *mixer_create(int sample_rate, int channels, int fixed_channels) {
	Mixer *mixer = new Mixer();
	memset(mixer, 0, sizeof(Mixer));
	mixer->channels = new Channel[channels];
	memset(mixer->channels, 0, sizeof(Channel) * channels);
	mixer->sample_rate = sample_rate;
	mixer->num_channels = channels;
	mixer->num_fixed_channels = fixed_channels;
	return mixer;
}

void mixer_destroy(Mixer *mixer) {
	delete [] mixer->channels;
	delete mixer;
}

static int get_free_channel(Mixer *mixer) {
	int chan_with_biggest_pos = -1;
	int biggest_pos = -1;
	for (int i = mixer->num_fixed_channels; i < mixer->num_channels; i++) {
		Channel *chan = &mixer->channels[i];
		if (!chan->current_clip) {
			return i;
		}
		if (chan->pos > biggest_pos) {
			biggest_pos = chan->pos;
			chan_with_biggest_pos = i;
		}
	}
	return chan_with_biggest_pos;
}

Clip *clip_load(const char *filename) {
	short *data;
	int num_samples;
	int sample_rate, num_channels;

	if (!strcmp(filename + strlen(filename) - 4, ".ogg")) {
		// Ogg file. For now, directly decompress, no streaming support.
		uint8_t *filedata;
		size_t size;
		filedata = VFSReadFile(filename, &size);
		num_samples = (int)stb_vorbis_decode_memory(filedata, size, &num_channels, &data);
		if (num_samples <= 0)
			return NULL;
		sample_rate = 44100;
		ILOG("read ogg %s, length %i, rate %i", filename, num_samples, sample_rate);
	} else {
		// Wav file. Easy peasy.
		data = wav_read(filename, &num_samples, &sample_rate, &num_channels);
		if (!data) {
			return NULL;
		}
	}

	Clip *clip = new Clip();
	clip->type = CT_PCM16;
	clip->data = data;
	clip->length = num_samples;
	clip->num_channels = num_channels;
	clip->sample_rate = sample_rate;
	clip->loop_start = 0;
	clip->loop_end = 0;
	return clip;
}

void clip_destroy(Clip *clip) {
	if (clip) {
		free(clip->data);
		delete clip;
	} else {
		ELOG("Can't destroy zero clip");
	}
}

const short *clip_data(const Clip *clip)
{
	return clip->data;
}

size_t clip_length(const Clip *clip) {
	return clip->length;
}

void clip_set_loop(Clip *clip, int start, int end) {
	clip->loop_start = start;
	clip->loop_end = end;
}

PlayParams *mixer_play_clip(Mixer *mixer, const Clip *clip, int channel) {
	if (channel == -1) {
		channel = get_free_channel(mixer);
	}

	Channel *chan = &mixer->channels[channel];
	// Think about this order and make sure it's thread"safe" (not perfect but should not cause crashes).
	chan->pos = 0;
	chan->current_clip = clip;
	chan->state = PB_PLAYING;
	PlayParams *params = &chan->params;
	params->volume = 128;
	params->pan = 128;
	return params;
}

void mixer_mix(Mixer *mixer, short *buffer, int num_samples) {
	// Clear the buffer.
	memset(buffer, 0, num_samples * sizeof(short) * 2);
	for (int i = 0; i < mixer->num_channels; i++) {
		Channel *chan = &mixer->channels[i];
		if (chan->state == PB_PLAYING) {
			const Clip *clip = chan->current_clip;
			if (clip->type == CT_PCM16) {
				// For now, only allow mono PCM
				CHECK(clip->num_channels == 1);
				if (true || chan->params.delta == 0) {
					// Fast playback of non pitched clips
					int cnt = num_samples;
					if (clip->length - chan->pos < cnt) {
						cnt = clip->length - chan->pos;
					}
					// TODO: Take pan into account.
					int left_volume = chan->params.volume;
					int right_volume = chan->params.volume;
					// TODO: NEONize. Can also make special loops for left_volume == right_volume etc.
					for (int s = 0; s < cnt; s++) {
						int cdata = clip->data[chan->pos];
						buffer[s * 2 + 0] += cdata * left_volume >> 8;
						buffer[s * 2 + 1] += cdata * right_volume >> 8;
						chan->pos++;
					}
					if (chan->pos >= clip->length) {
						chan->state = PB_STOPPED;
						chan->current_clip = 0;
						break;
					}
				}
			} else if (clip->type == CT_VORBIS) {
				// For music
			}
		}
	}
}
