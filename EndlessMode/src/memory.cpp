#include "memory.h"

#include <Windows.h>
#include <cstring>

namespace memory {
    bool SafeRead(uintptr_t addr, void* out, size_t len) {
        __try {
            memcpy(out, (const void*)addr, len);
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    bool SafeWrite(uintptr_t addr, const void* in, size_t len) {
        __try {
            memcpy((void*)addr, in, len);
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

    bool SafeCall(uintptr_t (*fn)(void*), void* arg, uintptr_t& out) {
        __try {
            out = fn(arg);
            return true;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            out = 0;
            return false;
        }
    }
}
