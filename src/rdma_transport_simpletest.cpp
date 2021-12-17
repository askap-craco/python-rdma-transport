#include "rdma_transport_simpletest.hpp"

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
              ) {

    if (numTotalMessages == 0)
        numTotalMessages = numMemoryBlocks * numContiguousMessages;
    if (numMetricAveraging == 0)
        numMetricAveraging = numMemoryBlocks * numContiguousMessages;
    setLogLevel(requestLogLevel);

    if (mode == SEND_MODE)
    {
        logger(LOG_INFO, "Receive Visibilities starting as sender");
    }
    else
    {
        logger(LOG_INFO, "Receive Visibilities starting as receiver");
    }

    /* allocate memory blocks as buffers to be used in the memory regions */
    uint64_t memoryBlockSize = messageSize * numContiguousMessages;
    void **memoryBlocks = allocateMemoryBlocks(memoryBlockSize, &numMemoryBlocks);

    /* create a memory region manager to manage the usage of the memory blocks */
    /* note for simplicity memory region manager doesn't monitor memory regions so this code doesn't have to */
    /* load memory blocks with new data during sending/receiving */
    MemoryRegionManager* manager = createMemoryRegionManager(memoryBlocks,
        messageSize, numMemoryBlocks, numContiguousMessages, numTotalMessages, false);

    /* if in send mode then populate the memory blocks and display contents to stdio */
    if (mode == SEND_MODE)
    {
        if (dataFileName != NULL)
        {
            // read data from dataFileName.0, dataFileName.1, ... into memory blocks
            if (!readFilesIntoMemoryBlocks(manager, dataFileName))
            {
                logger(LOG_WARNING, "Unsuccessful read of data files");
            }
        }
        else
        {
            /* put something into each allocated memory block to test sending */
            for (uint32_t blockIndex=0; blockIndex<numMemoryBlocks; blockIndex++)
            {
                for (uint64_t i=0; i<memoryBlockSize; i++)
                {
                    ((unsigned char*)memoryBlocks[blockIndex])[i]
                        = (unsigned char)(i+(blockIndex*memoryBlockSize));
                }
            }
        }
        setAllMemoryRegionsPopulated(manager, true);
        displayMemoryBlocks(manager, 10, 10); /* display contents that will send */
    }
    else
    {
        /* clear each memory block */
        for (uint32_t blockIndex=0; blockIndex<numMemoryBlocks; blockIndex++)
        {
            memset(memoryBlocks[blockIndex], 0, memoryBlockSize);
        }
        setAllMemoryRegionsPopulated(manager, false);
    }

    /* create a pointer to an identifier exchange function (see eg RDMAexchangeidentifiers for some examples) */
    enum exchangeResult (*identifierExchangeFunction)(bool isSendMode,
        uint32_t packetSequenceNumber, uint32_t queuePairNumber, union ibv_gid gidAddress, uint16_t localIdentifier,
        uint32_t *remotePSNPtr, uint32_t *remoteQPNPtr, union ibv_gid *remoteGIDPtr, uint16_t *remoteLIDPtr) = NULL;
    if (identifierFileName != NULL)
    {
        setIdentifierFileName(identifierFileName);
        identifierExchangeFunction = exchangeViaSharedFiles;
    }

    /* prepare the RDMA device and then perform RDMA transfers in a separate thread */
    rdmaTransferMemoryBlocks(mode, manager,
        messageDelayTime, identifierExchangeFunction,
        rdmaDeviceName, rdmaPort, gidIndex,
        metricURL, numMetricAveraging);
    /* process the message data as they get transferred */
    uint64_t expectedTransfer = 0;
    uint32_t currentRegionIndex = 0;
    uint32_t currentContiguousIndex = 0;
    while (expectedTransfer < numTotalMessages)
    {
        /* delay until the current memory location has its data transferred */
        uint64_t currentTransfer = getMessageTransferred(manager, currentRegionIndex, currentContiguousIndex);
        while (currentTransfer==NO_MESSAGE_ORDINAL || currentTransfer<expectedTransfer)
        {
            /* expected message transfer has not yet taken place */
            microSleep(10); /* sleep current thread before checking currentTransfer again */
            currentTransfer = getMessageTransferred(manager, currentRegionIndex, currentContiguousIndex);
        }
        /* process the current message transfer */
        if (currentTransfer > expectedTransfer)
        {
            /* messages between expectedTransfer and currentTransfer-1 inclusive have been dropped during the transfer */
            /* HERE: do something about the missing messages */
        }
        /* note message data for currentTransfer is now available in memoryBlocks[currentRegionIndex] at start index currentContiguousIndex*messageSize */
        /* HERE: do some processing with the current message transfer */
        /* once done processing the current message transfer move along to the next memory location */
        expectedTransfer = currentTransfer + 1;
        currentContiguousIndex++;
        if (currentContiguousIndex >= manager->numContiguousMessages)
        {
            /* move along to the start of the next memory region */
            setMemoryRegionPopulated(manager, currentRegionIndex, false); /* finished processing this memory region so it can now be reused */
            currentContiguousIndex = 0;
            currentRegionIndex++;
            if (currentRegionIndex >= manager->numMemoryRegions)
            {
                /* wrap around to reuse the same memory regions */
                currentRegionIndex = 0;
            }
        }
    }
    /* ensure the RDMA transfers taking place in a separate thread have completed by making current thread wait until they have completed */
    waitForRdmaTransferCompletion(manager); 

    /* if in receive mode then display contents to stdio */
    if (mode == RECV_MODE)
    {
        if (dataFileName != NULL)
        {
            // write data to dataFileName.0, dataFileName.1, ... from memory blocks
            if (!writeFilesFromMemoryBlocks(manager, dataFileName))
            {
                logger(LOG_WARNING, "Unsuccessful write of data files");
            }
        }
        displayMemoryBlocks(manager, 10, 10); /* display contents that were received */
    }

    destroyMemoryRegionManager(manager);
    /* free memory block buffers */
    freeMemoryBlocks(memoryBlocks, numMemoryBlocks);

    logger(LOG_INFO, "Receive Visibilities ending");

}
