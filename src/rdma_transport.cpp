#include "rdma_transport.hpp"
extern "C" {
#include "RDMAapi.h"
#include "RDMAexchangeidcallbacks.h"
}

#include <stdio.h>

struct RdmaTransport {
  int gidIndex;
  std::string rdmaDeviceName;
  uint8_t rdmaPort;
  uint32_t queueCapacity;
  uint32_t maxInlineDataSize;
  enum runMode mode;
  
  struct ibv_context *rdmaDeviceContextPtr;
  struct ibv_comp_channel *eventChannelPtr;
  struct ibv_pd *protectionDomainPtr;
  struct ibv_cq *receiveCompletionQueuePtr;
  struct ibv_cq *sendCompletionQueuePtr;
  struct ibv_qp *queuePairPtr;

  uint16_t localIdentifier;
  union ibv_gid gidAddress;
  enum ibv_mtu mtu;

  RdmaTransport(const std::string &_rdmaDeviceName,
                uint8_t _rdmaPort,
                uint32_t _queueCapacity,
                uint32_t _maxInlineDataSize,
		int _gidIndex,
		enum runMode _mode) :
    rdmaDeviceName(_rdmaDeviceName),
    rdmaPort(_rdmaPort),
    queueCapacity(_queueCapacity),
    maxInlineDataSize(_maxInlineDataSize),
    gidIndex(_gidIndex),
    mode(_mode)
  {
    // SEE: RAII
    allocateRDMAResources((char*)rdmaDeviceName.c_str(),
                          rdmaPort,
                          queueCapacity,
                          maxInlineDataSize,
                          &rdmaDeviceContextPtr,
                          &eventChannelPtr,
                          &protectionDomainPtr,
                          &receiveCompletionQueuePtr,
                          &sendCompletionQueuePtr,
                          &queuePairPtr);
   
    setupRDMAConnection(rdmaDeviceContextPtr,
			rdmaPort,
			&localIdentifier,
			&gidAddress,
			&gidIndex,
			&mtu); 
  }
  
  virtual ~RdmaTransport()
  {
    cleanupRDMAResources(rdmaDeviceContextPtr,
                         eventChannelPtr,
                         protectionDomainPtr,
                         receiveCompletionQueuePtr,
                         sendCompletionQueuePtr,
                         queuePairPtr);
  }
  
  uint32_t remotePSN;
  uint32_t remoteQPN;
  union ibv_gid remoteGID;
  uint16_t remoteLID;
  uint32_t packetSequenceNumber;
  
  enum exchangeResult exchangeViaStdIOPybind11()
  {
    srand(getpid() * time(NULL));
    packetSequenceNumber = (uint32_t) (rand() & 0x00FFFFFF);
    
    return exchangeViaStdIO(mode==SEND_MODE,
			    packetSequenceNumber,
			    queuePairPtr->qp_num,
			    gidAddress,
			    localIdentifier,
			    &remotePSN,
			    &remoteQPN,
			    &remoteGID,
			    &remoteLID);
  }
  
  int modifyQueuePairReadyPybind11()
  {  
    return modifyQueuePairReady(queuePairPtr,
				rdmaPort,
				gidIndex,
				mode,
				packetSequenceNumber,
				remotePSN,
				remoteQPN,
				remoteGID,
				remoteLID,
				mtu);
  }
  
  //int registerMemoryRegions(struct ibv_pd *protectionDomain,
  //			    MemoryRegionManager* manager);
  //
  
  int ibv_req_notify_cq_pybind11(struct ibv_cq *cq,
				 int solicited_only){

    return ibv_req_notify_cq(cq, solicited_only);
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
    
    py::class_<RdmaTransport>(m, "RdmaTransport")
      .def(py::init<const std::string &,
	   uint8_t ,
	   uint32_t ,
	   uint32_t,
	   int,
	   enum runMode>())
      
      /**********************************************************************
       * Exchanges the necessary RDMA configuration identifiers with those of
       * the remote RDMA peer, by printing the local values to the display
       * and prompting the user to enter the remote values
       * Returns EXCH_SUCCESS if identifier exchange has completed successfully
       **********************************************************************/
      .def("exchangeViaStdIOPybind11", &RdmaTransport::exchangeViaStdIOPybind11)
      
      /**********************************************************************
       * Modifies the queue pair so that it is ready to receive
       * and possibly to also send
       **********************************************************************/
      .def("modifyQueuePairReadyPybind11", &RdmaTransport::modifyQueuePairReadyPybind11)
      
      ///**********************************************************************
      // * Registers memory regions with protection domain
      // **********************************************************************/
      //.def("registerMemoryRegions", &RdmaTransport::registerMemoryRegions)

      .def("ibv_req_notify_cq_pybind11", &RdmaTransport::ibv_req_notify_cq_pybind11)
       
      .def("say_hello", &RdmaTransport::say_hello)
      .def("addition", &RdmaTransport::addition);
    

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
