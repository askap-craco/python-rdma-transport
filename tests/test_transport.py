from rdma_transport import RdmaTransport

def test_hello():
    # From the C sources aboutmaxIlinedadtaSize
    # must be zero NOTE put back at 236 once testing completed
    maxInlineDataSize = 0; 
    
    t = RdmaTransport('mlx5_1', 1, 4, maxInlineDataSize)
    t.say_hello()



    
