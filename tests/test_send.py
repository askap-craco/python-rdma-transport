from rdma_transport import RdmaTransport
from rdma_transport import runMode
from rdma_transport import ibv_wc
import numpy as np
import time

def test_send_messages():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    mode = runMode.SEND_MODE
    messageSize = 65536
    numMemoryBlocks = 10
    numContiguousMessages = 100
    numTotalMessages = 100*numMemoryBlocks*numContiguousMessages-1
    messageDelayTime = 0
    rdmaDeviceName = None #"mlx5_1"
    rdmaPort = 1
    gidIndex = -1
    identifierFileName = "exchange"
  
    rdma_transport = RdmaTransport(mode, 
                                   messageSize,
                                   numMemoryBlocks,
                                   numContiguousMessages,
                                   numTotalMessages,
                                   messageDelayTime,
                                   rdmaDeviceName,
                                   rdmaPort,
                                   gidIndex)
                                       
    packetSequenceNumber = rdma_transport.getPacketSequenceNumber()
    print(f"Local packet sequence number is {packetSequenceNumber}")

    queuePairNumber = rdma_transport.getQueuePairNumber()
    print(f"Local queue pair number is {queuePairNumber}")
    
    gidAddress = rdma_transport.getGidAddress()
    gidAddress = np.frombuffer(gidAddress, dtype=np.uint8)
    print(f"Local gid address is {gidAddress}")
    
    localIdentifier = rdma_transport.getLocalIdentifier()
    print(f"Local identifier is {localIdentifier}")

    # This should be after get local numbers
    # and setup remote numbers
    rdma_transport.setupRdma(identifierFileName)

    numCompletionsTotal = 0
    start = time.time()
    while numCompletionsTotal < numTotalMessages:
        loopStart = time.time()
        
        rdma_transport.issueRequests()
        print("issue requests done")
        
        rdma_transport.waitRequestsCompletion()
        print("wait requests completion done")
        
        rdma_transport.pollRequests()
        print("poll requests done")
        
        numCompletionsFound = rdma_transport.get_numCompletionsFound()
        print("got numCompletionsFound")

        # Record how many completions got so far
        numCompletionsTotal += numCompletionsFound

        workCompletionsStart = time.time()
        workCompletions = rdma_transport.get_workCompletions()
        workCompletionsEnd = time.time()
        
        print(f"got_workCompletions takes {workCompletionsEnd - workCompletionsStart} seconds")
        print(f"the rest takes {workCompletionsStart - loopStart} seconds")
        
        #ndata_print = 10
        #rdma_memory = rdma_transport.get_memoryview(0)
        #rdma_buffer = np.frombuffer(rdma_memory, dtype=np.int16)
        
        #print(f"Number of numCompletionsFound {numCompletionsFound}, and workCompletions {workCompletions} with first {ndata_print} data\n {rdma_buffer[0:ndata_print]}")

        #time.sleep(0.1)

    end = time.time()
    interval = end - start
    rate = 8E-9*numTotalMessages*messageSize/interval
    
    print(f'sender data rate is {rate} Gbps')
    
if __name__ == '__main__':
    test_send_messages()
