#include "rdma_transport.hpp"
extern "C" {
#include "RDMAapi.h"
}

#include <stdio.h>

struct RdmaTransport {
  std::string rdmaDeviceName;
  uint8_t rdmaPort;
  uint32_t queueCapacity;
  uint32_t maxInlineDataSize;
  struct ibv_context *rdmaDeviceContextPtr;
  struct ibv_comp_channel *eventChannelPtr;
  struct ibv_pd *protectionDomainPtr;
  struct ibv_cq *receiveCompletionQueuePtr;
  struct ibv_cq *sendCompletionQueuePtr;
  struct ibv_qp *queuePairPtr;


  RdmaTransport(const std::string &_rdmaDeviceName,
                uint8_t _rdmaPort,
                uint32_t _queueCapacity,
                uint32_t _maxInlineDataSize) :
    rdmaDeviceName(_rdmaDeviceName),
    rdmaPort(_rdmaPort),
    queueCapacity(_queueCapacity),
    maxInlineDataSize(_maxInlineDataSize)
  {
    // SEE: RAII

    if(allocateRDMAResources((char*)rdmaDeviceName.c_str(),
                          rdmaPort,
                          queueCapacity,
                          maxInlineDataSize,
                          &rdmaDeviceContextPtr,
                          &eventChannelPtr,
                          &protectionDomainPtr,
                          &receiveCompletionQueuePtr,
                          &sendCompletionQueuePtr,
                             &queuePairPtr) != SUCCESS) {
      throw std::bad_alloc();
    }

    
  }

  virtual ~RdmaTransport() {
    cleanupRDMAResources(rdmaDeviceContextPtr,
                         eventChannelPtr,
                         protectionDomainPtr,
                         receiveCompletionQueuePtr,
                         sendCompletionQueuePtr,
                         queuePairPtr);
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
                      uint32_t >())
      .def("say_hello", &RdmaTransport::say_hello);
    

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
