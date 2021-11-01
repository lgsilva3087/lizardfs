/*
   Copyright 2005-2010 Jakub Kruszona-Zawadzki, Gemius SA, 2013-2014 EditShare, 2013-2015 Skytechnology sp. z o.o..

   This file was part of MooseFS and is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common/platform.h"

#include <inttypes.h>

#include "mount/chunk_locator.h"
#include "mount/readdata_cache.h"

uint32_t read_data_get_wave_read_timeout_ms();
uint32_t read_data_get_connect_timeout_ms();
uint32_t read_data_get_total_read_timeout_ms();
bool read_data_get_prefetchxorstripes();

void read_inode_ops(uint32_t inode);
void* read_data_new(uint32_t inode);
void read_data_end(void *rr);
int read_data(void *rr, uint64_t offset, uint32_t size, ReadCache::Result &ret);
void read_data_freebuff(void *rr);
void read_data_init(uint32_t retries,
		uint32_t chunkserverRoundTripTime_ms,
		uint32_t chunkserverConnectTimeout_ms,
		uint32_t chunkServerWaveReadTimeout_ms,
		uint32_t chunkserverTotalReadTimeout_ms,
		uint32_t cache_expiration_time_ms,
		uint32_t readahead_max_window_size_kB,
		bool prefetchXorStripes,
		double bandwidth_overuse);
void read_data_term(void);

void* read_data_second_buffer(void *args);
