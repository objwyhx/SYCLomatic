// UNSUPPORTED: cuda-8.0, cuda-9.0, cuda-9.1, cuda-9.2, cuda-10.0, cuda-10.1, cuda-10.2
// UNSUPPORTED: v8.0, v9.0, v9.1, v9.2, v10.0, v10.1, v10.2
// RUN: dpct --format-range=none -in-root %S -out-root %T/iterator/constant_iterator %S/constant_iterator.cu --cuda-include-path="%cuda-path/include" -- -std=c++14 -x cuda --cuda-host-only
// RUN: FileCheck --input-file %T/iterator/constant_iterator/constant_iterator.dp.cpp %s

#include <cub/cub.cuh>
#include <iostream>

#define N 10

void init(int *&d_in, int *&d_out) {
  static constexpr int h_in[N] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
  cudaMalloc((void **)&d_in, sizeof(int) * N);
  cudaMalloc((void **)&d_out, sizeof(int) * N);
  cudaMemcpy((void *)d_in, (void *)h_in, sizeof(h_in), cudaMemcpyHostToDevice);
  std::cout << "Input:  ";
  std::copy(h_in, h_in + N, std::ostream_iterator<int>(std::cout, "\t"));
  std::cout << "\n";
}

void print(int *d_out) {
  int h_out[N];
  cudaMemcpy((void *)h_out, (void *)d_out, sizeof(int) * N, cudaMemcpyDeviceToHost);
  std::cout << "Output: ";
  std::copy(h_out, h_out + N, std::ostream_iterator<int>(std::cout, "\t"));
  std::cout << "\n";
}

using ArgIndexInputIterator = cub::ArgIndexInputIterator<int *>;
using Pair = ArgIndexInputIterator::value_type;

struct TransformOp {
  __device__ int operator()(const Pair &x) const {
    if (x.key & 1)
      return 0;
    return x.value;
  }
};

struct SumOp {
  __device__ int operator()(int x, int y) const {
    return x + y;
  }
};

void work() {
  int *d_in = nullptr;
  int *d_out = nullptr;
  int *d_temp = nullptr;
  size_t d_temp_size;
  init(d_in, d_out);
  SumOp scan_op;
  TransformOp input_iter_transform;
  auto input = cub::TransformInputIterator<int, TransformOp, ArgIndexInputIterator>(
    ArgIndexInputIterator(d_in), 
    input_iter_transform
  );
  cub::DeviceScan::InclusiveScan(d_temp, d_temp_size, input, d_out, scan_op, N);
  cudaMalloc((void **)&d_temp, d_temp_size);
  cub::DeviceScan::InclusiveScan(d_temp, d_temp_size, input, d_out, scan_op, N);
  print(d_out);
}

int main() {
  work();
  return 0;
}

