/*
 * Copyright (C) 2003-2013 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "PcmConvert.hxx"
#include "PcmChannels.hxx"
#include "AudioFormat.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Error.hxx"
#include "util/Domain.hxx"
#include "util/ConstBuffer.hxx"

#include <assert.h>
#include <math.h>

const Domain pcm_convert_domain("pcm_convert");

bool
pcm_convert_global_init(Error &error)
{
	return pcm_resample_global_init(error);
}

PcmConvert::PcmConvert()
{
#ifndef NDEBUG
	src_format.Clear();
	dest_format.Clear();
#endif
}

PcmConvert::~PcmConvert()
{
	assert(!src_format.IsValid());
	assert(!dest_format.IsValid());
}

bool
PcmConvert::Open(AudioFormat _src_format, AudioFormat _dest_format,
		 Error &error)
{
	assert(!src_format.IsValid());
	assert(!dest_format.IsValid());
	assert(_src_format.IsValid());
	assert(_dest_format.IsValid());

	src_format = _src_format;
	dest_format = _dest_format;

	AudioFormat format = src_format;
	if (format.format == SampleFormat::DSD)
		format.format = SampleFormat::FLOAT;

	if (format.format != dest_format.format &&
	    !format_converter.Open(format.format, dest_format.format, error))
		return false;
	format.format = dest_format.format;

	return true;
}

void
PcmConvert::Close()
{
	if (src_format.format != dest_format.format)
		format_converter.Close();

	dsd.Reset();
	resampler.Reset();

#ifndef NDEBUG
	src_format.Clear();
	dest_format.Clear();
#endif
}

inline ConstBuffer<int16_t>
PcmConvert::Convert16(ConstBuffer<int16_t> src, AudioFormat format,
		      Error &error)
{
	assert(format.format == SampleFormat::S16);
	assert(dest_format.format == SampleFormat::S16);

	auto buf = src.data;
	size_t len = src.size * sizeof(*src.data);

	if (format.channels != dest_format.channels) {
		buf = pcm_convert_channels_16(channels_buffer,
					      dest_format.channels,
					      format.channels,
					      buf, len, &len);
		if (buf == nullptr) {
			error.Format(pcm_convert_domain,
				     "Conversion from %u to %u channels "
				     "is not implemented",
				     format.channels,
				     dest_format.channels);
			return nullptr;
		}
	}

	if (format.sample_rate != dest_format.sample_rate) {
		buf = resampler.Resample16(dest_format.channels,
					   format.sample_rate, buf, len,
					   dest_format.sample_rate, &len,
					   error);
		if (buf == nullptr)
			return nullptr;
	}

	return ConstBuffer<int16_t>::FromVoid({buf, len});
}

inline ConstBuffer<int32_t>
PcmConvert::Convert24(ConstBuffer<int32_t> src, AudioFormat format,
		      Error &error)
{
	assert(format.format == SampleFormat::S24_P32);
	assert(dest_format.format == SampleFormat::S24_P32);

	auto buf = src.data;
	size_t len = src.size * sizeof(*src.data);

	if (format.channels != dest_format.channels) {
		buf = pcm_convert_channels_24(channels_buffer,
					      dest_format.channels,
					      format.channels,
					      buf, len, &len);
		if (buf == nullptr) {
			error.Format(pcm_convert_domain,
				     "Conversion from %u to %u channels "
				     "is not implemented",
				     format.channels,
				     dest_format.channels);
			return nullptr;
		}
	}

	if (format.sample_rate != dest_format.sample_rate) {
		buf = resampler.Resample24(dest_format.channels,
					   format.sample_rate, buf, len,
					   dest_format.sample_rate, &len,
					   error);
		if (buf == nullptr)
			return nullptr;
	}

	return ConstBuffer<int32_t>::FromVoid({buf, len});
}

inline ConstBuffer<int32_t>
PcmConvert::Convert32(ConstBuffer<int32_t> src, AudioFormat format,
		      Error &error)
{
	assert(format.format == SampleFormat::S32);
	assert(dest_format.format == SampleFormat::S32);

	auto buf = src.data;
	size_t len = src.size * sizeof(*src.data);

	if (format.channels != dest_format.channels) {
		buf = pcm_convert_channels_32(channels_buffer,
					      dest_format.channels,
					      format.channels,
					      buf, len, &len);
		if (buf == nullptr) {
			error.Format(pcm_convert_domain,
				     "Conversion from %u to %u channels "
				     "is not implemented",
				     format.channels,
				     dest_format.channels);
			return nullptr;
		}
	}

	if (format.sample_rate != dest_format.sample_rate) {
		buf = resampler.Resample32(dest_format.channels,
					   format.sample_rate, buf, len,
					   dest_format.sample_rate, &len,
					   error);
		if (buf == nullptr)
			return nullptr;
	}

	return ConstBuffer<int32_t>::FromVoid({buf, len});
}

inline ConstBuffer<float>
PcmConvert::ConvertFloat(ConstBuffer<float> src, AudioFormat format,
			 Error &error)
{
	assert(format.format == SampleFormat::FLOAT);
	assert(dest_format.format == SampleFormat::FLOAT);

	auto buffer = src.data;
	size_t size = src.size * sizeof(*src.data);

	/* convert channels */

	if (format.channels != dest_format.channels) {
		buffer = pcm_convert_channels_float(channels_buffer,
						    dest_format.channels,
						    format.channels,
						    buffer, size, &size);
		if (buffer == nullptr) {
			error.Format(pcm_convert_domain,
				     "Conversion from %u to %u channels "
				     "is not implemented",
				     format.channels,
				     dest_format.channels);
			return nullptr;
		}
	}

	/* resample with float, because this is the best format for
	   libsamplerate */

	if (format.sample_rate != dest_format.sample_rate) {
		buffer = resampler.ResampleFloat(dest_format.channels,
						 format.sample_rate,
						 buffer, size,
						 dest_format.sample_rate,
						 &size, error);
		if (buffer == nullptr)
			return nullptr;
	}

	return ConstBuffer<float>::FromVoid({buffer, size});
}

const void *
PcmConvert::Convert(const void *src, size_t src_size,
		    size_t *dest_size_r,
		    Error &error)
{
	ConstBuffer<void> buffer(src, src_size);
	AudioFormat format = src_format;

	if (format.format == SampleFormat::DSD) {
		auto s = ConstBuffer<uint8_t>::FromVoid(buffer);
		auto d = dsd.ToFloat(format.channels,
				     false, s);
		if (d.IsNull()) {
			error.Set(pcm_convert_domain,
				  "DSD to PCM conversion failed");
			return nullptr;
		}

		buffer = d.ToVoid();
		format.format = SampleFormat::FLOAT;
	}

	if (format.format != dest_format.format) {
		buffer = format_converter.Convert(buffer, error);
		if (buffer.IsNull())
			return nullptr;

		format.format = dest_format.format;
	}

	switch (dest_format.format) {
	case SampleFormat::S16:
		buffer = Convert16(ConstBuffer<int16_t>::FromVoid(buffer),
				   format, error).ToVoid();
		break;

	case SampleFormat::S24_P32:
		buffer = Convert24(ConstBuffer<int32_t>::FromVoid(buffer),
				   format, error).ToVoid();
		break;

	case SampleFormat::S32:
		buffer = Convert32(ConstBuffer<int32_t>::FromVoid(buffer),
				   format, error).ToVoid();
		break;

	case SampleFormat::FLOAT:
		buffer = ConvertFloat(ConstBuffer<float>::FromVoid(buffer),
				      format, error).ToVoid();
		break;

	default:
		error.Format(pcm_convert_domain,
			     "PCM conversion to %s is not implemented",
			     sample_format_to_string(dest_format.format));
		return nullptr;
	}

	*dest_size_r = buffer.size;
	return buffer.data;
}
