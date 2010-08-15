/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * check_list.c - check that gwlib/list.c works
 */
 
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

#define NUM_PRODUCERS (4)
#define NUM_CONSUMERS (4)
#define NUM_ITEMS_PER_PRODUCER (1*1000)

struct producer_info {
    List *list;
    long start_index;
    long id;
};


static char received[NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER];


typedef struct {
	long producer;
	long num;
	long index;
} Item;


static Item *new_item(long producer, long num, long index) {
	Item *item;
	
	item = gw_malloc(sizeof(Item));
	item->producer = producer;
	item->num = num;
	item->index = index;
	return item;
}


static void producer(void *arg) {
	long i, index;
	long id;
	struct producer_info *info;

	info = arg;

	id = gwthread_self();
	index = info->start_index;
	for (i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i, ++index)
		gwlist_produce(info->list, new_item(id, i, index));
	gwlist_remove_producer(info->list);
}

static void consumer(void *arg) {
	List *list;
	Item *item;
	
	list = arg;
	for (;;) {
		item = gwlist_consume(list);
		if (item == NULL)
			break;
		received[item->index] = 1;
		gw_free(item);
	}
}


static void init_received(void) {
	memset(received, 0, sizeof(received));
}


static void main_for_producer_and_consumer(void) {
	List *list;
	int i;
	Item *item;
	struct producer_info tab[NUM_PRODUCERS];
	long p, n, index;
	int errors;
	
	list = gwlist_create();
	init_received();
	
	for (i = 0; i < NUM_PRODUCERS; ++i) {
	    	tab[i].list = list;
		tab[i].start_index = i * NUM_ITEMS_PER_PRODUCER;
	    	gwlist_add_producer(list);
		tab[i].id = gwthread_create(producer, tab + i);
	}
	for (i = 0; i < NUM_CONSUMERS; ++i)
		gwthread_create(consumer, list);
	
    	gwthread_join_every(producer);
    	gwthread_join_every(consumer);

	while (gwlist_len(list) > 0) {
		item = gwlist_get(list, 0);
		gwlist_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", (long) item->producer, 
				item->num, item->index);
	}
	
	errors = 0;
	for (p = 0; p < NUM_PRODUCERS; ++p) {
		for (n = 0; n < NUM_ITEMS_PER_PRODUCER; ++n) {
			index = p * NUM_ITEMS_PER_PRODUCER + n;
			if (!received[index]) {
				error(0, "Not received: producer=%ld "
				         "item=%ld index=%ld", 
					 tab[p].id, n, index);
				errors = 1;
			}
		}
	}
	
	if (errors)
		panic(0, "Not all messages were received.");
}


static int compare_cstr(void *item, void *pat) {
    	/* Remove a macro definition of strcmp to prevent warnings from
	 * a broken version in the glibc libary. */
	#undef strcmp
	return strcmp(item, pat) == 0;
}


static void main_for_list_add_and_delete(void) {
	static char *items[] = {
		"one",
		"two",
		"three",
	};
	int num_items = sizeof(items) / sizeof(items[0]);
	int num_repeats = 3;
	int i, j;
	char *p;
	List *list;

	list = gwlist_create();
	
	for (j = 0; j < num_repeats; ++j)
		for (i = 0; i < num_items; ++i)
			gwlist_append(list, items[i]);
	gwlist_delete_matching(list, items[0], compare_cstr);
	for (i = 0; i < gwlist_len(list); ++i) {
		p = gwlist_get(list, i);
		if (strcmp(p, items[0]) == 0)
			panic(0, "list contains `%s' after deleting it!",
				items[0]);
	}
	
	for (i = 0; i < num_items; ++i)
		gwlist_delete_equal(list, items[i]);
	if (gwlist_len(list) != 0)
		panic(0, "list is not empty after deleting everything");
	
	gwlist_destroy(list, NULL);
}


static void main_for_extract(void) {
	static char *items[] = {
		"one",
		"two",
		"three",
	};
	int num_items = sizeof(items) / sizeof(items[0]);
	int num_repeats = 3;
	int i, j;
	char *p;
	List *list, *extracted;

	list = gwlist_create();
	
	for (j = 0; j < num_repeats; ++j)
		for (i = 0; i < num_items; ++i)
			gwlist_append(list, items[i]);

	for (j = 0; j < num_items; ++j) {
		extracted = gwlist_extract_matching(list, items[j], 
					compare_cstr);
		if (extracted == NULL)
			panic(0, "no extracted elements, should have!");
		for (i = 0; i < gwlist_len(list); ++i) {
			p = gwlist_get(list, i);
			if (strcmp(p, items[j]) == 0)
				panic(0, "list contains `%s' after "
				         "extracting it!",
					items[j]);
		}
		for (i = 0; i < gwlist_len(extracted); ++i) {
			p = gwlist_get(extracted, i);
			if (strcmp(p, items[j]) != 0)
				panic(0, 
				  "extraction returned wrong element!");
		}
		gwlist_destroy(extracted, NULL);
	}
	
	if (gwlist_len(list) != 0)
		panic(0, "list is not empty after extracting everything");
	
	gwlist_destroy(list, NULL);
}


int main(void) {
	gwlib_init();
	log_set_output_level(GW_INFO);
	main_for_list_add_and_delete();
	main_for_extract();
	main_for_producer_and_consumer();
	gwlib_shutdown();
	return 0;
}
