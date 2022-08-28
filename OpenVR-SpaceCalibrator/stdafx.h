#pragma once

#if  !defined(_WIN32) && !defined(_WIN64)
#include <wchar.h>
#include "compat.h"

#else
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#endif

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <iostream>
