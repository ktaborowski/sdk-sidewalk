/*
 * Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * AMAZON PROPRIETARY/CONFIDENTIAL
 *
 * You may not use this file except in compliance with the terms and
 * conditions set forth in the accompanying LICENSE.txt file. This file is a
 * Modifiable File, as defined in the accompanying LICENSE.txt file.
 *
 * THESE MATERIALS ARE PROVIDED ON AN "AS IS" BASIS. AMAZON SPECIFICALLY
 * DISCLAIMS, WITH RESPECT TO THESE MATERIALS, ALL WARRANTIES, EXPRESS,
 * IMPLIED, OR STATUTORY, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
 */

#include <sid_hal_memory_ifc.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

LOG_MODULE_REGISTER(hal_memory, CONFIG_SIDEWALK_LOG_LEVEL);

#ifndef CONFIG_SID_HAL_PROTOCOL_MEMORY_SZ
#define CONFIG_SID_HAL_PROTOCOL_MEMORY_SZ 0
#endif

#ifndef CONFIG_SIDEWALK_HEAP_SIZE
#define CONFIG_SIDEWALK_HEAP_SIZE 0
#endif

#ifndef CONFIG_SID_END_DEVICE_EVENT_HEAP_SIZE
#define CONFIG_SID_END_DEVICE_EVENT_HEAP_SIZE 0
#endif

#ifndef CONFIG_SIDEWALK_FILE_TRANSFER_HEAP_SIZE
#define CONFIG_SIDEWALK_FILE_TRANSFER_HEAP_SIZE 0
#endif

#define HEAP_SIZE CONFIG_SID_HAL_PROTOCOL_MEMORY_SZ + CONFIG_SIDEWALK_HEAP_SIZE + CONFIG_SID_END_DEVICE_EVENT_HEAP_SIZE + CONFIG_SIDEWALK_FILE_TRANSFER_HEAP_SIZE

K_HEAP_DEFINE(sid_heap, HEAP_SIZE);

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
static void heap_alloc_stats(struct sys_heap *p_heap, size_t mem_to_alloc)
{
	static size_t max_usage = 0;
	struct sys_memory_stats stat = {};

	sys_heap_runtime_stats_get(p_heap, &stat);
	if (mem_to_alloc > stat.free_bytes) {
		LOG_ERR("Not heap left. Alloc size: %u, free: %u", mem_to_alloc, stat.free_bytes);
		return;
	}

	if(max_usage < stat.max_allocated_bytes + mem_to_alloc){
		max_usage = stat.max_allocated_bytes + mem_to_alloc;
		LOG_DBG("New max heap usage %u / %u", max_usage, HEAP_SIZE);
	}
}
#endif
#ifdef CONFIG_SIDEWALK_TRACE_HEAP
uint32_t alloc_stat = 0;
uint32_t free_stat = 0;

struct trace_heap{
	void* caller_function;
	void* buffer;
	size_t size;
};

struct trace_heap open_buffers[100];
static void store_buffer(void* caller, void* buffer, size_t size){
	for(size_t i = 0; i < ARRAY_SIZE(open_buffers); i++){
		if(open_buffers[i].buffer == NULL){
			open_buffers[i].buffer = buffer;
			open_buffers[i].caller_function = caller;
			open_buffers[i].size = size;
			return;
		}
	}
}
static void remove_buffer(void* buffer){
	for(size_t i = 0; i < ARRAY_SIZE(open_buffers); i++){
		if(open_buffers[i].buffer == buffer){
			open_buffers[i] = (struct trace_heap){0, 0, 0};
			return;
		}
	}
}

void print_open_buffers(void){
	printk("Allocated %d, freed %d\n",alloc_stat, free_stat);
	for(size_t i = 0; i < ARRAY_SIZE(open_buffers); i++){
		if(open_buffers[i].buffer != NULL){
			printk("function %p, buffer %p size %d\n", open_buffers[i].caller_function, open_buffers[i].buffer, open_buffers[i].size);
		}
	}
}
#endif

void *sid_hal_malloc(size_t size)
{
    #ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	heap_alloc_stats(&sid_heap.heap, size);
    #endif

	void * ptr = k_heap_alloc(&sid_heap, size, K_NO_WAIT);
	#if CONFIG_SIDEWALK_TRACE_HEAP
	LOG_DBG("Alloc %d bytes at addr %p", size, ptr);
	alloc_stat++;
	store_buffer(__builtin_return_address(0), ptr, size);
	#endif /* CONFIG_SIDEWALK_TRACE_HEAP */
	return ptr;
}

void sid_hal_free(void *ptr)
{
    if (!ptr) {
	LOG_ERR("Can not free NULL ptr");
        return;
    }
    	#if CONFIG_SIDEWALK_TRACE_HEAP
	LOG_DBG("Free ptr at addr %p", ptr);
    	free_stat++;
	remove_buffer(ptr);
	#endif /* CONFIG_SIDEWALK_TRACE_HEAP */
    k_heap_free(&sid_heap, ptr);
}
