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
  
  uint16_t localIdentifier;
  union ibv_gid gidAddress;
  enum ibv_mtu mtu;
  
  struct ibv_context *rdmaDeviceContextPtr;
  struct ibv_comp_channel *eventChannelPtr;
  struct ibv_pd *protectionDomainPtr;
  struct ibv_cq *receiveCompletionQueuePtr;
  struct ibv_cq *sendCompletionQueuePtr;
  struct ibv_qp *queuePairPtr;

  RdmaTransport(const std::string &_rdmaDeviceName,
                uint8_t _rdmaPort,
                uint32_t _queueCapacity,
                uint32_t _maxInlineDataSize,
		int _gidIndex) :
    rdmaDeviceName(_rdmaDeviceName),
    rdmaPort(_rdmaPort),
    queueCapacity(_queueCapacity),
    maxInlineDataSize(_maxInlineDataSize),
    gidIndex(_gidIndex)
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
    
  }

  virtual ~RdmaTransport() {
    cleanupRDMAResources(rdmaDeviceContextPtr,
                         eventChannelPtr,
                         protectionDomainPtr,
                         receiveCompletionQueuePtr,
                         sendCompletionQueuePtr,
                         queuePairPtr);
  }

  //int setupRDMAConnection(rdmaDeviceContextPtr,
  //			  rdmaPort,
  //			  &localIdentifierPtr,
  //			  &gidAddressPtr,
  //			  &gidIndexPtr,
  //			  &mtuPtr);

  int setupRDMAConnectionPybind11(struct ibv_context *rdmaDeviceContext,
				  uint8_t rdmaPort,
				  uint16_t *localIdentifierPtr,
				  union ibv_gid *gidAddressPtr,
				  int *gidIndexPtr,
				  enum ibv_mtu *mtuPtr){

    setupRDMAConnection(rdmaDeviceContextPtr,
			rdmaPort,
			localIdentifierPtr,
			gidAddressPtr,
			gidIndexPtr,
			mtuPtr);
  }
  
  //enum exchangeResult exchangeViaStdIO(bool isSendMode,
  //				       uint32_t packetSequenceNumber,
  //				       uint32_t queuePairNumber,
  //				       union ibv_gid gidAddress,
  //				       uint16_t localIdentifier,
  //				       uint32_t *remotePSNPtr,
  //				       uint32_t *remoteQPNPtr,
  //				       union ibv_gid *remoteGIDPtr,
  //				       uint16_t *remoteLIDPtr);
  //
  //int modifyQueuePairReady(struct ibv_qp *queuePair,
  //			   uint8_t rdmaPort,
  //			   int gidIndex,
  //			   enum runMode mode,
  //			   int packetSequenceNumber,
  //			   uint32_t remotePSN,
  //			   uint32_t remoteQPN,
  //			   union ibv_gid remoteGID,
  //			   uint16_t remoteLID,
  //			   enum ibv_mtu mtu);
  //
  //int registerMemoryRegions(struct ibv_pd *protectionDomain,
  //			    MemoryRegionManager* manager);
  //
  //int ibv_req_notify_cq(struct ibv_cq *cq,
  //			int solicited_only);
  //
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

    py::class_<RdmaTransport>(m, "RdmaTransport")
      .def(py::init<const std::string &,
	   uint8_t ,
	   uint32_t ,
	   uint32_t,
	   int >())
      
      /**********************************************************************
       * Sets up the RDMA connection and return its details in the
       * pointer parameters
       **********************************************************************/
      .def("setupRDMAConnectionPybind11", &RdmaTransport::setupRDMAConnectionPybind11)
      
      ///**********************************************************************
      // * Exchanges the necessary RDMA configuration identifiers with those of
      // * the remote RDMA peer, by printing the local values to the display
      // * and prompting the user to enter the remote values
      // * Returns EXCH_SUCCESS if identifier exchange has completed successfully
      // **********************************************************************/
      //.def("exchangeViaStdIO", &RdmaTransport::exchangeViaStdIO)
      //
      ///**********************************************************************
      // * Modifies the queue pair so that it is ready to receive
      // * and possibly to also send
      // **********************************************************************/
      //.def("modifyQueuePairReady", &RdmaTransport::modifyQueuePairReady)
      //
      ///**********************************************************************
      // * Registers memory regions with protection domain
      // **********************************************************************/
      //.def("registerMemoryRegions", &RdmaTransport::registerMemoryRegions)

      //.def("ibv_req_notify_cq", &RdmaTransport::ibv_req_notify_cq)
       
      .def("say_hello", &RdmaTransport::say_hello)
      .def("addition", &RdmaTransport::addition);
    

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
