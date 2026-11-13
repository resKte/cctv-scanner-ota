#pragma once

#include <stddef.h>

struct RouterBindingRecord {
  const char *ip;
  const char *mac;
  const char *description;
  bool enabled;
};

struct RouterTableRecord {
  const char *gatewayIp;
  const char *gatewayMac;
  const char *brand;
  const char *subnet;
  size_t firstBinding;
  size_t bindingCount;
};

extern const RouterBindingRecord routerBindingRecords[];
extern const size_t routerBindingRecordCount;
extern const RouterTableRecord routerTableRecords[];
extern const size_t routerTableRecordCount;
