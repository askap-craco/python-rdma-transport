#pragma once

#include "rdma_transport.hpp"

extern "C" {
#include "RDMAapi.h"
}


void run_test(
              /* set up default values that might be overriden by command line arguments */
             enum logType requestLogLevel,
             enum runMode mode ,
             uint32_t messageSize , /* size in bytes of single RDMA message */
             uint32_t numMemoryBlocks, /* number of memory blocks to allocate for RDMA messages */
             uint32_t numContiguousMessages, /* number of contiguous messages to hold in each memory block */
             char *dataFileName, /* default to not loading (sender) nor saving (receiver) to file the data memory blocks */
             uint64_t numTotalMessages,/* total number of messages to send or receive, if 0 then default to numMemoryBlocks*numContiguousMessages */
             uint32_t messageDelayTime,/* time in milliseconds to delay after each message send/receive posted, default is no delay */
             char *rdmaDeviceName , /* no preferred rdma device name to choose */
             uint8_t rdmaPort ,
             int gidIndex , /* preferred gid index or -1 for no preference */
             char *identifierFileName, /* default to using stdio for exchanging RDMA identifiers */
             char *metricURL ,/* default to not push metrics */
             uint32_t numMetricAveraging  /* number of message completions over which to average metrics, default to numMemoryBlocks*numContiguousMessages */
              );
