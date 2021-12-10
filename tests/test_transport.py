from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import logType

def test_hello():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    requestLogLevel = logType.LOG_NOTICE
    mode = runMode.RECV_MODE
    messageSize = 65536
    numMemoryBlocks = 1
    numContiguousMessages = 1
    dataFileName = " "
    numTotalMessages = 0
    messageDelayTime = 0
    rdmaDeviceName = "mlx5_1"
    rdmaPort = 1
    gidIndex = -1
    identifierFileName = " "
    metricURL = " "
    numMetricAveraging = 0

  
    t = RdmaTransport(requestLogLevel, 
                      mode, 
                      messageSize,
                      numMemoryBlocks,
                      numContiguousMessages,
                      dataFileName,
                      numTotalMessages,
                      messageDelayTime,
                      rdmaDeviceName,
                      rdmaPort,
                      gidIndex,
                      identifierFileName,
                      metricURL,
                      numMetricAveraging)

    t.say_hello()

    a = 2
    b = 3

    c = t.addition(a, b)
    
    assert c == (a+b)
    print(f"We get c = {c} with addition of {a} and {b}")
    
    t.sendRecvive()
