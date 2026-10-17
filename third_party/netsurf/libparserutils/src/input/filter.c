/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef WITHOUT_ICONV_FILTER
#include <iconv.h>
#endif

#include <parserutils/charset/mibenum.h>
#include <parserutils/charset/codec.h>

#include "charset/codecs/codec_impl.h"
#include "input/filter.h"
#include "utils/utils.h"

/** Input filter */
struct parserutils_filter {
#ifndef WITHOUT_ICONV_FILTER
	iconv_t cd;			/**< Iconv conversion descriptor */
	uint16_t int_enc;		/**< The internal encoding */
#else
	parserutils_charset_codec *read_codec;	/**< Read codec */
	parserutils_charset_codec *write_codec;	/**< Write codec */

	uint32_t pivot_buf[64];		/**< Conversion pivot buffer */

	bool leftover;			/**< Data remains from last call */
	uint8_t *pivot_left;		/**< Remaining pivot to write */
	size_t pivot_len;		/**< Length of pivot remaining */
#endif

	bool utf8_out;			/**< The document is wanted in UTF-8 */
	bool utf8;			/**< And it is in UTF-8: copy it */
	uint8_t part[4];		/**< Sequence cut short by a chunk */
	uint8_t part_len;		/**< Its length in bytes */
	uint8_t need;			/**< Bytes it still needs */
	uint8_t lo, hi;			/**< Range of the next of them */

	struct {
		uint16_t encoding;	/**< Input encoding */
	} settings;			/**< Filter settings */
};

static parserutils_error filter_set_defaults(parserutils_filter *input);
static parserutils_error filter_set_encoding(parserutils_filter *input,
		const char *enc);
static uint16_t filter_utf8_mibenum(void);
static parserutils_error filter_utf8(parserutils_filter *input,
		const uint8_t **data, size_t *len,
		uint8_t **output, size_t *outlen);

/**
 * Create an input filter
 *
 * \param int_enc  Desired encoding of document
 * \param filter   Pointer to location to receive filter instance
 * \return PARSERUTILS_OK on success,
 *         PARSERUTILS_BADPARM on bad parameters,
 *         PARSERUTILS_NOMEM on memory exhausion,
 *         PARSERUTILS_BADENCODING if the encoding is unsupported
 */
parserutils_error parserutils__filter_create(const char *int_enc,
		parserutils_filter **filter)
{
	parserutils_filter *f;
	parserutils_error error;

	if (int_enc == NULL || filter == NULL)
		return PARSERUTILS_BADPARM;

	f = malloc(sizeof(parserutils_filter));
	if (f == NULL)
		return PARSERUTILS_NOMEM;

#ifndef WITHOUT_ICONV_FILTER
	f->cd = (iconv_t) -1;
	f->int_enc = parserutils_charset_mibenum_from_name(
			int_enc, strlen(int_enc));
	if (f->int_enc == 0) {
		free(f);
		return PARSERUTILS_BADENCODING;
	}
	f->utf8_out = f->int_enc == filter_utf8_mibenum();
#else
	f->leftover = false;
	f->pivot_left = NULL;
	f->pivot_len = 0;
	f->utf8_out = false;	/* until the write codec is made, below */
#endif

	f->utf8 = false;
	f->part_len = 0;
	f->need = 0;

	error = filter_set_defaults(f);
	if (error != PARSERUTILS_OK) {
		free(f);
		return error;
	}

#ifdef WITHOUT_ICONV_FILTER
	error = parserutils_charset_codec_create(int_enc, &f->write_codec);
	if (error != PARSERUTILS_OK) {
		if (f->read_codec != NULL) {
			parserutils_charset_codec_destroy(f->read_codec);
			f->read_codec = NULL;
		}
		free(f);
		return error;
	}

	f->utf8_out = f->write_codec->mibenum == filter_utf8_mibenum();
	f->utf8 = f->utf8_out &&
			f->settings.encoding == filter_utf8_mibenum();
#endif

	*filter = f;

	return PARSERUTILS_OK;
}

/**
 * Destroy an input filter
 *
 * \param input  Pointer to filter instance
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils__filter_destroy(parserutils_filter *input)
{
	if (input == NULL)
		return PARSERUTILS_BADPARM;

#ifndef WITHOUT_ICONV_FILTER
	if (input->cd != (iconv_t) -1) {
		iconv_close(input->cd);
		input->cd = (iconv_t) -1;
	}
#else
	if (input->read_codec != NULL) {
		parserutils_charset_codec_destroy(input->read_codec);
		input->read_codec = NULL;
	}

	if (input->write_codec != NULL) {
		parserutils_charset_codec_destroy(input->write_codec);
		input->write_codec = NULL;
	}
#endif

	free(input);

	return PARSERUTILS_OK;
}

/**
 * Configure an input filter
 *
 * \param input   Pointer to filter instance
 * \param type    Input option type to configure
 * \param params  Option-specific parameters
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils__filter_setopt(parserutils_filter *input,
		parserutils_filter_opttype type,
		parserutils_filter_optparams *params)
{
	parserutils_error error = PARSERUTILS_OK;

	if (input == NULL || params == NULL)
		return PARSERUTILS_BADPARM;

	switch (type) {
	case PARSERUTILS_FILTER_SET_ENCODING:
		error = filter_set_encoding(input, params->encoding.name);
		break;
	}

	return error;
}

/**
 * Process a chunk of data
 *
 * \param input   Pointer to filter instance
 * \param data    Pointer to pointer to input buffer
 * \param len     Pointer to length of input buffer
 * \param output  Pointer to pointer to output buffer
 * \param outlen  Pointer to length of output buffer
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 *
 * Call this with an input buffer length of 0 to flush any buffers.
 */
parserutils_error parserutils__filter_process_chunk(parserutils_filter *input,
		const uint8_t **data, size_t *len,
		uint8_t **output, size_t *outlen)
{
	if (input == NULL || data == NULL || *data == NULL || len == NULL ||
			output == NULL || *output == NULL || outlen == NULL)
		return PARSERUTILS_BADPARM;

	if (input->utf8)
		return filter_utf8(input, data, len, output, outlen);

#ifndef WITHOUT_ICONV_FILTER
	if (iconv(input->cd, (void *) data, len, 
			(char **) output, outlen) == (size_t) -1) {
		switch (errno) {
		case E2BIG:
			return PARSERUTILS_NOMEM;
		case EILSEQ:
			if (*outlen < 3)
				return PARSERUTILS_NOMEM;

			(*output)[0] = 0xef;
			(*output)[1] = 0xbf;
			(*output)[2] = 0xbd;

			*output += 3;
			*outlen -= 3;

			(*data)++;
			(*len)--;

			while (*len > 0) {
				size_t ret;
				
				ret = iconv(input->cd, (void *) data, len, 
						(char **) output, outlen);
				if (ret != (size_t) -1 || errno != EILSEQ)
					break;

				if (*outlen < 3)
					return PARSERUTILS_NOMEM;

				(*output)[0] = 0xef;
				(*output)[1] = 0xbf;
				(*output)[2] = 0xbd;

				*output += 3;
				*outlen -= 3;

				(*data)++;
				(*len)--;
			}

			return errno == E2BIG ? PARSERUTILS_NOMEM 
					      : PARSERUTILS_OK;
		}
	}

	return PARSERUTILS_OK;
#else
	if (input->leftover) {
		parserutils_error write_error;

		/* Some data left to be written from last call */

		/* Attempt to flush the remaining data. */
		write_error = parserutils_charset_codec_encode(
				input->write_codec,
				(const uint8_t **) &input->pivot_left,
				&input->pivot_len,
				output, outlen);

		if (write_error != PARSERUTILS_OK)
			return write_error;


		/* And clear leftover */
		input->pivot_left = NULL;
		input->pivot_len = 0;
		input->leftover = false;
	}

	/* With no input, this still runs once: the read codec may hold a
	 * character it decoded when the pivot buffer was full */
	do {
		parserutils_error read_error, write_error;
		size_t pivot_len = sizeof(input->pivot_buf);
		uint8_t *pivot = (uint8_t *) input->pivot_buf;

		read_error = parserutils_charset_codec_decode(input->read_codec,
				data, len,
				(uint8_t **) &pivot, &pivot_len);

		pivot = (uint8_t *) input->pivot_buf;
		pivot_len = sizeof(input->pivot_buf) - pivot_len;

		if (pivot_len > 0) {
			write_error = parserutils_charset_codec_encode(
					input->write_codec,
					(const uint8_t **) &pivot,
					&pivot_len,
					output, outlen);

			if (write_error != PARSERUTILS_OK) {
				input->leftover = true;
				input->pivot_left = pivot;
				input->pivot_len = pivot_len;

				return write_error;
			}
		}

		if (read_error != PARSERUTILS_OK && 
				read_error != PARSERUTILS_NOMEM)
			return read_error;
	} while (*len > 0);

	return PARSERUTILS_OK;
#endif
}

/**
 * Reset an input filter's state
 *
 * \param input  The input filter to reset
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils__filter_reset(parserutils_filter *input)
{
	parserutils_error error = PARSERUTILS_OK;

	if (input == NULL)
		return PARSERUTILS_BADPARM;

	input->part_len = 0;
	input->need = 0;

#ifndef WITHOUT_ICONV_FILTER
	iconv(input->cd, NULL, 0, NULL, 0);
#else
	/* Clear pivot buffer leftovers */
	input->pivot_left = NULL;
	input->pivot_len = 0;
	input->leftover = false;

	/* Reset read codec */
	error = parserutils_charset_codec_reset(input->read_codec);
	if (error != PARSERUTILS_OK)
		return error;

	/* Reset write codec */
	error = parserutils_charset_codec_reset(input->write_codec);
	if (error != PARSERUTILS_OK)
		return error;
#endif

	return error;
}

/**
 * Set an input filter's default settings
 *
 * \param input  Input filter to configure
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error filter_set_defaults(parserutils_filter *input)
{
	parserutils_error error;

	if (input == NULL)
		return PARSERUTILS_BADPARM;

#ifdef WITHOUT_ICONV_FILTER
	input->read_codec = NULL;
	input->write_codec = NULL;
#endif

	input->settings.encoding = 0;
	error = filter_set_encoding(input, "UTF-8");
	if (error != PARSERUTILS_OK)
		return error;

	return PARSERUTILS_OK;
}

/**
 * Set an input filter's encoding
 *
 * \param input  Input filter to configure
 * \param enc    Encoding name
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error filter_set_encoding(parserutils_filter *input,
		const char *enc)
{
	parserutils_error error = PARSERUTILS_OK;
	uint16_t mibenum;

	if (input == NULL || enc == NULL)
		return PARSERUTILS_BADPARM;

	mibenum = parserutils_charset_mibenum_from_name(enc, strlen(enc));
	if (mibenum == 0)
		return PARSERUTILS_BADENCODING;

	/* Exit early if we're already using this encoding */
	if (input->settings.encoding == mibenum)
		return PARSERUTILS_OK;

#ifndef WITHOUT_ICONV_FILTER
	if (input->cd != (iconv_t) -1) {
		iconv_close(input->cd);
		input->cd = (iconv_t) -1;
	}

	input->cd = iconv_open(
		parserutils_charset_mibenum_to_name(input->int_enc),
		parserutils_charset_mibenum_to_name(mibenum));
	if (input->cd == (iconv_t) -1) {
		return (errno == EINVAL) ? PARSERUTILS_BADENCODING
					 : PARSERUTILS_NOMEM;
	}
#else
	if (input->read_codec != NULL) {
		parserutils_charset_codec_destroy(input->read_codec);
		input->read_codec = NULL;
	}

	error = parserutils_charset_codec_create(enc, &input->read_codec);
	if (error != PARSERUTILS_OK)
		return error;
#endif

	input->settings.encoding = mibenum;

	input->utf8 = input->utf8_out && mibenum == filter_utf8_mibenum();
	input->part_len = 0;
	input->need = 0;

	return error;

}

/**
 * UTF-8's MIB enum, looked up once
 */
uint16_t filter_utf8_mibenum(void)
{
	static uint16_t utf8;

	if (utf8 == 0)
		utf8 = parserutils_charset_mibenum_from_name("UTF-8",
				SLEN("UTF-8"));

	return utf8;
}

/**
 * Append U+REPLACEMENT CHARACTER, in UTF-8
 */
static inline uint8_t *filter_fffd(uint8_t *o)
{
	o[0] = 0xef;
	o[1] = 0xbf;
	o[2] = 0xbd;

	return o + 3;
}

/**
 * Process a chunk of UTF-8 for UTF-8 output, by copying it
 *
 * \param input   Pointer to filter instance
 * \param data    Pointer to pointer to input buffer
 * \param len     Pointer to length of input buffer
 * \param output  Pointer to pointer to output buffer
 * \param outlen  Pointer to length of output buffer
 * \return PARSERUTILS_OK on success,
 *         PARSERUTILS_NOMEM if the output buffer filled first
 *
 * Decoding UTF-8 to UCS-4 and encoding it again costs dearly for what it
 * does. This copies what is UTF-8 and replaces what isn't as the WHATWG
 * Encoding Standard's UTF-8 decoder does: each byte that can't start a
 * sequence, and each sequence that ends too soon (at an unexpected byte,
 * or at the end of the input), becomes one U+FFFD. Overlong forms,
 * surrogates and code points beyond U+10FFFF are caught by the range of a
 * sequence's second byte. A sequence split between chunks is held over;
 * calling this with no input (at the end of the input) writes it out as
 * U+FFFD.
 */
parserutils_error filter_utf8(parserutils_filter *input,
		const uint8_t **data, size_t *len,
		uint8_t **output, size_t *outlen)
{
	const uint8_t *s = *data;
	const uint8_t *end = s + *len;
	uint8_t *o = *output;
	uint8_t *oend = o + *outlen;
	parserutils_error error = PARSERUTILS_OK;

	/* Room for the longest thing one step writes: a character or
	 * U+FFFD */
#define ROOM (oend - o >= 4)

	if (s == end) {
		/* The end of the input: a sequence cut short by it */
		if (input->need > 0) {
			if (oend - o < 3)
				return PARSERUTILS_NOMEM;

			o = filter_fffd(o);
			input->need = 0;
			input->part_len = 0;
		}
	} else if (input->need > 0) {
		/* Finish the sequence the last chunk ended in */
		if (!ROOM)
			return PARSERUTILS_NOMEM;

		while (input->need > 0 && s < end) {
			uint8_t b = *s;

			if (b < input->lo || b > input->hi) {
				/* Read b again, below */
				o = filter_fffd(o);
				input->need = 0;
				input->part_len = 0;
				break;
			}

			input->part[input->part_len++] = b;
			s++;
			input->lo = 0x80;
			input->hi = 0xbf;

			if (--input->need == 0) {
				memcpy(o, input->part, input->part_len);
				o += input->part_len;
				input->part_len = 0;
			}
		}
	}

	while (s < end) {
		uint8_t b = *s;
		uint8_t lo = 0x80, hi = 0xbf;
		size_t n, i;

		if (b < 0x80) {
			/* Most of a page: a run of ASCII */
			n = min((size_t) (end - s), (size_t) (oend - o));
			if (n == 0) {
				error = PARSERUTILS_NOMEM;
				break;
			}

			do {
				*o++ = b;
				s++;
			} while (--n > 0 && (b = *s) < 0x80);

			continue;
		}

		if (!ROOM) {
			error = PARSERUTILS_NOMEM;
			break;
		}

		if (b >= 0xc2 && b <= 0xdf) {
			n = 2;
		} else if (b >= 0xe0 && b <= 0xef) {
			n = 3;
			if (b == 0xe0)
				lo = 0xa0;
			else if (b == 0xed)
				hi = 0x9f;
		} else if (b >= 0xf0 && b <= 0xf4) {
			n = 4;
			if (b == 0xf0)
				lo = 0x90;
			else if (b == 0xf4)
				hi = 0x8f;
		} else {
			/* Can't start a sequence */
			o = filter_fffd(o);
			s++;
			continue;
		}

		for (i = 1; i < n; i++) {
			uint8_t c;

			if (s + i == end) {
				/* Cut short by the chunk: hold it over */
				memcpy(input->part, s, i);
				input->part_len = i;
				input->need = n - i;
				input->lo = lo;
				input->hi = hi;
				s = end;
				break;
			}

			c = s[i];
			if (c < lo || c > hi) {
				/* Read c again */
				o = filter_fffd(o);
				s += i;
				break;
			}

			lo = 0x80;
			hi = 0xbf;
		}

		if (i == n) {
			o[0] = b;
			o[1] = s[1];
			if (n > 2) {
				o[2] = s[2];
				if (n > 3)
					o[3] = s[3];
			}
			o += n;
			s += n;
		}
	}

#undef ROOM

	*len -= s - *data;
	*data = s;
	*outlen -= o - *output;
	*output = o;

	return error;
}
