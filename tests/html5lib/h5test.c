/* SPDX-License-Identifier: MIT
 * Copyright (C) 2007-2009 the Hubbub authors (John-Mark Bell and others)
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Hubbub against the HTML tree-construction tests (html5lib's format, kept
 * now by the Web Platform Tests in html/syntax/parsing/resources).
 *
 * This is Hubbub's own test/tree2.c (MIT licence: see
 * third_party/netsurf/libhubbub/COPYING), with a main() that runs every
 * test in the files it is given instead of stopping at the first failure:
 *
 *   h5test [-v] FILE.dat ...
 *
 * prints, per file and in all, how many passed, failed and were skipped
 * (fragment tests: Hubbub has no fragment parsing). -v shows each failure.
 */
#define _GNU_SOURCE

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hubbub/hubbub.h>
#include <hubbub/parser.h>
#include <hubbub/tree.h>

#include <assert.h>

#define UNUSED(x) ((x) = (x))

typedef struct attr_t attr_t;
typedef struct node_t node_t;
typedef struct buf_t buf_t;

struct attr_t {
	hubbub_ns ns;
	char *name;
	char *value;
};

struct node_t {
	enum { DOCTYPE, COMMENT, ELEMENT, CHARACTER } type;

	union {
		struct {
			char *name;
			char *public_id;
			char *system_id;
		} doctype;

		struct {
			hubbub_ns ns;
			char *name;
			attr_t *attrs;
			size_t n_attrs;
		} element;

		char *content;		/**< For comments, characters **/
	} data;

	node_t *next;
	node_t *prev;

	node_t *child;
	node_t *parent;

	uint32_t refcnt;
};

struct buf_t {
	char *buf;
	size_t len;
	size_t pos;
};


#define NUM_NAMESPACES 7
const char * const ns_names[NUM_NAMESPACES] =
		{ NULL, NULL /*html*/, "math", "svg", "xlink", "xml", "xmlns" };


node_t *Document;



static void node_print(buf_t *buf, node_t *node, unsigned depth);

static hubbub_error create_comment(void *ctx, const hubbub_string *data, void **result);
static hubbub_error create_doctype(void *ctx, const hubbub_doctype *doctype,
		void **result);
static hubbub_error create_element(void *ctx, const hubbub_tag *tag, void **result);
static hubbub_error create_text(void *ctx, const hubbub_string *data, void **result);
static hubbub_error ref_node(void *ctx, void *node);
static hubbub_error unref_node(void *ctx, void *node);
static hubbub_error append_child(void *ctx, void *parent, void *child, void **result);
static hubbub_error insert_before(void *ctx, void *parent, void *child, void *ref_child,
		void **result);
static hubbub_error remove_child(void *ctx, void *parent, void *child, void **result);
static hubbub_error clone_node(void *ctx, void *node, bool deep, void **result);
static hubbub_error reparent_children(void *ctx, void *node, void *new_parent);
static hubbub_error get_parent(void *ctx, void *node, bool element_only, void **result);
static hubbub_error has_children(void *ctx, void *node, bool *result);
static hubbub_error form_associate(void *ctx, void *form, void *node);
static hubbub_error add_attributes(void *ctx, void *node,
		const hubbub_attribute *attributes, uint32_t n_attributes);
static hubbub_error set_quirks_mode(void *ctx, hubbub_quirks_mode mode);
static hubbub_error complete_script(void *ctx, void *script);

static void delete_node(node_t *node);
static void delete_attr(attr_t *attr);

static hubbub_tree_handler tree_handler = {
	create_comment,
	create_doctype,
	create_element,
	create_text,
	ref_node,
	unref_node,
	append_child,
	insert_before,
	remove_child,
	clone_node,
	reparent_children,
	get_parent,
	has_children,
	form_associate,
	add_attributes,
	set_quirks_mode,
	NULL,
        complete_script,
	NULL
};


/*
 * Create, initialise, and return, a parser instance.
 */
static hubbub_parser *setup_parser(bool scripting)
{
	hubbub_parser *parser;
	hubbub_parser_optparams params;

	if (hubbub_parser_create("UTF-8", false, &parser) != HUBBUB_OK)
		return NULL;

	params.tree_handler = &tree_handler;
	hubbub_parser_setopt(parser, HUBBUB_PARSER_TREE_HANDLER,
			&params);

	params.document_node = (void *)1;
	hubbub_parser_setopt(parser, HUBBUB_PARSER_DOCUMENT_NODE,
			&params);

	params.enable_scripting = scripting;
	hubbub_parser_setopt(parser, HUBBUB_PARSER_ENABLE_SCRIPTING,
			&params);

	return parser;
}



/*** Buffer handling bits ***/
static void buf_add(buf_t *buf, const char *str)
{
	size_t len = strlen(str) + 1;

	if (!buf) {
		printf("%s", str);
		return;
	}

	if (buf->buf == NULL) {
		buf->len = ((len + 1024) / 1024) * 1024;
		buf->buf = calloc(1, buf->len);
	}

	while (buf->pos + len > buf->len) {
		buf->len *= 2;
		buf->buf = realloc(buf->buf, buf->len);
	}

	strcat(buf->buf, str);
	buf->pos += len;
}



hubbub_error create_comment(void *ctx, const hubbub_string *data, void **result)
{
	node_t *node = calloc(1, sizeof *node);

	UNUSED(ctx);

	node->type = COMMENT;
	node->data.content = strndup((const char *) data->ptr, data->len);
	node->refcnt = 1;
	node->refcnt = 1;

	*result = node;

	return HUBBUB_OK;
}

hubbub_error create_doctype(void *ctx, const hubbub_doctype *doctype, 
		void **result)
{
	node_t *node = calloc(1, sizeof *node);

	UNUSED(ctx);

	node->type = DOCTYPE;
	node->data.doctype.name = strndup(
			(const char *) doctype->name.ptr,
			doctype->name.len);

	if (!doctype->public_missing) {
		node->data.doctype.public_id = strndup(
				(const char *) doctype->public_id.ptr,
				doctype->public_id.len);
	}

	if (!doctype->system_missing) {
		node->data.doctype.system_id = strndup(
				(const char *) doctype->system_id.ptr,
				doctype->system_id.len);
	}
	node->refcnt = 1;

	*result = node;

	return HUBBUB_OK;
}

hubbub_error create_element(void *ctx, const hubbub_tag *tag, void **result)
{
	size_t i;
	node_t *node = calloc(1, sizeof *node);

	UNUSED(ctx);

	assert(tag->ns < NUM_NAMESPACES);

	node->type = ELEMENT;
	node->data.element.ns = tag->ns;
	node->data.element.name = strndup(
			(const char *) tag->name.ptr,
			tag->name.len);
	node->data.element.n_attrs = tag->n_attributes;

	node->data.element.attrs = calloc(node->data.element.n_attrs,
			sizeof *node->data.element.attrs);

	for (i = 0; i < tag->n_attributes; i++) {
		attr_t *attr = &node->data.element.attrs[i];

		assert(tag->attributes[i].ns < NUM_NAMESPACES);

		attr->ns = tag->attributes[i].ns;

		attr->name = strndup(
				(const char *) tag->attributes[i].name.ptr,
				tag->attributes[i].name.len);

		attr->value = strndup(
				(const char *) tag->attributes[i].value.ptr,
				tag->attributes[i].value.len);
	}
	node->refcnt = 1;

	*result = node;

	return HUBBUB_OK;
}

hubbub_error create_text(void *ctx, const hubbub_string *data, void **result)
{
	node_t *node = calloc(1, sizeof *node);

	UNUSED(ctx);

	node->type = CHARACTER;
	node->data.content = strndup((const char *) data->ptr, data->len);
	node->refcnt = 1;
	node->refcnt = 1;

	*result = node;

	return HUBBUB_OK;
}

hubbub_error ref_node(void *ctx, void *node)
{
	node_t *n = node;

	UNUSED(ctx);

	if (node != (void *) 1)
		n->refcnt++;

	return HUBBUB_OK;
}

hubbub_error unref_node(void *ctx, void *node)
{
	node_t *n = node;

	UNUSED(ctx);

	if (n != (void *) 1) {
		assert(n->refcnt > 0);

		n->refcnt--;


		if (n->refcnt == 0 && n->parent == NULL) {
			delete_node(n);
		}
	}

	return HUBBUB_OK;
}

hubbub_error append_child(void *ctx, void *parent, void *child, void **result)
{
	node_t *tparent = parent;
	node_t *tchild = child;
	node_t *insert = NULL;

	tchild->next = tchild->prev = NULL;

#ifndef NDEBUG
	printf("appending (%p):\n", (void *) tchild);
	node_print(NULL, tchild, 0);
	printf("to:\n");
	if (parent != (void *)1)
		node_print(NULL, tparent, 0);
#endif

	*result = child;

	if (parent == (void *)1) {
		if (Document) {
			insert = Document;
		} else {
			Document = tchild;
		}
	} else {
		if (tparent->child == NULL) {
			tparent->child = tchild;
		} else {
			insert = tparent->child;
		}
	}

	if (insert) {
		while (insert->next != NULL) {
			insert = insert->next;
		}

		if (tchild->type == CHARACTER && insert->type == CHARACTER) {
			insert->data.content = realloc(insert->data.content,
					strlen(insert->data.content) +
					strlen(tchild->data.content) + 1);
			strcat(insert->data.content, tchild->data.content);
			*result = insert;
		} else {
			insert->next = tchild;
			tchild->prev = insert;
		}
	}

	if (*result == child)
		tchild->parent = tparent;

	ref_node(ctx, *result);

	return HUBBUB_OK;
}

/* insert 'child' before 'ref_child', under 'parent' */
hubbub_error insert_before(void *ctx, void *parent, void *child, 
		void *ref_child, void **result)
{
	node_t *tparent = parent;
	node_t *tchild = child;
	node_t *tref = ref_child;

#ifndef NDEBUG
	printf("inserting (%p):\n", (void *) tchild);
	node_print(NULL, tchild, 0);
	printf("before:\n");
	node_print(NULL, tref, 0);
	printf("under:\n");
	if (parent != (void *)1)
		node_print(NULL, tparent, 0);
#endif

	if (tchild->type == CHARACTER && tref->prev &&
			tref->prev->type == CHARACTER) {
		node_t *insert = tref->prev;

		insert->data.content = realloc(insert->data.content,
				strlen(insert->data.content) +
				strlen(tchild->data.content) + 1);
		strcat(insert->data.content, tchild->data.content);

		*result = insert;
	} else {
		tchild->parent = parent;

		tchild->prev = tref->prev;
		tchild->next = tref;
		tref->prev = tchild;

		if (tchild->prev)
			tchild->prev->next = tchild;
		else
			tparent->child = tchild;

		*result = child;
	}

	ref_node(ctx, *result);

	return HUBBUB_OK;
}

hubbub_error remove_child(void *ctx, void *parent, void *child, void **result)
{
	node_t *tparent = parent;
	node_t *tchild = child;

	(void) tparent;			/* only the asserts look at it */
	assert(tparent->child);
	assert(tchild->parent == tparent);


	if (tchild->parent->child == tchild) {
		tchild->parent->child = tchild->next;
	}

	if (tchild->prev)
		tchild->prev->next = tchild->next;

	if (tchild->next)
		tchild->next->prev = tchild->prev;

	/* now reset all the child's pointers */
	tchild->next = tchild->prev = tchild->parent = NULL;

	*result = child;

	ref_node(ctx, *result);

	return HUBBUB_OK;
}

hubbub_error clone_node(void *ctx, void *node, bool deep, void **result)
{
	node_t *old_node = node;
	node_t *new_node = calloc(1, sizeof *new_node);
	node_t *last;
	node_t *child;
	size_t i;

	new_node->type = old_node->type;

	switch (old_node->type) {
	case DOCTYPE:
		new_node->data.doctype.name = 
				strdup(old_node->data.doctype.name);
		if (old_node->data.doctype.public_id)
			new_node->data.doctype.public_id = 
				strdup(old_node->data.doctype.public_id);
		if (old_node->data.doctype.system_id)
			new_node->data.doctype.system_id =
				strdup(old_node->data.doctype.system_id);
		break;
	case COMMENT:
	case CHARACTER:
		new_node->data.content = strdup(old_node->data.content);
		break;
	case ELEMENT:
		new_node->data.element.ns = old_node->data.element.ns;
		new_node->data.element.name = 
				strdup(old_node->data.element.name);
		new_node->data.element.attrs = 
				calloc(old_node->data.element.n_attrs, 
					sizeof *new_node->data.element.attrs);
		for (i = 0; i < old_node->data.element.n_attrs; i++) {
			attr_t *attr = &new_node->data.element.attrs[i];

			attr->ns = old_node->data.element.attrs[i].ns;
			attr->name = 
				strdup(old_node->data.element.attrs[i].name);
			attr->value =
				strdup(old_node->data.element.attrs[i].value);
		}
		new_node->data.element.n_attrs = old_node->data.element.n_attrs;
		break;
	}

	*result = new_node;

	new_node->child = new_node->parent =
			new_node->next = new_node->prev =
			NULL;

	new_node->refcnt = 1;

	if (deep == false)
		return 0;

	last = NULL;

	for (child = old_node->child; child != NULL; child = child->next) {
		node_t *n;

		clone_node(ctx, child, true, (void **) (void *) &n);

		n->refcnt = 0;

		if (last == NULL) {
			new_node->child = n;
		} else {
			last->next = n;
			n->prev = last;
		}

		n->parent = new_node;
		last = n;
	}

	return HUBBUB_OK;
}

/* Take all of the child nodes of "node" and append them to "new_parent" */
hubbub_error reparent_children(void *ctx, void *node, void *new_parent)
{
	node_t *parent = new_parent;
	node_t *old_parent = node;

	node_t *insert;
	node_t *kids;

	UNUSED(ctx);

	kids = old_parent->child;
	if (!kids) return 0;

	old_parent->child = NULL;

	insert = parent->child;
	if (!insert) {
		parent->child = kids;
	} else {
		while (insert->next != NULL) {
			insert = insert->next;
		}

		insert->next = kids;
		kids->prev = insert;
	}

	while (kids) {
		kids->parent = parent;
		kids = kids->next;
	}

	return HUBBUB_OK;
}

hubbub_error get_parent(void *ctx, void *node, bool element_only, void **result)
{
	UNUSED(element_only);

	*result = ((node_t *)node)->parent;

	if (*result != NULL)
		ref_node(ctx, *result);

	return HUBBUB_OK;
}

hubbub_error has_children(void *ctx, void *node, bool *result)
{
	UNUSED(ctx);

	*result = ((node_t *)node)->child ? true : false;

	return HUBBUB_OK;
}

hubbub_error form_associate(void *ctx, void *form, void *node)
{
	UNUSED(ctx);
	UNUSED(form);
	UNUSED(node);

	return HUBBUB_OK;
}

hubbub_error add_attributes(void *ctx, void *vnode,
		const hubbub_attribute *attributes, uint32_t n_attributes)
{
	node_t *node = vnode;
	size_t old_elems = node->data.element.n_attrs;
	size_t i;

	UNUSED(ctx);

	node->data.element.n_attrs += n_attributes;

	node->data.element.attrs = realloc(node->data.element.attrs,
			node->data.element.n_attrs *
				sizeof *node->data.element.attrs);

	for (i = 0; i < n_attributes; i++) {
		attr_t *attr = &node->data.element.attrs[old_elems + i];

		assert(attributes[i].ns < NUM_NAMESPACES);

		attr->ns = attributes[i].ns;

		attr->name = strndup(
				(const char *) attributes[i].name.ptr,
				attributes[i].name.len);

		attr->value = strndup(
				(const char *) attributes[i].value.ptr,
				attributes[i].value.len);
	}


	return HUBBUB_OK;
}

hubbub_error set_quirks_mode(void *ctx, hubbub_quirks_mode mode)
{
	UNUSED(ctx);
	UNUSED(mode);

	return HUBBUB_OK;
}

hubbub_error complete_script(void *ctx, void *script)
{
	UNUSED(ctx);
	UNUSED(script);

	return HUBBUB_OK;
}


/*** Serialising bits ***/

static int compare_attrs(const void *a, const void *b) {
	const attr_t *first = a;
	const attr_t *second = b;

	return strcmp(first->name, second->name);
}




static void indent(buf_t *buf, unsigned depth)
{
	unsigned int i;

	buf_add(buf, "| ");

	for (i = 0; i < depth; i++) {
		buf_add(buf, "  ");
	}
}

static void print_ns(buf_t *buf, hubbub_ns ns)
{
	if (ns_names[ns] != NULL) {
		buf_add(buf, ns_names[ns]);
		buf_add(buf, " ");
	}
}

static void node_print(buf_t *buf, node_t *node, unsigned depth)
{
	size_t i;

	if (!node) return;

	indent(buf, depth);

	switch (node->type)
	{
	case DOCTYPE:
		buf_add(buf, "<!DOCTYPE ");
		buf_add(buf, node->data.doctype.name);

		if (node->data.doctype.public_id ||
				node->data.doctype.system_id) {
			buf_add(buf, " \"");
			if (node->data.doctype.public_id)
				buf_add(buf, node->data.doctype.public_id);
			buf_add(buf, "\" \"");
			if (node->data.doctype.system_id)
				buf_add(buf, node->data.doctype.system_id);
			buf_add(buf, "\"");
		}

		buf_add(buf, ">\n");
		break;
	case ELEMENT:
		buf_add(buf, "<");
		print_ns(buf, node->data.element.ns);
		buf_add(buf, node->data.element.name);
		buf_add(buf, ">\n");

		if (node->data.element.n_attrs > 0) {
			qsort(node->data.element.attrs,
					node->data.element.n_attrs,
					sizeof *node->data.element.attrs,
					compare_attrs);
		}

		for (i = 0; i < node->data.element.n_attrs; i++) {
			indent(buf, depth + 1);
			print_ns(buf, node->data.element.attrs[i].ns);
			buf_add(buf, node->data.element.attrs[i].name);
			buf_add(buf, "=");
			buf_add(buf, "\"");
			buf_add(buf, node->data.element.attrs[i].value);
			buf_add(buf, "\"\n");
		}

		break;
	case CHARACTER:
		buf_add(buf, "\"");
		buf_add(buf, node->data.content);
		buf_add(buf, "\"\n");
		break;
	case COMMENT:
		buf_add(buf, "<!-- ");
		buf_add(buf, node->data.content);
		buf_add(buf, " -->\n");
		break;
	default:
		printf("Unexpected node type %d\n", node->type);
		assert(0);
	}

	if (node->child) {
		node_print(buf, node->child, depth + 1);
	}

	if (node->next) {
		node_print(buf, node->next, depth);
	}
}

static void delete_node(node_t *node)
{
	size_t i;
	node_t *c, *d;

	if (node == NULL)
		return;

	if (node->refcnt != 0) {
		printf("Node %p has non-zero refcount %d\n", 
				(void *) node, node->refcnt);
		assert(0);
	}

	switch (node->type) {
	case DOCTYPE:
		free(node->data.doctype.name);
		free(node->data.doctype.public_id);
		free(node->data.doctype.system_id);
		break;
	case COMMENT:
	case CHARACTER:
		free(node->data.content);
		break;
	case ELEMENT:
		free(node->data.element.name);
		for (i = 0; i < node->data.element.n_attrs; i++)
			delete_attr(&node->data.element.attrs[i]);
		free(node->data.element.attrs);
		break;
	}

	for (c = node->child; c != NULL; c = d) {
		d = c->next;

		delete_node(c);
	}

	memset(node, 0xdf, sizeof(node_t));

	free(node);
}

static void delete_attr(attr_t *attr)
{
	if (attr == NULL)
		return;

	free(attr->name);
	free(attr->value);

	memset(attr, 0xdf, sizeof(attr_t));
}


/*** The tests ***/

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n + 1);
	if (b != NULL && fread(b, 1, n, f) != (size_t) n) {
		free(b);
		b = NULL;
	}
	if (b != NULL)
		b[n] = 0;
	fclose(f);
	return b;
}

/* a line that starts a section of a test */
static bool is_section(const char *s)
{
	static const char *const names[] = { "#data\n", "#errors\n", "#new-errors\n",
		"#document\n", "#document-fragment\n", "#script-off\n", "#script-on\n",
		"#description\n" };
	size_t i;

	for (i = 0; i < sizeof names / sizeof names[0]; i++)
		if (strncmp(s, names[i], strlen(names[i])) == 0)
			return true;
	return false;
}

/* the section that starts at *p (a line "#name"): its name goes to name,
 * its text (up to the newline before the next section's line, which is
 * cut) is returned, and *p moves to the next section */
static char *section(char **p, char *name, size_t size)
{
	char *s = *p, *e, *text;

	if (*s != '#')
		return NULL;
	e = strchr(s, '\n');
	if (e == NULL)
		return NULL;
	snprintf(name, size, "%.*s", (int) (e - s - 1), s + 1);
	text = e + 1;
	if (is_section(text)) {		/* an empty section */
		*e = 0;
		*p = text;
		return e;
	}
	for (e = text; ; e++) {
		e = strstr(e, "\n#");
		if (e == NULL) {
			e = text + strlen(text);
			break;
		}
		if (is_section(e + 1))
			break;
	}
	*p = *e ? e + 1 : e;
	*e = 0;
	return text;
}

int main(int argc, char **argv)
{
	int verbose = 0, a;
	unsigned long all_pass = 0, all_fail = 0, all_skip = 0;

	for (a = 1; a < argc; a++) {
		char *file, *p;
		unsigned long pass = 0, fail = 0, skip = 0;

		if (strcmp(argv[a], "-v") == 0) {
			verbose = 1;
			continue;
		}
		file = slurp(argv[a]);
		if (file == NULL) {
			printf("%s: can't read\n", argv[a]);
			return 1;
		}
		p = strstr(file, "#data\n");
		while (p != NULL && *p) {
			char name[40], *data = NULL, *expected = NULL, *text;
			bool fragment = false, scripting = true;
			hubbub_parser *parser;
			buf_t got = { NULL, 0, 0 };

			/* one test: "#data", then its other sections up to the next "#data" */
			do {
				text = section(&p, name, sizeof name);
				if (text == NULL)
					break;
				if (strcmp(name, "data") == 0)
					data = text;
				else if (strcmp(name, "document-fragment") == 0)
					fragment = true;
				else if (strcmp(name, "script-off") == 0)
					scripting = false;
				else if (strcmp(name, "document") == 0)
					expected = text;
				while (*p == '\n')
					p++;
			} while (*p == '#' && strncmp(p, "#data\n", 6) != 0);
			if (expected != NULL) {
				size_t n = strlen(expected);
				while (n > 0 && expected[n - 1] == '\n')
					expected[--n] = 0;
			}
			if (data == NULL)
				break;
			if (expected == NULL) {
				skip++;
				continue;
			}
			if (fragment) {
				skip++;
				continue;
			}
			parser = setup_parser(scripting);
			if (parser == NULL ||
					hubbub_parser_parse_chunk(parser, (uint8_t *) data, strlen(data)) != HUBBUB_OK ||
					hubbub_parser_completed(parser) != HUBBUB_OK) {
				fail++;
				if (verbose)
					printf("FAIL (parser error): %s\n", data);
			} else {
				node_print(&got, Document, 0);
				if (got.buf != NULL && got.pos > 0 && got.buf[strlen(got.buf) - 1] == '\n')
					got.buf[strlen(got.buf) - 1] = 0;
				if (got.buf != NULL && strcmp(got.buf, expected) == 0) {
					pass++;
				} else {
					fail++;
					if (verbose)
						printf("FAIL\n#data\n%s\n#expected\n%s\n#got\n%s\n\n", data,
							expected, got.buf ? got.buf : "");
				}
			}
			if (parser != NULL)
				hubbub_parser_destroy(parser);
			while (Document) {
				node_t *victim = Document;
				Document = victim->next;
				delete_node(victim);
			}
			free(got.buf);
		}
		printf("%-48s %5lu passed %5lu failed %5lu skipped\n", argv[a], pass, fail, skip);
		all_pass += pass;
		all_fail += fail;
		all_skip += skip;
		free(file);
	}
	printf("all: %lu passed, %lu failed, %lu skipped (fragments); %.1f%% of those run\n",
		all_pass, all_fail, all_skip,
		all_pass + all_fail ? 100.0 * all_pass / (all_pass + all_fail) : 0.0);
	return 0;
}
