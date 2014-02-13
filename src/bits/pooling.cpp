//
//  pooling.cpp
//  matconv
//
//  Created by Andrea Vedaldi on 11/02/2014.
//  Copyright (c) 2014 Andrea Vedaldi. All rights reserved.
//

#include <sm_20_atomic_functions.h>
#include "caffe-scraps.hpp"
#include "pooling.hpp" 

/* ---------------------------------------------------------------- */
/*                                                 maxPooling (CPU) */
/* ---------------------------------------------------------------- */

template<typename T>
void maxPooling_cpu(T* pooled,
                    T const* data,
                    size_t width,
                    size_t height,
                    size_t depth,
                    size_t poolSize,
                    size_t poolStride)
{
  int pooledWidth = (width - poolSize)/poolStride + 1 ;
  int pooledHeight = (height - poolSize)/poolStride + 1 ;

  for (int c = 0; c < depth; ++c) {
    for (int ph = 0; ph < pooledHeight; ++ph) {
      for (int pw = 0; pw < pooledWidth; ++pw) {
        int hstart = ph * poolStride;
        int wstart = pw * poolStride;
        int hend = std::min(hstart + poolSize, height);
        int wend = std::min(wstart + poolSize, width);
        for (int h = hstart; h < hend; ++h) {
          for (int w = wstart; w < wend; ++w) {
            pooled[ph * pooledWidth + pw] = max(pooled[ph * pooledWidth + pw],
                                                data[h * width + w]);
          }
        }
      }
    }
    data += width*height ;
    pooled += pooledWidth*pooledHeight ;
  }
}

template
void maxPooling_cpu<float>(float* pooled,
                           float const* data,
                           size_t width,
                           size_t height,
                           size_t depth,
                           size_t poolSize,
                           size_t poolStride) ;

template
void maxPooling_cpu<double>(double* pooled,
                            double const* data,
                            size_t width,
                            size_t height,
                            size_t depth,
                            size_t poolSize,
                            size_t poolStride) ;

/* ---------------------------------------------------------------- */
/*                                                 maxPooling (GPU) */
/* ---------------------------------------------------------------- */

template <typename Dtype>
__global__ void maxPooling_gpu_kernel
(const int nthreads, const Dtype* bottom_data,
 const int num, const int channels, const int height,
 const int width, const int pooled_height, const int pooled_width,
 const int ksize, const int stride, Dtype* top_data)
{
  int index = threadIdx.x + blockIdx.x * blockDim.x;
  if (index < nthreads) {
    // decode index:
    // index = (n*channels + c)*pooled_height) + ph)*pooled_width + pw
    int pw = index % pooled_width;
    int ph = (index / pooled_width) % pooled_height;
    int c = (index / pooled_width / pooled_height) % channels;
    int n = index / pooled_width / pooled_height / channels;
    // pooled patch start and end
    int hstart = ph * stride;
    int hend = min(hstart + ksize, height);
    int wstart = pw * stride;
    int wend = min(wstart + ksize, width);
    Dtype maxval = -FLT_MAX;
    bottom_data += (n * channels + c) * height * width;
    for (int h = hstart; h < hend; ++h) {
      for (int w = wstart; w < wend; ++w) {
        maxval = max(maxval, bottom_data[h * width + w]);
      }
    }
    top_data[index] = maxval;
  }
}

template<typename T>
void maxPooling_gpu(T* pooled,
                    T const* data,
                    size_t width,
                    size_t height,
                    size_t depth,
                    size_t poolSize,
                    size_t poolStride)
{
  int pooledWidth = (width - poolSize)/poolStride + 1 ;
  int pooledHeight = (height - poolSize)/poolStride + 1 ;
  int count = pooledWidth * pooledHeight * depth ;
  maxPooling_gpu_kernel<T><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>
  (count, data, 1, depth, height, width, pooledHeight, pooledWidth, poolSize, poolStride, pooled) ;
}

template
void maxPooling_gpu<float>(float* pooled,
                           float const* data,
                           size_t width,
                           size_t height,
                           size_t depth,
                           size_t poolSize,
                           size_t poolStride) ;

template
void maxPooling_gpu<double>(double* pooled,
                            double const* data,
                            size_t width,
                            size_t height,
                            size_t depth,
                            size_t poolSize,
                            size_t poolStride) ;

/* ---------------------------------------------------------------- */
/*                                         maxPoolingBackward (CPU) */
/* ---------------------------------------------------------------- */

template<typename T>
void maxPoolingBackward_cpu(T* dzdx,
                            T const* data,
                            T const* dzdy,
                            size_t width,
                            size_t height,
                            size_t depth,
                            size_t poolSize,
                            size_t poolStride)
{
  int pooledWidth = (width - poolSize)/poolStride + 1 ;
  int pooledHeight = (height - poolSize)/poolStride + 1 ;

  for (int c = 0; c < depth; ++c) {
    for (int ph = 0; ph < pooledHeight; ++ph) {
      for (int pw = 0; pw < pooledWidth; ++pw) {
        int hstart = ph * poolStride;
        int wstart = pw * poolStride;
        int hend = std::min(hstart + poolSize, height);
        int wend = std::min(wstart + poolSize, width);
        int bestIndex = hstart * width + wstart ;
        T bestValue = data[bestIndex] ;
        for (int h = hstart; h < hend; ++h) {
          for (int w = wstart; w < wend; ++w) {
            int index = h * width + w ;
            T x = data[index] ;
            if (x > bestValue) {
              bestValue = x ;
              bestIndex = index ;
            }
          }
        }
        dzdx[bestIndex] += dzdy[ph * pooledWidth + pw] ;
      }
    }
    data += width*height ;
    dzdx += width*height ;
    dzdy += pooledWidth*pooledHeight ;
  }
}

template
void maxPoolingBackward_cpu<float>(float* dzdx,
                                   float const* data,
                                   float const* dzdy,
                                   size_t width,
                                   size_t height,
                                   size_t depth,
                                   size_t poolSize,
                                   size_t poolStride) ;

template
void maxPoolingBackward_cpu<double>(double* dzdx,
                                    double const* data,
                                    double const* dzdy,
                                    size_t width,
                                    size_t height,
                                    size_t depth,
                                    size_t poolSize,
                                    size_t poolStride) ;

/* ---------------------------------------------------------------- */
/*                                         maxPoolingBackward (GPU) */
/* ---------------------------------------------------------------- */

template <typename Dtype>
__global__ void maxPoolingBackward_gpu_kernel
(const int nthreads, const Dtype* bottom_data, const Dtype* top_diff,
 const int num, const int channels, const int height,
 const int width, const int pooled_height, const int pooled_width,
 const int ksize, const int stride, Dtype* bottom_diff)
{
  int index = threadIdx.x + blockIdx.x * blockDim.x;
  if (index < nthreads) {
    // decode index:
    // index = (n*channels + c)*pooled_height) + ph)*pooled_width + pw
    int pw = index % pooled_width;
    int ph = (index / pooled_width) % pooled_height;
    int c = (index / pooled_width / pooled_height) % channels;
    int n = index / pooled_width / pooled_height / channels;
    // pooled patch start and end
    int hstart = ph * stride;
    int hend = min(hstart + ksize, height);
    int wstart = pw * stride;
    int wend = min(wstart + ksize, width);
    Dtype bestValue = -FLT_MAX;
    int bestIndex = 0 ;
    bottom_data += (n * channels + c) * height * width;
    for (int h = hstart; h < hend; ++h) {
      for (int w = wstart; w < wend; ++w) {
        int index = h * width + w ;
        Dtype x = bottom_data[index] ;
        if (x > bestValue) {
          bestValue = x ;
          bestIndex = index ;
        }
      }
    }
    /* 
      This is bad, but required to eliminate a race condition when writing
      to bottom_diff.
      Caffe goes the other way around, but requrires remembering the layer
      output, or the maximal indexes.
     */
    atomicAdd(bottom_diff + bestIndex, top_diff[index]) ;
    //bottom_diff[bestIndex] += top_diff[index] ;
  }
}

template<typename T>
void maxPoolingBackward_gpu(T* dzdx,
                    T const* data,
                    T const* dzdy,
                    size_t width,
                    size_t height,
                    size_t depth,
                    size_t poolSize,
                    size_t poolStride)
{
  int pooledWidth = (width - poolSize)/poolStride + 1 ;
  int pooledHeight = (height - poolSize)/poolStride + 1 ;
  int count = pooledWidth * pooledHeight * depth ;
  maxPoolingBackward_gpu_kernel<T><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>
  (count, data, dzdy, 1, depth, height, width, pooledHeight, pooledWidth, poolSize, poolStride, dzdx) ;
}

template
void maxPoolingBackward_gpu<float>(float* pooled,
                                   float const* data,
                                   float const* dzdy,
                                   size_t width,
                                   size_t height,
                                   size_t depth,
                                   size_t poolSize,
                                   size_t poolStride) ;

#if 0
template
void maxPoolingBackward_gpu<double>(double* pooled,
                                    double const* data,
                                    double const* dzdy,
                                    size_t width,
                                    size_t height,
                                    size_t depth,
                                    size_t poolSize,
                                    size_t poolStride) ;
#endif