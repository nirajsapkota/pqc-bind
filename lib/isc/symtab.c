/*
 * Copyright (C) 1996, 1997, 1998  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#include <config.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <isc/assertions.h>
#include <isc/list.h>
#include <isc/symtab.h>

#include "util.h"

typedef struct elt {
	char *				key;
	unsigned int			type;
	isc_symvalue_t			value;
	LINK(struct elt)		link;
} elt_t;

typedef LIST(elt_t)			eltlist_t;

#define SYMTAB_MAGIC			0x53796D54U	/* SymT. */
#define VALID_SYMTAB(st)		((st) != NULL && \
					 (st)->magic == SYMTAB_MAGIC)

struct isc_symtab {
	/* Unlocked. */
	unsigned int			magic;
	isc_mem_t *			mctx;
	unsigned int			size;
	eltlist_t *			table;
	isc_symtabaction_t		undefine_action;
};

isc_result_t
isc_symtab_create(isc_mem_t *mctx, unsigned int size,
		  isc_symtabaction_t undefine_action,
		  isc_symtab_t **symtabp)
{
	isc_symtab_t *symtab;
	unsigned int i;

	REQUIRE(mctx != NULL);
	REQUIRE(symtabp != NULL && *symtabp == NULL);
	REQUIRE(size > 0);	/* Should be prime. */

	symtab = (isc_symtab_t *)isc_mem_get(mctx, sizeof *symtab);
	if (symtab == NULL)
		return (ISC_R_NOMEMORY);
	symtab->table = (eltlist_t *)isc_mem_get(mctx,
						 size * sizeof (eltlist_t));
	if (symtab->table == NULL) {
		isc_mem_put(mctx, symtab, sizeof *symtab);
		return (ISC_R_NOMEMORY);
	}
	for (i = 0; i < size; i++)
		INIT_LIST(symtab->table[i]);
	symtab->mctx = mctx;
	symtab->size = size;
	symtab->undefine_action = undefine_action;
	symtab->magic = SYMTAB_MAGIC;

	*symtabp = symtab;

	return (ISC_R_SUCCESS);
}

void
isc_symtab_destroy(isc_symtab_t **symtabp) {
	isc_symtab_t *symtab;
	unsigned int i;
	elt_t *elt, *nelt;

	REQUIRE(symtabp != NULL);
	symtab = *symtabp;
	REQUIRE(VALID_SYMTAB(symtab));

	for (i = 0; i < symtab->size; i++) {
		for (elt = HEAD(symtab->table[i]); elt != NULL; elt = nelt) {
			nelt = NEXT(elt, link);
			if (symtab->undefine_action != NULL)
				(symtab->undefine_action)(elt->key,
							  elt->type,
							  elt->value);
			isc_mem_put(symtab->mctx, elt, sizeof *elt);
		}
	}
	isc_mem_put(symtab->mctx, symtab->table,
		    symtab->size * sizeof (eltlist_t));
	symtab->magic = 0;
	isc_mem_put(symtab->mctx, symtab, sizeof *symtab);

	*symtabp = NULL;
}

static inline unsigned int
hash(const char *key) {
	const char *s;
	unsigned int h = 0;
	unsigned int g;
	int c;

	/*
	 * P. J. Weinberger's hash function, adapted from p. 436 of
	 * _Compilers: Principles, Techniques, and Tools_, Aho, Sethi
	 * and Ullman, Addison-Wesley, 1986, ISBN 0-201-10088-6.
	 */

	for (s = key; *s != '\0'; s++) {
		c = *s;
		if (isascii(c) && isupper(c))
			c = tolower(c);
		h = ( h << 4 ) + c;
		if ((g = ( h & 0xf0000000 )) != 0) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return (h);
}

#define FIND(s, k, t, b, e) \
	b = hash((k)) % (s)->size; \
	for (e = HEAD((s)->table[b]); e != NULL; e = NEXT(e, link)) { \
		if (((t) == 0 || e->type == (t)) && \
		    strcasecmp(e->key, (k)) == 0) \
			break; \
	}

isc_result_t
isc_symtab_lookup(isc_symtab_t *symtab, const char *key, unsigned int type,
		  isc_symvalue_t *value)
{
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);

	FIND(symtab, key, type, bucket, elt);

	if (elt == NULL)
		return (ISC_R_NOTFOUND);

	if (value != NULL)
		*value = elt->value;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_symtab_define(isc_symtab_t *symtab, char *key, unsigned int type,
		  isc_symvalue_t value)
{
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);

	FIND(symtab, key, type, bucket, elt);

	if (elt != NULL)
		return (ISC_R_EXISTS);

	elt = (elt_t *)isc_mem_get(symtab->mctx, sizeof *elt);
	if (elt == NULL)
		return (ISC_R_NOMEMORY);
	elt->key = key;
	elt->type = type;
	elt->value = value;

	APPEND(symtab->table[bucket], elt, link);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_symtab_undefine(isc_symtab_t *symtab, char *key, unsigned int type) {
	unsigned int bucket;
	elt_t *elt;

	REQUIRE(VALID_SYMTAB(symtab));
	REQUIRE(key != NULL);

	FIND(symtab, key, type, bucket, elt);

	if (elt == NULL)
		return (ISC_R_NOTFOUND);

	if (symtab->undefine_action != NULL)
		(symtab->undefine_action)(elt->key, elt->type, elt->value);
	UNLINK(symtab->table[bucket], elt, link);
	isc_mem_put(symtab->mctx, elt, sizeof *elt);

	return (ISC_R_SUCCESS);
}
