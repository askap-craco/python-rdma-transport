#include "rdma_transport.hpp"
#include "rdma_transport_simpletest.hpp"
#include <poll.h>

extern "C" {
#include "RDMAapi.h"
}

// Custom exception class for timeouts
class TimeoutException : public std::runtime_error {
  public:
      explicit TimeoutException(const std::string& message)
          : std::runtime_error(message) {}
  };

// How to print on terminal, okay
// How to get c++ exception with python, done
// How to understand c++ struct with python, done

struct RdmaTransport {
  
  /*
    set up default values that might be overriden when creating a instance of the class 
    these setups are from outside
  */
  enum runMode mode = RECV_MODE;
  uint32_t messageSize = 65536; /* size in bytes of single RDMA message */
  uint32_t numMemoryBlocks = 1; /* number of memory blocks to allocate for RDMA messages */
  uint32_t numContiguousMessages = 1; /* number of contiguous messages to hold in each memory block */
  uint64_t numTotalMessages = 0; /* total number of messages to send or receive, if 0 then default to numMemoryBlocks*numContiguousMessages */
  uint32_t messageDelayTime = 0; /* time in milliseconds to delay after each message send/receive posted, default is no delay */
  char* rdmaDeviceName = nullptr; /* no preferred rdma device name to choose */
  uint8_t rdmaPort = 1;
  int gidIndex = -1; /* preferred gid index or -1 for no preference */
  bool checkImmediate = false;

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
  
  uint32_t currentQueueLoading = 0; /* no work requests initially in queue */
  uint64_t numWorkRequestsEnqueued = 0;
  uint32_t previousImmediateData;
  uint64_t numWorkRequestsMissing = 0; /* determined by finding non-incrementing immediate data values */
  uint32_t regionIndex = 0;
  uint64_t numWorkRequestCompletions = 0;

  /* initialise sender metrics */
  struct ibv_recv_wr *badReceiveRequest = nullptr;
  struct ibv_send_wr *badSendRequest = nullptr;

  /* These two will be accessed outside*/
  int numCompletionsFound;
  uint32_t numMissingFound;
  std::vector<struct ibv_wc> workCompletions; // resize it in contructor

  /* these sgItems need to are setup in setupRequests */
  struct ibv_sge** sgItems;

  /* information for remote machine to pair with */
  uint32_t packetSequenceNumber;
  uint32_t queuePairNumber;
  union ibv_gid gidAddress;
  uint16_t localIdentifier;

  /* information from remote machine */
  uint32_t remotePSN;
  uint32_t remoteQPN;
  union ibv_gid remoteGID;
  uint16_t remoteLID;
  
  enum ibv_mtu mtu;

  /* timeout in milliseconds for a wait call*/
  int timeoutMillis;
  
  RdmaTransport(enum runMode _mode,
		uint32_t _messageSize,
		uint32_t _numMemoryBlocks,
		uint32_t _numContiguousMessages,
		uint64_t _numTotalMessages,
		uint32_t _messageDelayTime,
		char* _rdmaDeviceName,
		uint8_t _rdmaPort,
		int _gidIndex) :
    mode(_mode),
    messageSize(_messageSize),
    numMemoryBlocks(_numMemoryBlocks),
    numContiguousMessages(_numContiguousMessages),
    numTotalMessages(_numTotalMessages),
    messageDelayTime(_messageDelayTime),
    rdmaDeviceName(_rdmaDeviceName),
    rdmaPort(_rdmaPort),
    gidIndex(_gidIndex)
  {
    timeoutMillis = 0;  /* no timeout by default */
    if (numTotalMessages == 0)
      numTotalMessages = numMemoryBlocks * numContiguousMessages;
    
    if (mode == RECV_MODE)
      {
	fprintf(stdout, "INFO:\tReceive Visibilities starting as receiver\n");
      }
    else
      {
	fprintf(stdout, "INFO:\tReceive Visibilities starting as sender\n");
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
	/* put something into each allocated memory block to test sending */
	for (uint32_t blockIndex=0; blockIndex<numMemoryBlocks; blockIndex++)
	  {
	    for (uint64_t i=0; i<memoryBlockSize; i++)
	      {
		((unsigned char*)memoryBlocks[blockIndex])[i]
		  = (unsigned char)(i+(blockIndex*memoryBlockSize));
	      }
	  }
	setAllMemoryRegionsPopulated(manager, true);
        //displayMemoryBlocks(manager, 10, 10); /* display contents that will send */
      }

    fprintf(stdout, "INFO:\tReceive Visibilities starting");
    if (mode == RECV_MODE)
      fprintf(stdout, "INFO:\tRunning as receiver\n");
    else
      fprintf(stdout, "INDO:\tRunning as sender\n");

    /* initialise libibverb data structures so have fork() protection */
    /* note this has a performance hit, and is optional */
    //enableForkProtection();

    /* allocate all the RDMA resources */
    queueCapacity = manager->numContiguousMessages * manager->numMemoryRegions ; /* minimum capacity for completion queues */
    maxInlineDataSize = 0; /* NOTE put back at 236 once testing completed */;

    if (allocateRDMAResources(rdmaDeviceName, rdmaPort, queueCapacity, maxInlineDataSize,
			      &rdmaDeviceContext, &eventChannel, &protectionDomain, &receiveCompletionQueue,
			      &sendCompletionQueue, &queuePair) != SUCCESS)
      {
    	fprintf(stderr, "ERR:\tUnable to allocate the RDMA resources\n");
	throw std::runtime_error( "ERR:\tUnable to allocate the RDMA resources");
      }
    
    /* set up the RDMA connection */
    if (setupRDMAConnection(rdmaDeviceContext, rdmaPort, &localIdentifier,
			    &gidAddress, &gidIndex, &mtu) != SUCCESS)
      {
	fprintf(stderr, "ERR:\tUnable to set up the RDMA connection\n");
	throw std::runtime_error( "ERR:\tUnable to set up the RDMA connection");
      }

    fprintf(stdout, "INFO:\tRDMA connection initialised on local %s", mode ? "sender" : "receiver\n");

    /* initialise the random number generator to generate a 24-bit psn */
    srand(getpid() * time(nullptr));
    packetSequenceNumber = (uint32_t) (rand() & 0x00FFFFFF);

    /* exchange the necessary information with the remote RDMA peer */
    queuePairNumber = queuePair->qp_num;
  }

  void setupRdma(const char* identifierFileName)
  {
    fprintf(stdout, "identifier file name is %s\n", identifierFileName);
    
    /* We either exchange information with shared file or with messages, no stdio anymore */ 
    if (identifierFileName != nullptr)
      {
        setIdentifierFileName(identifierFileName);
    	
    	fprintf(stdout, "I am here???\n");
    	if (exchangeViaSharedFiles(mode==SEND_MODE,
    				   packetSequenceNumber, queuePairNumber, gidAddress, localIdentifier,
    				   &remotePSN, &remoteQPN, &remoteGID, &remoteLID) != EXCH_SUCCESS)
    	  {
    	    fprintf(stderr, "ERR:\tUnable to exchange identifiers via provided exchange function\n");
    	    throw std::runtime_error("ERR:\tUnable to exchange identifiers via provided exchange function\n");
    	  }
    	
      }
  
    /* modify the queue pair to be ready to receive and possibly send */
    if (modifyQueuePairReady(queuePair, rdmaPort, gidIndex, mode, packetSequenceNumber,
			     remotePSN, remoteQPN, remoteGID, remoteLID, mtu) != SUCCESS)
      {
	fprintf(stderr, "ERR:\tUnable to modify queue pair to be ready\n");
	throw std::runtime_error("ERR:\tUnable to modify queue pair to be ready\n");
      }
    
    /* register memory blocks as memory regions for buffer */
    if (registerMemoryRegions(protectionDomain, manager) != SUCCESS)
      {
	fprintf(stderr, "ERR:\tUnable to allocate memory regions\n");
	throw std::runtime_error("ERR:\tUnable to allocate memory regions\n");
      }
    
    /* perform the rdma data transfer */
    if (mode == RECV_MODE)
      {
	if (ibv_req_notify_cq(receiveCompletionQueue, 0) != SUCCESS)
	  {
	    fprintf(stderr, "ERR:\tUnable to request receive completion queue notifications\n");
	    throw std::runtime_error("ERR:\tUnable to request receive completion queue notifications\n");
	  }
      }
    else
      {
	if (ibv_req_notify_cq(sendCompletionQueue, 0) != SUCCESS)
	  {
	    fprintf(stdout, "WARN:\tUnable to request send completion queue notifications\n");
	  }
      }

    // setup buffer for work requests that lives on the heap. The original one was on the stack
    sgItems = new struct ibv_sge*[manager->numMemoryRegions];
    for(auto iregion = 0; iregion < manager->numMemoryRegions; iregion++) {
      sgItems[iregion] = new struct ibv_sge[manager->numContiguousMessages];
    }


    /* now create work requests*/
    setupRequests();
    fprintf(stdout, "DEBUG:\tRequests setup done\n");
    
    /* Now setup for work completions */
    setupCompletions();
    fprintf(stdout, "DEBUG:\tCompletions setup done\n");
  }

  uint32_t getPacketSequenceNumber()
  {
    return packetSequenceNumber;
  }
  
  uint32_t getQueuePairNumber()
  {
    return queuePairNumber;
  }
  
  uint16_t getLocalIdentifier()
  {
    return localIdentifier;
  }

  void setPacketSequenceNumber(uint32_t _remotePSN)
  {
    remotePSN = _remotePSN;
  }
  
  void setQueuePairNumber(uint32_t _remoteQPN)
  {
    remoteQPN = _remoteQPN;
  }
  
  void setLocalIdentifier(uint16_t _remoteLID)
  {
    remoteLID = _remoteLID;
  }
  
  void setGidAddress(py::array_t<uint8_t>& _remoteGID_raw)
  {
    py::buffer_info remoteGID_raw = _remoteGID_raw.request();
  
    uint8_t* ptr = (uint8_t*)remoteGID_raw.ptr;
    
    for(int i = 0; i < 16; i++){
      remoteGID.raw[i] = ptr[i];
    }
  }

  void setupRequests()
  {
    /* prepare scatter/gather array specifying buffers to read from or write into */
    /* note this implementation uses only one sgItem per work request but chains together */
    /* multiple work requests when numContiguousMessages > 1 */
    /* Alternatively, could instead use multiple sge, but then need to adjust queue pair specs */

    int sgListLength = 1; /* only one sgItem per work request */
    assert(mode == RECV_MODE || mode == SEND_MODE);
	
    if (mode == RECV_MODE)
      {      
	/* Setup memory space for requests and reset it to 0*/
	assert(manager->numMemoryRegions > 0);
	receiveRequests = (ibv_recv_wr**) calloc(manager->numMemoryRegions, sizeof(ibv_recv_wr*));
	if(receiveRequests == nullptr)
	  {
	    throw std::runtime_error("ERR:\tCould not calloc receive requests\n");
	  }
	
	for(int i = 0; i < manager->numMemoryRegions; i++)
	  {
            assert(manager->numContiguousMessages > 0);
	    receiveRequests[i] = (ibv_recv_wr*) calloc(manager->numContiguousMessages, sizeof(ibv_recv_wr));
	    if(receiveRequests[i] == nullptr)
	      {
		throw std::runtime_error("ERR:\tCould not calloc receive requests\n");
	      }
	  }      
	
	for (uint32_t regionIndex=0; regionIndex<manager->numMemoryRegions; regionIndex++)
	  {
	    for (uint32_t i=0; i<manager->numContiguousMessages; i++)
	      {
		assert(manager->messageSize > 0);
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
		    receiveRequests[regionIndex][i].next = nullptr; /* note can chain multiple workRequest together for performance */
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
	/* Setup memory space for requests and reset it to 0*/
	sendRequests = (ibv_send_wr**) calloc(manager->numMemoryRegions, sizeof(ibv_send_wr*));
	if(sendRequests == nullptr)
	  {
	    throw std::runtime_error("ERR:\tCould not calloc send requests\n");
	  }
	
	for(int i = 0; i < manager->numMemoryRegions; i++)
	  {
	    sendRequests[i] = (ibv_send_wr*) calloc(manager->numContiguousMessages, sizeof(ibv_send_wr));
	  
	    if(sendRequests[i] == nullptr)
	      {
		throw std::runtime_error("ERR:\tCould not calloc send requests\n");
	      }
	  }
	
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
		    sendRequests[regionIndex][i].next = nullptr; /* note can chain multiple workRequest together for performance */
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
    assert(queueCapacity > 0);
    /* setup for loop enqueueing blocks of work requests and polling for blocks of work completions */
    minWorkRequestEnqueue = (uint32_t) ceil(queueCapacity * MIN_WORK_REQUEST_ENQUEUE);
    maxWorkRequestDequeue = queueCapacity; /* try to completely drain queue of any completed work requests */
    //workCompletions.resize(maxWorkRequestDequeue);
    
    currentQueueLoading = 0; /* no work requests initially in queue */
    numWorkRequestsEnqueued = 0;
    previousImmediateData = UINT32_MAX;
    if (previousImmediateData+(uint32_t)1 != (uint32_t)0)
      {
	throw std::runtime_error("ERR:\tAssertion that UINT32_MAX+1 == 0 has failed, so error checking immediate values\n");
      }
    numWorkRequestsMissing = 0; /* determined by finding non-incrementing immediate data values */
    regionIndex = 0;
    numWorkRequestCompletions = 0;
  }

  /*
    This function only issue requests for minWorkRequestEnqueue
  */
  void issueRequests(){
    assert(mode == RECV_MODE || mode == SEND_MODE);
    if ( mode == RECV_MODE)
      {	
        /* post block of receive work requests to sufficiently full work request queue */
        /* KB - it isn't clear what this check for missing work requests is trying to do
           When we have many missing work requets, we evneutally stup submitting more
           work requets (which happens when the immediate data isn't incrementing by 1,
           because it carries frameID
        */
        while ((numWorkRequestsEnqueued + numWorkRequestsMissing < manager->numTotalMessages)
	       && (currentQueueLoading < minWorkRequestEnqueue))
	  {
            assert(regionIndex < manager->numMemoryRegions);
            if (ibv_post_recv(queuePair, &receiveRequests[regionIndex][0], &badReceiveRequest) != SUCCESS)
	      {
		fprintf(stderr, "ERR:\tUnable to post receive request with error %d\n", errno);
		throw std::runtime_error("ERR:\tUnable to post receive request with error\n");
	      }
            else
	      {
                currentQueueLoading += manager->numContiguousMessages;
                numWorkRequestsEnqueued += manager->numContiguousMessages;
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
		fprintf(stderr, "ERR:\tUnable to post send request with error %d\n", errno);
		throw std::runtime_error("ERR:\tUnable to post send request with error");
	      }
            else
	      {
                currentQueueLoading += manager->numContiguousMessages;
                numWorkRequestsEnqueued += manager->numContiguousMessages;
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
   * If timeoutMillis > 0 then it will poll with the timeout and throw a TimeoutException 
   * error if it timed out
   **********************************************************************/
  int waitForCompletionQueueEvent()
  {
    /* wait until get completion queue event */
    struct ibv_cq *eventCompletionQueue;
    void *eventContext;
    assert(eventChannel != NULL);
    if (timeoutMillis > 0) {
      struct pollfd pollFd;
      pollFd.fd = eventChannel->fd;  // File descriptor for the event channel
      pollFd.events = POLLIN;        // Wait for input events
  
      int pollResult = poll(&pollFd, 1, timeoutMillis); // Poll with timeout
      if (pollResult < 0)
      {
        throw std::runtime_error("ERR:\tError while waiting for completion queue event\n");
      }
      else if (pollResult == 0)
      {
          throw TimeoutException("Timeout while waiting for completion queue event");
      }
    }     
    
    if (ibv_get_cq_event(eventChannel, &eventCompletionQueue, &eventContext) != SUCCESS)
      {	
	fprintf(stderr, "ERR:\tError while waiting for completion queue event\n");
	throw std::runtime_error("ERR:\tError while waiting for completion queue event\n");
      }
    
    /* assert: eventCompletionQueue is either receiveCompletionQueue or sendCompletionQueue */
    /* assert: eventContext is rdmaDeviceContext */
    /* acknowledge the one completion queue event */
    ibv_ack_cq_events(eventCompletionQueue, 1);
        
    /* request notification for next completion event on the completion queue */
    if (ibv_req_notify_cq(eventCompletionQueue, 0) != SUCCESS)
      {
    	fprintf(stdout, "WARN:\tError while receiver requesting completion notification\n");
      }

    return SUCCESS;
  }
  
  /*
    This function block previous work request until it finishes 
  */
  void waitRequestsCompletion()
  {    
    if (waitForCompletionQueueEvent() != SUCCESS)
      {
	fprintf(stderr, "ERR:\tunable to wait for completion notification\n");
	throw std::runtime_error("ERR:\tunable to wait for completion notification\n");
      }
  }
  
  void pollRequests()
  {    
    numCompletionsFound = 0;
    numMissingFound = 0;
    workCompletions.resize(maxWorkRequestDequeue);
    
    if (mode == RECV_MODE)
      {
	// We do not need to reset the size here as the size will be returned seperately as numCompletionsFound
	numCompletionsFound = ibv_poll_cq(receiveCompletionQueue, maxWorkRequestDequeue, workCompletions.data());

	if (numCompletionsFound < 0)
	  {
	    fprintf(stderr, "ERR:\tReceiver failed to poll for work completions");
	    throw std::runtime_error("ERR:\tReceiver failed to poll for work completions");
	  }
	else
	  {
	    for (int wcIndex=0; wcIndex<numCompletionsFound; wcIndex++)
	      {
		struct ibv_wc workCompletion = workCompletions[wcIndex];
		bool hasImmediateData = (workCompletion.wc_flags & IBV_WC_WITH_IMM) != 0;
		uint32_t immediateData = ntohl(workCompletion.imm_data);
		if (workCompletion.status != IBV_WC_SUCCESS)
		  {
		    fprintf(stderr, "ERR:\tReceiver work completion error with status:%d, id:%" PRIu64
			    ", imm_data:%" PRIu32 ", syndrome:0x%x",
			    workCompletion.status, workCompletion.wr_id, immediateData,
			    workCompletion.vendor_err);
		    throw std::runtime_error("ERR:\tReceiver work completion error");
		  }
		if (hasImmediateData && checkImmediate)
		  {
		    /* check immediate data value is incrementing contiguously */
		    if (immediateData == previousImmediateData)
		      {
			/* identical work completion immediate data values received */
			fprintf(stdout, "WARN:\tReceiver work completion %" PRIu64 " immediate data %" PRIu32
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
			numMissingFound = immediateData - (previousImmediateData+1u);
			numWorkRequestsMissing += numMissingFound;
			fprintf(stdout, "WARN:\tReceiver detected missing %" PRIu32
				" message(s) while receiving work completion %" PRIu64"\n",
				numMissingFound, numWorkRequestCompletions + numWorkRequestsMissing);
		      }
		    previousImmediateData = immediateData;
		  }
		currentQueueLoading--;
		numWorkRequestCompletions++;
	      }
	  }
      }
    else
      {
	// We do not need to reset the size here as the size will be returned seperately as numCompletionsFound
	numCompletionsFound = ibv_poll_cq(sendCompletionQueue, maxWorkRequestDequeue, workCompletions.data());
	if (numCompletionsFound < 0)
	  {
	    fprintf(stderr, "ERR:\tSender failed to poll for work completions");
	    throw std::runtime_error("ERR:\tSender failed to poll for work completions");
	  }
	else
	  {
	    for (int wcIndex=0; wcIndex<numCompletionsFound; wcIndex++)
	      {
		struct ibv_wc workCompletion = workCompletions[wcIndex];
		if (workCompletion.status != IBV_WC_SUCCESS)
		  {
		    fprintf(stderr, "ERR:\tSender work completion error with status:%d, id:%" PRIu64 ", imm_data:%" PRIu32 ", syndrome:0x%x",
			    workCompletion.status, workCompletion.wr_id, workCompletion.imm_data, workCompletion.vendor_err);
		    throw std::runtime_error("ERR:\tSender work completion error");
		  }
		currentQueueLoading--;
		numWorkRequestCompletions++;
	      }
	  }	
      }

    workCompletions.resize(numCompletionsFound);
  }
  
  // Easy way to get class member
  int get_numCompletionsFound()
  {
    return numCompletionsFound;
  }

  uint64_t get_numWorkRequestCompletions()
  {
    return numWorkRequestCompletions;
  }

  uint32_t get_numMissingFound()
  {
    return numMissingFound;
  }
  
  // A easy way to get a vector of struct from python
  std::vector<struct ibv_wc> get_workCompletions()
  {
    return workCompletions;
  }
  
  virtual ~RdmaTransport()
  {
    /* if in receive mode then display contents to stdio */
    if (mode == RECV_MODE)
      {
        //displayMemoryBlocks(manager, 10, 10); /* display contents that were received */
      }
    
    deregisterMemoryRegions(manager);

    cleanupRDMAResources(rdmaDeviceContext, eventChannel, protectionDomain,
			 receiveCompletionQueue, sendCompletionQueue, queuePair);

    if (sgItems != nullptr) { 
      for(auto iregion = 0; iregion < manager->numMemoryRegions; iregion++) {
          printf("SGitems 0x%x iregion %d=0x%x", sgItems, iregion, sgItems[iregion]);
          if (sgItems[iregion] != nullptr) {
            delete [] sgItems[iregion]; // breaks? Not sure why. DOn't care.
          }      
        }
        delete [] sgItems;
      }

      destroyMemoryRegionManager(manager);

      /* free memory block buffers */
      freeMemoryBlocks(memoryBlocks, numMemoryBlocks);

      fprintf(stdout, "INFO:\tReceive Visibilities ending %d\n", mode);
    }  
  
  int addition(int a, int b){
    return a+b;
  }
  
  void say_hello() {
    printf("Hello! \n");
  }

  /***
   * Returns a python memoryview of a memory buffer allocated by memorymanager
   * given by the blockid
   * 
   * @param blockid blockid of the block. Must be less than numMemoryBlocks
   * @returns py::memoryview of the given block with memoryblcoksize
   * @throws runtime_error if blockid >= numMemoryBlocks
   */
  py::memoryview get_memoryview(uint32_t blockid) {
    uint64_t memoryBlockSize = messageSize * numContiguousMessages;
    if (blockid >= numMemoryBlocks) {
      throw py::index_error("blockid exceeds numMemoryBlocks");
    }
    return py::memoryview::from_memory(
                                       memoryBlocks[blockid], // pointer
                                       memoryBlockSize  // size
                                       );
  }
  
  py::memoryview getGidAddress() {    
    return py::memoryview::from_memory(
                                       gidAddress.raw, // pointer
                                       sizeof(gidAddress.raw)  // size
                                       );
  }
};

PYBIND11_MODULE(rdma_transport, m) {
  m.doc() = R"pbdoc(
    RDMA transport pluggin
        -----------------------

        .. currentmodulex:: CRACO

        .. autosummary::
           :toctree: _generate


    )pbdoc";

  py::enum_<runMode>(m, "runMode")
    .value("RECV_MODE", runMode{RECV_MODE})
    .value("SEND_MODE", runMode{SEND_MODE})
    .export_values();
    
  // expose ibv_wc struct to python
  py::class_<ibv_wc>(m, "ibv_wc")
    .def_readonly("wr_id",          &ibv_wc::wr_id)
    .def_readonly("status",         &ibv_wc::status)
    .def_readonly("opcode",         &ibv_wc::opcode)
    .def_readonly("vendor_err",     &ibv_wc::vendor_err)
    .def_readonly("byte_len",       &ibv_wc::byte_len)
    .def_readonly("imm_data",       &ibv_wc::imm_data)
    .def_readonly("qp_num",         &ibv_wc::qp_num)
    .def_readonly("src_qp",         &ibv_wc::src_qp)
    .def_readonly("wc_flags",       &ibv_wc::wc_flags)
    .def_readonly("pkey_index",     &ibv_wc::pkey_index)
    .def_readonly("slid",           &ibv_wc::slid)
    .def_readonly("sl",             &ibv_wc::sl)
    .def_readonly("dlid_path_bits", &ibv_wc::dlid_path_bits);

  py::enum_<ibv_wc_status>(m, "ibv_wc_status")  
    .value("IBV_WC_SUCCESS"          , ibv_wc_status{IBV_WC_SUCCESS})
    .value("IBV_WC_LOC_LEN_ERR"      , ibv_wc_status{IBV_WC_LOC_LEN_ERR})
    .value("IBV_WC_LOC_QP_OP_ERR"    , ibv_wc_status{IBV_WC_LOC_QP_OP_ERR})
    .value("IBV_WC_LOC_EEC_OP_ERR"   , ibv_wc_status{IBV_WC_LOC_EEC_OP_ERR})
    .value("IBV_WC_LOC_PROT_ERR"     , ibv_wc_status{IBV_WC_LOC_PROT_ERR})
    .value("IBV_WC_WR_FLUSH_ERR"     , ibv_wc_status{IBV_WC_WR_FLUSH_ERR})
    .value("IBV_WC_MW_BIND_ERR"	     , ibv_wc_status{IBV_WC_MW_BIND_ERR})
    .value("IBV_WC_BAD_RESP_ERR"     , ibv_wc_status{IBV_WC_BAD_RESP_ERR})
    .value("IBV_WC_LOC_ACCESS_ERR"   , ibv_wc_status{IBV_WC_LOC_ACCESS_ERR})
    .value("IBV_WC_REM_INV_REQ_ERR"  , ibv_wc_status{IBV_WC_REM_INV_REQ_ERR})
    .value("IBV_WC_REM_ACCESS_ERR"   , ibv_wc_status{IBV_WC_REM_ACCESS_ERR})
    .value("IBV_WC_REM_OP_ERR"	     , ibv_wc_status{IBV_WC_REM_OP_ERR})
    .value("IBV_WC_RETRY_EXC_ERR"    , ibv_wc_status{IBV_WC_RETRY_EXC_ERR})
    .value("IBV_WC_RNR_RETRY_EXC_ERR", ibv_wc_status{IBV_WC_RNR_RETRY_EXC_ERR})
    .value("IBV_WC_LOC_RDD_VIOL_ERR" , ibv_wc_status{IBV_WC_LOC_RDD_VIOL_ERR})
    .value("IBV_WC_REM_INV_RD_REQ_ERR", ibv_wc_status{IBV_WC_REM_INV_RD_REQ_ERR})
    .value("IBV_WC_REM_ABORT_ERR"     , ibv_wc_status{IBV_WC_REM_ABORT_ERR})
    .value("IBV_WC_INV_EECN_ERR"      , ibv_wc_status{IBV_WC_INV_EECN_ERR})
    .value("IBV_WC_INV_EEC_STATE_ERR" , ibv_wc_status{IBV_WC_INV_EEC_STATE_ERR})
    .value("IBV_WC_FATAL_ERR"	      , ibv_wc_status{IBV_WC_FATAL_ERR})
    .value("IBV_WC_RESP_TIMEOUT_ERR"  , ibv_wc_status{IBV_WC_RESP_TIMEOUT_ERR})
    .value("IBV_WC_GENERAL_ERR"	      , ibv_wc_status{IBV_WC_GENERAL_ERR})
    .value("IBV_WC_TM_ERR"	      , ibv_wc_status{IBV_WC_TM_ERR})
    .value("IBV_WC_TM_RNDV_INCOMPLETE", ibv_wc_status{IBV_WC_TM_RNDV_INCOMPLETE})
    .export_values();
  
  py::class_<RdmaTransport>(m, "RdmaTransport")
    .def(py::init<enum runMode,
	 uint32_t, 
	 uint32_t, 
	 uint32_t, 
	 uint64_t, 
	 uint32_t, 
	 char*,
	 uint8_t, 
	 int>())

    .def("getPacketSequenceNumber", &RdmaTransport::getPacketSequenceNumber)
    .def("getQueuePairNumber",      &RdmaTransport::getQueuePairNumber)
    .def("getGidAddress",           &RdmaTransport::getGidAddress)
    .def("getLocalIdentifier",      &RdmaTransport::getLocalIdentifier)

    .def("setPacketSequenceNumber", &RdmaTransport::setPacketSequenceNumber)
    .def("setQueuePairNumber",      &RdmaTransport::setQueuePairNumber)
    .def("setGidAddress",           &RdmaTransport::setGidAddress)
    .def("setLocalIdentifier",      &RdmaTransport::setLocalIdentifier)

    .def("get_numWorkRequestCompletions", &RdmaTransport::get_numWorkRequestCompletions)
    .def("get_numCompletionsFound",       &RdmaTransport::get_numCompletionsFound)
    .def("get_numMissingFound",           &RdmaTransport::get_numMissingFound)
    .def("get_workCompletions",           &RdmaTransport::get_workCompletions)    
    .def("pollRequests",                  &RdmaTransport::pollRequests)
    .def("waitRequestsCompletion",        &RdmaTransport::waitRequestsCompletion)
    .def("issueRequests",                 &RdmaTransport::issueRequests)
    .def("setupRdma",                     &RdmaTransport::setupRdma)
    
    .def("say_hello",               &RdmaTransport::say_hello)
    .def("addition",                &RdmaTransport::addition)
    .def("get_memoryview",          &RdmaTransport::get_memoryview)
    .def_readonly("messageSize", &RdmaTransport::messageSize)
    .def_readonly("numMemoryBlocks", &RdmaTransport::numMemoryBlocks)
    .def_readonly("numContiguousMessages", &RdmaTransport::numContiguousMessages)
    .def_readonly("numTotalMessages", &RdmaTransport::numTotalMessages)
    .def_readonly("messageDelayTime", &RdmaTransport::messageDelayTime)
    .def_readonly("rdmaDeviceName", &RdmaTransport::rdmaDeviceName)
    .def_readonly("rdmaPort", &RdmaTransport::rdmaPort)
    .def_readonly("gidIndex", &RdmaTransport::gidIndex)
    .def_readwrite("checkImmediate", &RdmaTransport::checkImmediate)
    .def_readonly("queueCapacity", &RdmaTransport::queueCapacity)
    .def_readonly("maxInlineDataSize", &RdmaTransport::maxInlineDataSize)
    .def_readonly("minWorkRequestEnqueue", &RdmaTransport::minWorkRequestEnqueue)
    .def_readonly("maxWorkRequestDequeue", &RdmaTransport::maxWorkRequestDequeue)
    .def_readonly("currentQueueLoading", &RdmaTransport::currentQueueLoading)
    .def_readonly("numWorkRequestsEnqueued", &RdmaTransport::numWorkRequestsEnqueued)
    .def_readonly("previousImmediateData", &RdmaTransport::previousImmediateData)
    .def_readonly("numWorkRequestsMissing", &RdmaTransport::numWorkRequestsMissing)
    .def_readonly("regionIndex", &RdmaTransport::regionIndex)
    .def_readonly("numWorkRequestCompletions", &RdmaTransport::numWorkRequestCompletions)
    .def_readonly("numCompletionsFound", &RdmaTransport::numCompletionsFound)
    .def_readonly("numMissingFound", &RdmaTransport::numMissingFound)
    .def_readwrite("packetSequenceNumber", &RdmaTransport::packetSequenceNumber)
    .def_readonly("queuePairNumber", &RdmaTransport::queuePairNumber)
    .def_readonly("localIdentifier", &RdmaTransport::localIdentifier)
    .def_readonly("remotePSN", &RdmaTransport::remotePSN)
    .def_readonly("remoteQPN", &RdmaTransport::remoteQPN)
    .def_readonly("remoteGID", &RdmaTransport::remoteGID)
    .def_readonly("remoteLID", &RdmaTransport::remoteLID)
    .def_readwrite("timeoutMillis", &RdmaTransport::timeoutMillis);

  m.def("run_test", &run_test);

  py::enum_<logType>(m, "logType")  
    .value("LOG_EMERG", logType{LOG_EMERG})
    .value("LOG_ALERT", logType{LOG_ALERT})
    .value("LOG_CRIT", logType{LOG_CRIT})
    .value("LOG_ERR", logType{LOG_ERR})
    .value("LOG_WARNING", logType{LOG_WARNING})
    .value("LOG_NOTICE", logType{LOG_NOTICE})
    .value("LOG_INFO", logType{LOG_INFO})
    .value("LOG_DEBUG", logType{LOG_DEBUG})
    .export_values();
  m.def("setLogLevel", &setLogLevel);

  py::register_exception<TimeoutException>(m, "TimeoutException");



  // To create a buffer
  // https://pybind11.readthedocs.io/en/stable/advanced/pycpp/numpy.html
  // expections
  // https://blog.ekbana.com/write-a-python-binding-for-your-c-code-using-pybind11-library-ef0992d4b68
    
#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
