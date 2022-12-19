// UNSUPPORTED: cuda-8.0, cuda-9.0, cuda-9.1, cuda-9.2, cuda-10.0, cuda-10.1, cuda-10.2, cuda-11.0, cuda-11.2, cuda-11.4
// UNSUPPORTED: v8.0, v9.0, v9.1, v9.2, v10.0, v10.1, v10.2, v11.0, v11.2, v11.4
// RUN: cat %s > %T/replace_callee_name_only.cu
// RUN: cat %S/replace_callee_name_only.yaml > %T/replace_callee_name_only.yaml
// RUN: cd %T
// RUN: rm -rf %T/replace_callee_name_only_output
// RUN: mkdir %T/replace_callee_name_only_output
// RUN: dpct -out-root %T/replace_callee_name_only_output replace_callee_name_only.cu --cuda-include-path="%cuda-path/include" --usm-level=none --rule-file=replace_callee_name_only.yaml -- -x cuda --cuda-host-only
// RUN: FileCheck --input-file %T/replace_callee_name_only_output/replace_callee_name_only.dp.cpp --match-full-lines replace_callee_name_only.cu

#include <cub/cub.cuh>
#include <stddef.h>

int n, *d_in, *d_out;
void *tmp;
size_t tmp_size;

#define CUB_WRAPPER(func, ...) do {                                       \
  void *temp_storage = nullptr;                                           \
  size_t temp_storage_bytes = 0;                                          \
  func(temp_storage, temp_storage_bytes, __VA_ARGS__);                    \
} while (false)

void test1() {
  // CHECK: CUB_WRAPPER(cub::DeviceScan::InclusiveSum, d_in, d_out, n);
  CUB_WRAPPER(cub::DeviceScan::InclusiveSum, d_in, d_out, n);
}

void test2() {
  // CHECK: cub::DeviceScan::InclusiveSum(tmp, tmp_size, d_in, d_out, n);
  cub::DeviceScan::InclusiveSum(tmp, tmp_size, d_in, d_out, n);
}
