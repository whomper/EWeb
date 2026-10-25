/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dom/bindings/xml/xmlparser.h - libdom's XML parser, as libsvgtiny
 * calls it, made of EWeb's HTML parser (dom.c): the file is gathered and
 * parsed when it is complete, an <svg> in it found as a page's is.
 */
#ifndef EW_SVG_XMLPARSER_H
#define EW_SVG_XMLPARSER_H

#include <stddef.h>
#include <stdint.h>

#include "dom/dom.h"

typedef enum { DOM_XML_OK = 0, DOM_XML_NOMEM = 1, DOM_XML_FAILED = 2 } dom_xml_error;
typedef void (*dom_msg)(uint32_t severity, void *ctx, const char *msg, ...);
typedef struct dom_xml_parser dom_xml_parser;

dom_xml_parser *dom_xml_parser_create(const char *enc, const char *int_enc, dom_msg msg, void *mctx,
				      dom_document **document);
dom_xml_error dom_xml_parser_parse_chunk(dom_xml_parser *p, const uint8_t *data, size_t len);
dom_xml_error dom_xml_parser_completed(dom_xml_parser *p);
void dom_xml_parser_destroy(dom_xml_parser *p);

#endif
