#include "rdma_transport.hpp"
extern "C" {
#include "RDMAapi.h"
//#include "RDMAmemorymanager.h"
//#include "RDMAexchangeidcallbacks.h"
}

struct RdmaTransport {
  
  /*
    set up default values that might be overriden when creating a instance of the class 
    these setups are from outside
*/
  enum logType requestLogLevel = LOG_NOTICE;
  enum runMode mode = RECV_MODE;
  uint32_t messageSize = 65536; /* size in bytes of single RDMA message */
  uint32_t numMemoryBlocks = 1; /* number of memory blocks to allocate for RDMA messages */
  uint32_t numContiguousMessages = 1; /* number of contiguous messages to hold in each memory block */
  std::string dataFileName = nullptr; /* default to not loading (sender) nor saving (receiver) to file the data memory blocks */
  uint64_t numTotalMessages = 0; /* total number of messages to send or receive, if 0 then default to numMemoryBlocks*numContiguousMessages */
  uint32_t messageDelayTime = 0; /* time in milliseconds to delay after each message send/receive posted, default is no delay */
  std::string rdmaDeviceName = nullptr; /* no preferred rdma device name to choose */
  uint8_t rdmaPort = 1;
  int gidIndex = -1; /* preferred gid index or -1 for no preference */
  std::string identifierFileName = nullptr; /* default to using stdio for exchanging RDMA identifiers */
  std::string metricURL = nullptr; /* default to not push metrics */
  uint32_t numMetricAveraging = 0; /* number of message completions over which to average metrics, default to numMemoryBlocks*numContiguousMessages */

  /* setup when class instance is created */
  struct ibv_context *rdmaDeviceContext;
  struct ibv_comp_channel *eventChannel;
  struct ibv_pd *protectionDomain;
  struct ibv_cq *receiveCompletionQueue;
  struct ibv_cq *sendCompletionQueue;
  struct ibv_qp *queuePair;
  MemoryRegionManager* manager;
  uint32_t queueCapacity;
  uint32_t maxInlineDataSize;
  void **memoryBlocks;

  struct ibv_recv_wr **receiveRequests;
  struct ibv_send_wr **sendRequests;

  /* setup for loop enqueueing blocks of work requests and polling for blocks of work completions */
  uint32_t minWorkRequestEnqueue;
  uint32_t maxWorkRequestDequeue;
  struct ibv_wc *workCompletions = NULL;
  bool *completionsStatus = nullptr;
  uint32_t currentQueueLoading = 0; /* no work requests initially in queue */
  uint64_t numWorkRequestsEnqueued = 0;
  uint32_t previousImmediateData;
  uint64_t numWorkRequestsMissing = 0; /* determined by finding non-incrementing immediate data values */
  uint32_t regionIndex = 0;
  uint64_t numWorkRequestCompletions = 0;

  /* initialise receiver metrics */
  uint64_t metricMessagesTransferred = 0;
  clock_t metricStartClockTime = -1;
  uint64_t metricWorkRequestsMissing = 0;
  
  /* initialise sender metrics */
  struct ibv_recv_wr *badReceiveRequest = NULL;
  struct ibv_send_wr *badSendRequest = NULL;
  struct timeval metricStartWallTime;
    
  RdmaTransport(enum logType _requestLogLevel,
		enum runMode _mode,
		uint32_t _messageSize,
		uint32_t _numMemoryBlocks,
		uint32_t _numContiguousMessages,
		const std::string &_dataFileName,
		uint64_t _numTotalMessages,
		uint32_t _messageDelayTime,
		const std::string &_rdmaDeviceName,
		uint8_t _rdmaPort,
		int _gidIndex,
		const std::string &_identifierFileName,
		const std::string &_metricURL,
		uint32_t _numMetricAveraging) :
    requestLogLevel(_requestLogLevel),
    mode(_mode),
    messageSize(_messageSize),
    numMemoryBlocks(_numMemoryBlocks),
    numContiguousMessages(_numContiguousMessages),
    dataFileName(_dataFileName),
    numTotalMessages(_numTotalMessages),
    messageDelayTime(_messageDelayTime),
    rdmaDeviceName(_rdmaDeviceName),
    rdmaPort(_rdmaPort),
    gidIndex(_gidIndex),
    identifierFileName(_identifierFileName),
    metricURL(_metricURL),
    numMetricAveraging(_numMetricAveraging)   
  {
    if (numTotalMessages == 0)
      numTotalMessages = numMemoryBlocks * numContiguousMessages;
    if (numMetricAveraging == 0)
      numMetricAveraging = numMemoryBlocks * numContiguousMessages;
    setLogLevel(requestLogLevel);

    if (mode == RECV_MODE)
      {
        logger(LOG_INFO, "Receive Visibilities starting as receiver");
      }
    else
      {
        logger(LOG_INFO, "Receive Visibilities starting as sender");
      }

    /* allocate memory blocks as buffers to be used in the memory regions */
    uint64_t memoryBlockSize = messageSize * numContiguousMessages;
    memoryBlocks = allocateMemoryBlocks(memoryBlockSize, &numMemoryBlocks);

    /* create a memory region manager to manage the usage of the memory blocks */
    /* note for simplicity memory region manager doesn't monitor memory regions so this code doesn't have to */
    /* load memory blocks with new data during sending/receiving */
    manager = createMemoryRegionManager(memoryBlocks,
					messageSize, numMemoryBlocks, numContiguousMessages, numTotalMessages, false);

    /* if in send mode then populate the memory blocks and display contents to stdio */
    if (mode == RECV_MODE)
      {
        /* clear each memory block */
        for (uint32_t blockIndex=0; blockIndex<numMemoryBlocks; blockIndex++)
	  {
            memset(memoryBlocks[blockIndex], 0, memoryBlockSize);
	  }
        setAllMemoryRegionsPopulated(manager, false);
      }
    else
      {
        if ((char*)dataFileName.c_str() != nullptr)
	  {
            // read data from dataFileName.0, dataFileName.1, ... into memory blocks
            if (!readFilesIntoMemoryBlocks(manager, (char*)dataFileName.c_str()))
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

    /* create a pointer to an identifier exchange function (see eg RDMAexchangeidentifiers for some examples) */
    enum exchangeResult (*identifierExchangeFunction)(bool isSendMode,
						      uint32_t packetSequenceNumber, uint32_t queuePairNumber, union ibv_gid gidAddress, uint16_t localIdentifier,
						      uint32_t *remotePSNPtr, uint32_t *remoteQPNPtr, union ibv_gid *remoteGIDPtr, uint16_t *remoteLIDPtr) = nullptr;
    if ((char*)identifierFileName.c_str() != nullptr)
      {
        setIdentifierFileName((char*)identifierFileName.c_str());
        identifierExchangeFunction = exchangeViaSharedFiles;
      }
    if (numMetricAveraging == 0)
      numMetricAveraging = manager->numMemoryRegions * manager->numContiguousMessages;

    logger(LOG_INFO, "Receive Visibilities starting");
    if (mode == RECV_MODE)
      logger(LOG_INFO, "Running as receiver");
    else
      logger(LOG_INFO, "Running as sender");

    /* initialise libibverb data structures so have fork() protection */
    /* note this has a performance hit, and is optional */
    // enableForkProtection();

    /* allocate all the RDMA resources */
    queueCapacity = manager->numContiguousMessages * manager->numMemoryRegions ; /* minimum capacity for completion queues */
    maxInlineDataSize = 0; /* NOTE put back at 236 once testing completed */;

    if (allocateRDMAResources((char*)rdmaDeviceName.c_str(), rdmaPort, queueCapacity, maxInlineDataSize,
			      &rdmaDeviceContext, &eventChannel, &protectionDomain, &receiveCompletionQueue,
			      &sendCompletionQueue, &queuePair) != SUCCESS)
      {
        logger(LOG_CRIT, "Unable to allocate the RDMA resources");
        exit(FAILURE);
      }
    else
      logger(LOG_DEBUG, "RDMA resources allocated");

    /* set up the RDMA connection */
    uint16_t localIdentifier;
    union ibv_gid gidAddress;
    enum ibv_mtu mtu;
    if (setupRDMAConnection(rdmaDeviceContext, rdmaPort, &localIdentifier,
			    &gidAddress, &gidIndex, &mtu) != SUCCESS)
      {
        logger(LOG_CRIT, "Unable to set up the RDMA connection");
        exit(FAILURE);
      }
    else
      logger(LOG_DEBUG, "RDMA connection set up");

    logger(LOG_INFO, "RDMA connection initialised on local %s", mode ? "sender" : "receiver");

    /* initialise the random number generator to generate a 24-bit psn */
    srand(getpid() * time(nullptr));
    uint32_t packetSequenceNumber = (uint32_t) (rand() & 0x00FFFFFF);

    /* exchange the necessary information with the remote RDMA peer */
    uint32_t queuePairNumber = queuePair->qp_num;
    uint32_t remotePSN;
    uint32_t remoteQPN;
    union ibv_gid remoteGID;
    uint16_t remoteLID;
    if (identifierExchangeFunction == nullptr)
      {
        if (exchangeViaStdIO(mode==SEND_MODE,
			     packetSequenceNumber, queuePairNumber, gidAddress, localIdentifier,
			     &remotePSN, &remoteQPN, &remoteGID, &remoteLID) != EXCH_SUCCESS)
	  {
            logger(LOG_CRIT, "Unable to exchange identifiers via standard IO");
            exit(FAILURE);
	  }
      }
    else
      {
        if (identifierExchangeFunction(mode==SEND_MODE,
				       packetSequenceNumber, queuePairNumber, gidAddress, localIdentifier,
				       &remotePSN, &remoteQPN, &remoteGID, &remoteLID) != EXCH_SUCCESS)
	  {
            logger(LOG_CRIT, "Unable to exchange identifiers via provided exchange function");
            exit(FAILURE);
	  }

      }

    /* modify the queue pair to be ready to receive and possibly send */
    if (modifyQueuePairReady(queuePair, rdmaPort, gidIndex, mode, packetSequenceNumber,
			     remotePSN, remoteQPN, remoteGID, remoteLID, mtu) != SUCCESS)
      {
        logger(LOG_CRIT, "Unable to modify queue pair to be ready");
        exit(FAILURE);
      }
    else
      logger(LOG_DEBUG, "Queue pair modified to be ready");

    /* register memory blocks as memory regions for buffer */
    if (registerMemoryRegions(protectionDomain, manager) != SUCCESS)
      {
        logger(LOG_CRIT, "Unable to allocate memory regions");
        exit(FAILURE);
      }

    /* perform the rdma data transfer */
    if (ibv_req_notify_cq(receiveCompletionQueue, 0) != SUCCESS)
      {
        logger(LOG_ERR, "Unable to request receive completion queue notifications");
        if (mode == RECV_MODE)
	  exit(FAILURE);
      }
    if (ibv_req_notify_cq(sendCompletionQueue, 0) != SUCCESS)
      {
        logger(LOG_WARNING, "Unable to request send completion queue notifications");
      }

    /* now create work requests*/
    setupRequests();

    /* Now setup for work completions */
    setupCompletions();
  }

  void setupRequests()
  {

    if (mode == RECV_MODE)
      {
	/* prepare scatter/gather array specifying buffers to read from or write into */
	/* note this implementation uses only one sgItem per work request but chains together */
	/* multiple work requests when numContiguousMessages > 1 */
	/* Alternatively, could instead use multiple sge, but then need to adjust queue pair specs */
	struct ibv_sge sgItems[manager->numMemoryRegions][manager->numContiguousMessages];
	memset(sgItems, 0, sizeof(struct ibv_sge[manager->numMemoryRegions][manager->numContiguousMessages]));
	int sgListLength = 1; /* only one sgItem per work request */
      
	/* Setup memory space for requests and reset it to 0*/
	receiveRequests = (ibv_recv_wr**) calloc(manager->numMemoryRegions, sizeof(ibv_recv_wr*));
	for(int i = 0; i < manager->numMemoryRegions; i++){
	  receiveRequests[i] = (ibv_recv_wr*) calloc(manager->numContiguousMessages, sizeof(ibv_recv_wr));
	}      
	memset(receiveRequests, 0, sizeof(struct ibv_recv_wr[manager->numMemoryRegions][manager->numContiguousMessages]));
	for (uint32_t regionIndex=0; regionIndex<manager->numMemoryRegions; regionIndex++)
	  {
	    for (uint32_t i=0; i<manager->numContiguousMessages; i++)
	      {
		/* prepare one sgItem to receive */
		sgItems[regionIndex][i].addr = (uint64_t)(manager->memoryRegions[regionIndex]->addr + i*manager->messageSize);
		sgItems[regionIndex][i].length = manager->messageSize;
		sgItems[regionIndex][i].lkey = manager->memoryRegions[regionIndex]->lkey;

		/* prepare one workRequest with this sgItem */
		receiveRequests[regionIndex][i].wr_id = (uint64_t)(regionIndex*manager->numContiguousMessages + i); /* gives memory region and location */
		receiveRequests[regionIndex][i].sg_list = &sgItems[regionIndex][i];
		receiveRequests[regionIndex][i].num_sge = sgListLength;
		if (i == manager->numContiguousMessages-1)
		  {
		    receiveRequests[regionIndex][i].next = NULL; /* note can chain multiple workRequest together for performance */
		  }
		else
		  {
		    receiveRequests[regionIndex][i].next = &receiveRequests[regionIndex][i+1]; /* chain multiple workRequest together for performance */
		  }
	      }
	  }
      }
    else
      {
	/* prepare scatter/gather array specifying buffers to read from or write into */
	/* note this implementation uses only one sgItem per work request but chains together */
	/* multiple work requests when numContiguousMessages > 1 */
	/* Alternatively, could instead use multiple sge, but then need to adjust queue pair specs */
	struct ibv_sge sgItems[manager->numMemoryRegions][manager->numContiguousMessages];
	memset(sgItems, 0, sizeof(struct ibv_sge[manager->numMemoryRegions][manager->numContiguousMessages]));
	int sgListLength = 1; /* only one sgItem per work request */

	/* Setup memory space for requests and reset it to 0*/
	sendRequests = (ibv_send_wr**) calloc(manager->numMemoryRegions, sizeof(ibv_send_wr*));
	for(int i = 0; i < manager->numMemoryRegions; i++){
	  sendRequests[i] = (ibv_send_wr*) calloc(manager->numContiguousMessages, sizeof(ibv_recv_wr));
	}
	memset(sendRequests, 0, sizeof(struct ibv_send_wr[manager->numMemoryRegions][manager->numContiguousMessages]));
	for (uint32_t regionIndex=0; regionIndex<manager->numMemoryRegions; regionIndex++)
	  {
	    for (uint32_t i=0; i<manager->numContiguousMessages; i++)
	      {
		/* prepare one sgItem to send */
		sgItems[regionIndex][i].addr = (uint64_t)(manager->memoryRegions[regionIndex]->addr + i*manager->messageSize);
		sgItems[regionIndex][i].length = manager->messageSize;
		sgItems[regionIndex][i].lkey = manager->memoryRegions[regionIndex]->lkey;

		/* prepare one workRequest with this sgItem */
		sendRequests[regionIndex][i].wr_id = (uint64_t)(regionIndex*manager->numContiguousMessages + i); /* gives memory region and location */
		sendRequests[regionIndex][i].sg_list = &sgItems[regionIndex][i];
		sendRequests[regionIndex][i].num_sge = sgListLength;
		sendRequests[regionIndex][i].opcode = IBV_WR_SEND_WITH_IMM;
		if (manager->messageSize <= maxInlineDataSize)
		  sendRequests[regionIndex][i].send_flags |= IBV_SEND_INLINE;
		/* optional immediate data sent along with payload, set to be incrementing values with zero first as reset */
		sendRequests[regionIndex][i].imm_data = (uint32_t)0; /* set appropriately prior to each ibv_post_send */
		if (i == manager->numContiguousMessages-1)
		  {
		    sendRequests[regionIndex][i].next = NULL; /* note can chain multiple workRequest together for performance */
		    sendRequests[regionIndex][i].send_flags = 0; // IBV_SEND_SIGNALED; /* include signal with last work request */
		  }
		else
		  {
		    sendRequests[regionIndex][i].next = &sendRequests[regionIndex][i+1]; /* chain multiple workRequest together for performance */
		  }
	      }
	  }
      }    
  }

  void setupCompletions()
  {    
    /* setup for loop enqueueing blocks of work requests and polling for blocks of work completions */
    minWorkRequestEnqueue = (uint32_t) ceil(queueCapacity * MIN_WORK_REQUEST_ENQUEUE);
    maxWorkRequestDequeue = queueCapacity; /* try to completely drain queue of any completed work requests */
    workCompletions = (struct ibv_wc*)malloc(sizeof(struct ibv_wc) * maxWorkRequestDequeue);
    if (workCompletions == NULL)
      {
	logger(LOG_ERR, "Receiver unable to allocate desired memory for work completions");
	exit(FAILURE);
      }

    completionsStatus = (bool*)malloc(sizeof(bool) * maxWorkRequestDequeue);
    if (completionsStatus == NULL)
      {
	logger(LOG_ERR, "Receiver unable to allocate desired memory for completion status");
	exit(FAILURE);
      }
    
    setAllMemoryRegionsEnqueued(manager, false); 
    if ((char*)metricURL.c_str() != NULL)
      {
	initialiseMetricReporter();
      }
    currentQueueLoading = 0; /* no work requests initially in queue */
    numWorkRequestsEnqueued = 0;
    previousImmediateData = UINT32_MAX;
    if (previousImmediateData+(uint32_t)1 != (uint32_t)0)
      {
	logger(LOG_ERR, "Assertion that UINT32_MAX+1 == 0 has failed, so error checking immediate values");
      }
    numWorkRequestsMissing = 0; /* determined by finding non-incrementing immediate data values */
    regionIndex = 0;
    numWorkRequestCompletions = 0;

    /* initialise metrics */
    metricMessagesTransferred = 0;
    metricWorkRequestsMissing = 0;
  
    /* initialise sender metrics */
    metricStartClockTime = clock();
    gettimeofday(&metricStartWallTime, NULL);
  }

  /*
    This function only issue requests for minWorkRequestEnqueue
   */
  void issueRequests(){

    if ( mode == RECV_MODE)
      {	
        /* post block of receive work requests to sufficiently full work request queue */
        while ((numWorkRequestsEnqueued + numWorkRequestsMissing < manager->numTotalMessages)
	       && (currentQueueLoading < minWorkRequestEnqueue))
	  {
            if (ibv_post_recv(queuePair, &receiveRequests[regionIndex][0], &badReceiveRequest) != SUCCESS)
	      {
                logger(LOG_ERR, "Unable to post receive request with error %d", errno);
	      }
            else
	      {
                logger(LOG_INFO, "Receiver has posted work requests for region %" PRIu32, regionIndex);
                /* check that the memory region isn't already populated with data */
                if (manager->isMonitoringRegions && getMemoryRegionPopulated(manager, regionIndex))
		  {
		    logger(LOG_WARNING, "Memory region %" PRIu32 " has been enqueued for receiving"
			   " but already populated while enqueueing work request %" PRIu64,
			   regionIndex, numWorkRequestsEnqueued + numWorkRequestsMissing);
		  }
                currentQueueLoading += manager->numContiguousMessages;
                numWorkRequestsEnqueued += manager->numContiguousMessages;
                setMemoryRegionEnqueued(manager, regionIndex, true);
                regionIndex++;
                if (regionIndex >= manager->numMemoryRegions)
		  regionIndex = 0;
                microSleep(messageDelayTime);
	      }
	  }
      }
    else
      {	
        /* post block of send work requests to sufficiently full work request queue */
        while ((numWorkRequestsEnqueued < manager->numTotalMessages) && (currentQueueLoading < minWorkRequestEnqueue))
	  {
            /* provide incrementing immediate data sent along with payload */
            for (uint32_t i=0; i<manager->numContiguousMessages; i++)
	      {
                sendRequests[regionIndex][i].imm_data = htonl(numWorkRequestsEnqueued+i);
	      }
            if (ibv_post_send(queuePair, &sendRequests[regionIndex][0], &badSendRequest) != SUCCESS)
	      {
                logger(LOG_ERR, "Unable to post send request with error %d", errno);
	      }
            else
	      {
                logger(LOG_INFO, "Sender has posted work requests for region %" PRIu32, regionIndex);
                /* check that the memory region is already populated with data */
                if (manager->isMonitoringRegions && !getMemoryRegionPopulated(manager, regionIndex))
		  {
                    logger(LOG_WARNING, "Memory region %" PRIu32 " has been enqueued for sending"
			   " without first being populated while enqueing work request %" PRIu64,
			   regionIndex, numWorkRequestsEnqueued);
		  }
                currentQueueLoading += manager->numContiguousMessages;
                numWorkRequestsEnqueued += manager->numContiguousMessages;
                setMemoryRegionEnqueued(manager, regionIndex, true);
                regionIndex++;
                if (regionIndex >= manager->numMemoryRegions)
		  regionIndex = 0;
                microSleep(messageDelayTime);
	      }
	  }
      }
  }

  
  /**********************************************************************
   * Waits until a completion event is received on the eventChannel
   * then acknowledge it and reset it to receive another notification that
   * may come in future on that completion queue
   * Note this function gets called by sendWorkRequests and receiveWorkRequests
   * so does not need to be called directly
   **********************************************************************/
  int waitForCompletionQueueEvent()
  {
    /* wait until get completion queue event */
    struct ibv_cq *eventCompletionQueue;
    void *eventContext;
    if (ibv_get_cq_event(eventChannel, &eventCompletionQueue, &eventContext) != SUCCESS)
      {
        logger(LOG_ERR, "Error while waiting for completion queue event");
        return FAILURE;
      }
    /* assert: eventCompletionQueue is either receiveCompletionQueue or sendCompletionQueue */
    /* assert: eventContext is rdmaDeviceContext */
    /* acknowledge the one completion queue event */
    ibv_ack_cq_events(eventCompletionQueue, 1);
    /* request notification for next completion event on the completion queue */
    if (ibv_req_notify_cq(eventCompletionQueue, 0) != SUCCESS)
      {
        logger(LOG_WARNING, "Error while receiver requesting completion notification");
      }
    return SUCCESS;
  }
  
  /*
    This function block previous work request until it finishes 
  */
  void waitRequestsCompletion()
  {
    if ( mode == RECV_MODE)
      {
        /* poll for block of work completions */
        logger(LOG_INFO, "Receiver waiting for completion %" PRIu64 " of %" PRIu64,
	       numWorkRequestCompletions + numWorkRequestsMissing, manager->numTotalMessages);
        if (waitForCompletionQueueEvent() != SUCCESS)
	  {
            logger(LOG_ERR, "Receiver unable to wait for completion notification");
	  }
      }
    else
      {
        logger(LOG_INFO, "Sender waiting for completion %" PRIu64 " of %" PRIu64, numWorkRequestCompletions, manager->numTotalMessages);
        if (waitForCompletionQueueEvent() != SUCCESS)
	  {
            logger(LOG_ERR, "Sender unable to wait for completion notification");
	  }
      }
  }

  void requestsData()
  {
    if (mode == RECV_MODE)
      {
	memset(workCompletions, 0, sizeof(struct ibv_wc) * maxWorkRequestDequeue);
	memset(completionsStatus, 0, sizeof(bool)*maxWorkRequestDequeue);
	
        int numCompletionsFound = 0;
        numCompletionsFound = ibv_poll_cq(receiveCompletionQueue, maxWorkRequestDequeue, workCompletions);
        if (numCompletionsFound < 0)
	  {
            logger(LOG_WARNING, "Receiver failed to poll for work completions");
	  }
        else
	  {
            logger(LOG_INFO, "Receiver has polled %d work completions", numCompletionsFound);
            for (int wcIndex=0; wcIndex<numCompletionsFound; wcIndex++)
	      {
                struct ibv_wc workCompletion = workCompletions[wcIndex];
                bool hasImmediateData = (workCompletion.wc_flags & IBV_WC_WITH_IMM) != 0;
                uint32_t immediateData = ntohl(workCompletion.imm_data);
                if (workCompletion.status != IBV_WC_SUCCESS)
		  {
                    logger(LOG_ERR,
			   "Receiver work completion error with status:%d, id:%" PRIu64
			   ", imm_data:%" PRIu32 ", syndrome:0x%x",
			   workCompletion.status, workCompletion.wr_id, immediateData,
			   workCompletion.vendor_err);
		    
		    completionsStatus[wcIndex] = false;
		  }
                else
		  {
                    uint32_t transferredRegionIndex = (uint32_t)(workCompletion.wr_id / manager->numContiguousMessages);
                    uint32_t transferredContiguousIndex = (uint32_t)(workCompletion.wr_id % manager->numContiguousMessages);
                    setMemoryRegionEnqueued(manager, transferredRegionIndex, false); /* note this gets reset for each of the contiguous messages */
                    setMemoryRegionPopulated(manager, transferredRegionIndex, true); /* note typically now want to utilise the data and then unpopulate region */
                    setMessageTransferred(manager, transferredRegionIndex, transferredContiguousIndex, numWorkRequestCompletions+numWorkRequestsMissing);
                    logger(LOG_DEBUG,
			   "Receiver work completion %" PRIu64 " success status with work request id %" PRIu64
			   " and immediate data %" PRIu32, numWorkRequestCompletions + numWorkRequestsMissing,
			   workCompletion.wr_id, immediateData);

		    completionsStatus[wcIndex] = true;
		  }
                if (hasImmediateData)
		  {
                    /* check immediate data value is incrementing contiguously */
                    if (immediateData == previousImmediateData)
		      {
                        /* identical work completion immediate data values received */
                        logger(LOG_WARNING, "Receiver work completion %" PRIu64 " immediate data %" PRIu32
			       " duplicated from previous messages", numWorkRequestCompletions + numWorkRequestsMissing,
			       immediateData);
		      }
                    else if (immediateData == previousImmediateData + (uint32_t)1) /* unsigned arithmetic with wraparound at UINT32_MAX */
		      {
                        /* immediateData has been incremented by one (possibly with wrap around) as expected */
		      }
                    else // if (immediateData - previousImmediateData > 1u) /* unsigned arithmetic with wraparound at UINT32_MAX */
		      {
                        /* there are missing work completions */
                        uint32_t numMissingFound = immediateData - (previousImmediateData+1u);
                        numWorkRequestsMissing += numMissingFound;
                        metricWorkRequestsMissing += numMissingFound;
                        logger(LOG_WARNING, "Receiver detected missing %" PRIu32
			       " message(s) while receiving work completion %" PRIu64,
			       numMissingFound, numWorkRequestCompletions + numWorkRequestsMissing);
		      }
                    previousImmediateData = immediateData;
		  }
                currentQueueLoading--;
                numWorkRequestCompletions++;
                metricMessagesTransferred++;
	      }
	  }

      }
    else
      {	
        /* poll the completion queue for work completions */
        /* NOTE perftest demo does not poll for number of workCompletions on sender */
        memset(workCompletions, 0, sizeof(struct ibv_wc) * maxWorkRequestDequeue);
	memset(completionsStatus, 0, sizeof(bool)*maxWorkRequestDequeue);
	
        int numCompletionsFound = 0;
        numCompletionsFound = ibv_poll_cq(sendCompletionQueue, maxWorkRequestDequeue, workCompletions);
        if (numCompletionsFound < 0)
	  {
            logger(LOG_WARNING, "Sender failed to poll for work completions"); 
	  }
        else
	  {
            logger(LOG_INFO, "Sender has polled %d work completions", numCompletionsFound);
            for (int wcIndex=0; wcIndex<numCompletionsFound; wcIndex++)
	      {
                struct ibv_wc workCompletion = workCompletions[wcIndex];
                if (workCompletion.status != IBV_WC_SUCCESS)
		  {
                    logger(LOG_ERR,
			   "Sender work completion error with status:%d, id:%" PRIu64 ", imm_data:%" PRIu32 ", syndrome:0x%x",
			   workCompletion.status, workCompletion.wr_id, workCompletion.imm_data, workCompletion.vendor_err);

		    completionsStatus[wcIndex] = false;
		  }
                else
		  {
                    /* note imm_data seems to not appear in sender's workCompletion */
                    uint32_t transferredRegionIndex = (uint32_t)(workCompletion.wr_id / manager->numContiguousMessages);
                    uint32_t transferredContiguousIndex = (uint32_t)(workCompletion.wr_id % manager->numContiguousMessages);
                    setMemoryRegionEnqueued(manager, transferredRegionIndex, false);
                    /* set memory region to be unpopulated so can be repopulated with further data */
                    setMemoryRegionPopulated(manager, transferredRegionIndex, false);
                    setMessageTransferred(manager, transferredRegionIndex, transferredContiguousIndex, numWorkRequestCompletions);
                    logger(LOG_DEBUG,
			   "Sender work completion %" PRIu64 " success status with work request id %" PRIu64,
			   numWorkRequestCompletions, workCompletion.wr_id);

		    completionsStatus[wcIndex] = true;
		  }
                currentQueueLoading--;
                numWorkRequestCompletions++;
                metricMessagesTransferred++;
	      }
	  }
	/* update sender metrics, including partial metrics for last iteration of loop */
        if ((metricMessagesTransferred >= numMetricAveraging) || (numWorkRequestCompletions >= manager->numTotalMessages))
	  {
            int queueUtilisation = (int)(100*currentQueueLoading/queueCapacity);
            logger(LOG_NOTICE, "Sender queue currently at %d%% capacity", queueUtilisation);
            uint64_t durationClockMicroSec = (uint64_t)((clock() - metricStartClockTime) * 1000000L / CLOCKS_PER_SEC);
            struct timeval endWallTime;
            gettimeofday(&endWallTime, NULL);
            uint64_t durationWallMicroSec = (endWallTime.tv_sec - metricStartWallTime.tv_sec) * 1000000L
	      + (endWallTime.tv_usec-metricStartWallTime.tv_usec);
            int cpuUtilisation = (int)round(100.0*durationClockMicroSec/durationWallMicroSec); /* only approx measure */
            uint64_t metricBytesTransferred = manager->messageSize * metricMessagesTransferred;
            double bandwidth = 8.0E-3 * metricBytesTransferred / durationWallMicroSec; /* Gbps */
            logger(LOG_NOTICE, "Sender wall time duration is %" PRIu64 " milliseconds "
		   "and CPU clock time duration is %" PRIu64 " milliseconds (%d%% utilisation)",
		   durationWallMicroSec/1000, durationClockMicroSec/1000, cpuUtilisation);
            logger(LOG_NOTICE, "Sender bandwidth is %.2f Gbps", bandwidth);
            uint64_t millisecondsSinceEpoch = endWallTime.tv_sec*1000L + endWallTime.tv_usec/1000;
            if ((char*)metricURL.c_str() != NULL)
	      {
                pushMetrics("RDMAsender", (char*)metricURL.c_str(), queueUtilisation, cpuUtilisation, bandwidth,
			    metricMessagesTransferred, 0, millisecondsSinceEpoch, manager);
	      }
            /* reset sender metrics */
            metricMessagesTransferred = 0;
            metricStartClockTime = clock();
            gettimeofday(&metricStartWallTime, NULL);
	  }
      }
  }
  
  virtual ~RdmaTransport()
  {
    /* if in receive mode then display contents to stdio */
    if (mode == RECV_MODE)
      {
        if ((char *)dataFileName.c_str() != nullptr)
    	  {
            // write data to dataFileName.0, dataFileName.1, ... from memory blocks
            if (!writeFilesFromMemoryBlocks(manager, (char*)dataFileName.c_str()))
    	      {
                logger(LOG_WARNING, "Unsuccessful write of data files");
    	      }
    	  }
        displayMemoryBlocks(manager, 10, 10); /* display contents that were received */
      }

    if ((char *)metricURL.c_str() != NULL)
      {
        cleanupMetricReporter();
      }
    
    free(workCompletions);
    free(completionsStatus);

    deregisterMemoryRegions(manager);

    cleanupRDMAResources(rdmaDeviceContext, eventChannel, protectionDomain,
			 receiveCompletionQueue, sendCompletionQueue, queuePair);

    destroyMemoryRegionManager(manager);

    /* free memory block buffers */
    freeMemoryBlocks(memoryBlocks, numMemoryBlocks);

    logger(LOG_INFO, "Receive Visibilities ending");
  }  
  
  int addition(int a, int b){
    return a+b;
  }
  
  void say_hello() {
    printf("Hello! \n");
  }
};

PYBIND11_MODULE(rdma_transport, m) {
  m.doc() = R"pbdoc(
    RDMA transport pluggin
        -----------------------

        .. currentmodule:: CRACO

        .. autosummary::
           :toctree: _generate


    )pbdoc";

  py::enum_<runMode>(m, "runMode")
    .value("RECV_MODE", runMode{RECV_MODE})
    .value("SEND_MODE", runMode{SEND_MODE})
    .export_values();
    
  py::enum_<logType>(m, "logType")
    .value("LOG_EMERG",   logType{LOG_EMERG})
    .value("LOG_ALERT",   logType{LOG_ALERT})
    .value("LOG_CRIT",    logType{LOG_CRIT})
    .value("LOG_ERR",     logType{LOG_ERR})
    .value("LOG_WARNING", logType{LOG_WARNING})
    .value("LOG_NOTICE",  logType{LOG_NOTICE})
    .value("LOG_INFO",    logType{LOG_INFO})
    .value("LOG_DEBUG",   logType{LOG_DEBUG})
    .export_values();
    
  py::class_<RdmaTransport>(m, "RdmaTransport")
    .def(py::init<enum logType,
	 enum runMode,
	 uint32_t, 
	 uint32_t, 
	 uint32_t, 
	 const std::string &, 
	 uint64_t, 
	 uint32_t, 
	 const std::string &, 
	 uint8_t, 
	 int, 
	 const std::string &, 
	 const std::string &,
	 uint32_t>())

    .def("requestsData", &RdmaTransport::requestsData)
    .def("issueRequests", &RdmaTransport::issueRequests)
    .def("waitRequestsCompletion", &RdmaTransport::waitRequestsCompletion)
    .def("say_hello", &RdmaTransport::say_hello)
    .def("addition", &RdmaTransport::addition);
  
#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
