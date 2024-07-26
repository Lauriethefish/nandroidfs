#pragma once

// Avoid including winsock.h with dokan since we are using winsock2
#define _WINSOCKAPI_
#include <dokan.h>
#undef _WINSOCKAPI_
