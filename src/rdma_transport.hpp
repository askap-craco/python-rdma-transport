#pragma once

#include <pybind11/pybind11.h>

#include <stdio.h>
#include <stdexcept>

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

namespace py = pybind11;

class RdmaException: public std::exception{
public:
  virtual const char* what() const throw();
};
