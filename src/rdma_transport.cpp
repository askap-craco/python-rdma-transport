#include "rdma_transport.hpp"
extern "C" {
#include "RDMAapi.h"
#include "RDMAexchangeidcallbacks.h"
}

#include <stdio.h>

struct RdmaTransport {
  
  /* set up default values that might be overriden when creating a instance of the class */
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
  }
  
  virtual ~RdmaTransport()
  {
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

      
      .def("say_hello", &RdmaTransport::say_hello)
      .def("addition", &RdmaTransport::addition);
    

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
