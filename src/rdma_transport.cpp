#include "rdma_transport.hpp"
extern "C" {
#include "RDMAapi.h"
//#include "RDMAmemorymanager.h"
//#include "RDMAexchangeidcallbacks.h"
}

/**********************************************************************
 * struct that holds parameters for sendWorkRequests
 **********************************************************************/
struct sendWorkRequestParams
{
    MemoryRegionManager* manager;
    struct ibv_qp *queuePair;
    struct ibv_comp_channel *eventChannel;
    uint32_t messageDelayTime;
    uint32_t maxInlineDataSize;
    struct ibv_cq *sendCompletionQueue;
    uint32_t queueCapacity;
    char *metricURL;
    uint32_t numMetricAveraging;
    struct ibv_context *rdmaDeviceContext; // required for cleanup once transfers completed
    struct ibv_pd *protectionDomain; // required for cleanup once transfers completed
    struct ibv_cq *receiveCompletionQueue; // required for cleanup once transfers completed
};


/**********************************************************************
 * struct that holds parameters for receiveWorkRequests
 **********************************************************************/
struct receiveWorkRequestParams
{
    MemoryRegionManager* manager;
    struct ibv_qp *queuePair;
    struct ibv_comp_channel *eventChannel;
    uint32_t messageDelayTime;
    struct ibv_cq *receiveCompletionQueue;
    uint32_t queueCapacity;
    char *metricURL;
    uint32_t numMetricAveraging;
    struct ibv_context *rdmaDeviceContext; // required for cleanup once transfers completed
    struct ibv_pd *protectionDomain; // required for cleanup once transfers completed
    struct ibv_cq *sendCompletionQueue; // required for cleanup once transfers completed
};

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
  std::string dataFileName = NULL; /* default to not loading (sender) nor saving (receiver) to file the data memory blocks */
  uint64_t numTotalMessages = 0; /* total number of messages to send or receive, if 0 then default to numMemoryBlocks*numContiguousMessages */
  uint32_t messageDelayTime = 0; /* time in milliseconds to delay after each message send/receive posted, default is no delay */
  std::string rdmaDeviceName = NULL; /* no preferred rdma device name to choose */
  uint8_t rdmaPort = 1;
  int gidIndex = -1; /* preferred gid index or -1 for no preference */
  std::string identifierFileName = NULL; /* default to using stdio for exchanging RDMA identifiers */
  std::string metricURL = NULL; /* default to not push metrics */
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
    memoryBlocks = allocateMemoryBlocks(memoryBlockSize, &numMemoryBlocks);

    /* create a memory region manager to manage the usage of the memory blocks */
    /* note for simplicity memory region manager doesn't monitor memory regions so this code doesn't have to */
    /* load memory blocks with new data during sending/receiving */
    manager = createMemoryRegionManager(memoryBlocks,
					messageSize, numMemoryBlocks, numContiguousMessages, numTotalMessages, false);

    /* if in send mode then populate the memory blocks and display contents to stdio */
    if (mode == SEND_MODE)
      {
        if ((char*)dataFileName.c_str() != NULL)
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
    if ((char*)identifierFileName.c_str() != NULL)
      {
        setIdentifierFileName((char*)identifierFileName.c_str());
        identifierExchangeFunction = exchangeViaSharedFiles;
      }
    if (numMetricAveraging == 0)
      numMetricAveraging = manager->numMemoryRegions * manager->numContiguousMessages;

    logger(LOG_INFO, "Receive Visibilities starting");
    if (mode == SEND_MODE)
      logger(LOG_INFO, "Running as sender");
    else
      logger(LOG_INFO, "Running as receiver");

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
    srand(getpid() * time(NULL));
    uint32_t packetSequenceNumber = (uint32_t) (rand() & 0x00FFFFFF);

    /* exchange the necessary information with the remote RDMA peer */
    uint32_t queuePairNumber = queuePair->qp_num;
    uint32_t remotePSN;
    uint32_t remoteQPN;
    union ibv_gid remoteGID;
    uint16_t remoteLID;
    if (identifierExchangeFunction == NULL)
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
  }

  void sendReceive(){

    // Why sendWorkRequests and recvWorkRequests destory all information?
    if (mode == SEND_MODE)
      {
	struct sendWorkRequestParams *params = NULL;
	params = (struct sendWorkRequestParams*)malloc(sizeof(struct sendWorkRequestParams)); // freed in sendWorkRequests
	params->manager = manager;
	params->queuePair = queuePair;
	params->eventChannel = eventChannel;
	params->messageDelayTime = messageDelayTime;
	params->maxInlineDataSize = maxInlineDataSize;
	params->sendCompletionQueue = sendCompletionQueue;
	params->queueCapacity = queueCapacity;
	params->metricURL = (char*)metricURL.c_str();
	params->numMetricAveraging = numMetricAveraging;
	params->rdmaDeviceContext = rdmaDeviceContext;
	params->protectionDomain = protectionDomain;
	params->receiveCompletionQueue = receiveCompletionQueue;
	sendWorkRequests(params);
      }
    else /* mode == RECV_MODE */
      {
        struct receiveWorkRequestParams *params = NULL;
        params = (struct receiveWorkRequestParams*)malloc(sizeof(struct receiveWorkRequestParams)); // freed in receiveWorkRequests
        params->manager = manager;
        params->queuePair = queuePair;
        params->eventChannel = eventChannel;
        params->messageDelayTime = messageDelayTime;
        params->receiveCompletionQueue = receiveCompletionQueue;
        params->queueCapacity = queueCapacity;
        params->metricURL = (char*)metricURL.c_str();
        params->numMetricAveraging = numMetricAveraging;
        params->rdmaDeviceContext = rdmaDeviceContext;
        params->protectionDomain = protectionDomain;
        params->sendCompletionQueue = sendCompletionQueue;
	receiveWorkRequests(params);
      }

    logger(LOG_INFO, "Receive Visibilities ending");
  }
  
  virtual ~RdmaTransport()
  {
    /* if in receive mode then display contents to stdio */
    if (mode == RECV_MODE)
      {
        if ((char *)dataFileName.c_str() != NULL)
    	  {
            // write data to dataFileName.0, dataFileName.1, ... from memory blocks
            if (!writeFilesFromMemoryBlocks(manager, (char*)dataFileName.c_str()))
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

    .def("sendReceive", &RdmaTransport::sendReceive)
    .def("say_hello", &RdmaTransport::say_hello)
    .def("addition", &RdmaTransport::addition);
    

#ifdef VERSION_INFO
  m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
  m.attr("__version__") = "dev";
#endif
}
