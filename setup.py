import sys

from pybind11 import get_cmake_dir
# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup
from glob import glob

__version__ = "0.0.1"

# The main interface is through Pybind11Extension.
# * You can add cxx_std=11/14/17, and then build_ext can be removed.
# * You can set include_pybind11=false to add the include directory yourself,
#   say from a submodule.
#
# Note:
#   Sort input source files if you glob sources to ensure bit-for-bit
#   reproducible builds (https://github.com/pybind/python_example/pull/53)

sources = sorted(glob("src/*.cpp"))
#sources.extend(['rdma-data-transport/RDMAapi.c','rdma-data-transport/RDMAmemorymanager.c', 'rdma-data-transport/RDMAexchangeidcallbacks.c'])
sources.extend(glob('rdma-data-transport/*.c'))

#print(sources)

ext_modules = [
    Pybind11Extension("rdma_transport",
                      sources,
                      # Example: passing in the version to the compiled code
                      define_macros = [('VERSION_INFO', __version__)],
                      include_dirs=['rdma-data-transport'],
                      extra_compile_args= ['-ggdb','-O0', '-UNDEBUG'],
                      extra_link_args= ['-ggdb', '-UNDEBUG', '-lm','-lrdmacm', '-libverbs','-lcurl']
    ),
]

setup(
    name="rdma_transport",
    version=__version__,
    author="Keith Bannister",
    author_email="keith.bannister@gmail.com",
    url="https://github.com/askap-craco/python-rdma-transport",
    description="RDMA transport",
    long_description="",
    ext_modules=ext_modules,
    extras_require={"test": "pytest"},
    # Currently, build_ext only provides an optional "highest supported C++
    # level" feature, but in the future it may provide more features.
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.6",
)
