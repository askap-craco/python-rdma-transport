# python-rdma-transport
Pybind11 bindings for RDMA data transport

# To install from source you need to

```

git clone git@github.com:askap-craco/python-rdma-transport.git
cd python-rdma-transport
git submodule init
git submodule update
pip install pybind11[global]
python setup.py build_ext
python setyp.py bdist_wheel

```