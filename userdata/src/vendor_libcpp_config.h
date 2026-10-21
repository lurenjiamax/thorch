// SPDX-License-Identifier: Apache-2.0
// Keep NDK configuration except the namespace used by Android platform libc++.
// A compiler -D alone is overwritten by NDK's __config_site.
#include <__config_site>
#undef _LIBCPP_ABI_NAMESPACE
#define _LIBCPP_ABI_NAMESPACE __1
