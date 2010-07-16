/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008 WiredTiger Software.
 *	All rights reserved.
 *
 * $Id$
 */

#include "wt_internal.h"

/*
 * __wt_db_row_del --
 *	Db.row_del method.
 */
int
__wt_db_row_del(WT_TOC *toc, DBT *key)
{
	ENV *env;
	IDB *idb;
	WT_PAGE *page;
	WT_REPL **new_repl, *repl;
	int ret;

	env = toc->env;
	idb = toc->db->idb;
	new_repl = NULL;
	repl = NULL;

	/* Search the btree for the key. */
	WT_RET(__wt_bt_search_row(toc, key, 0));
	page = toc->srch_page;

	/* Allocate a page replacement array as necessary. */
	if (page->repl == NULL)
		WT_ERR(__wt_calloc(
		    env, page->indx_count, sizeof(WT_REPL *), &new_repl));

	/* Allocate a WT_REPL structure and fill it in. */
	WT_ERR(__wt_calloc(env, 1, sizeof(WT_REPL), &repl));
	repl->data = WT_REPL_DELETED_VALUE;

	/* Schedule the workQ to insert the WT_REPL structure. */
	__wt_bt_update_serial(toc,
	    page, WT_ROW_SLOT(page, toc->srch_ip), new_repl, repl, ret);

	if (0) {
err:		if (repl != NULL)
			__wt_free(env, repl, sizeof(WT_REPL));
	}

	/* Free any replacement array unless the workQ used it. */
	if (new_repl != NULL && new_repl != page->repl)
		__wt_free(env, new_repl, page->indx_count * sizeof(WT_REPL *));

	if (page != NULL && page != idb->root_page)
		__wt_bt_page_out(toc, &page, ret == 0 ? WT_MODIFIED : 0);

	return (0);
}

/*
 * __wt_bt_update_serial_func --
 *	Server function to update a WT_REPL entry in the modification array.
 */
int
__wt_bt_update_serial_func(WT_TOC *toc)
{
	WT_PAGE *page;
	WT_REPL **new_repl, *repl;
	int slot;

	__wt_bt_update_unpack(toc, page, slot, new_repl, repl);

	/*
	 * If the page does not yet have a replacement array, our caller passed
	 * us one of the correct size.   (It's the caller's responsibility to
	 * detect & free the passed-in expansion array if we don't use it.)
	 */
	if (page->repl == NULL)
		page->repl = new_repl;

	/*
	 * Insert the new WT_REPL as the first item in the forward-linked list
	 * of replacement structures.  Flush memory to ensure the list is never
	 * broken.
	 */
	repl->next = page->repl[slot];
	WT_MEMORY_FLUSH;
	page->repl[slot] = repl;
	WT_PAGE_MODIFY_SET_AND_FLUSH(page);
	return (0);
}
