#pragma once

#if defined(_WIN32)
  #define GPUSIM_CUDART_EXPORT extern "C" __declspec(dllexport)
#else
  #define GPUSIM_CUDART_EXPORT extern "C" __attribute__((visibility("default")))
#endif
