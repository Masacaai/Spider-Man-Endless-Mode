#pragma once

#include <cstdint>

// Hardware write watchpoint (debug register DR0) on every game thread, to find the code that writes a given address
// (e.g. which function equips a suit mod when the menu writes the loadout block). The vectored exception handler only
// records hits (RIP and exe return addresses found on the stack) into a fixed buffer; `wp dump` logs them.
//   wp set <addr|exe+rva>   arm (8-byte write watch)
//   wp dump                 log the recorded hits
//   wp clear                disarm on all threads
namespace watchpoint {
    void Set(uintptr_t addr);
    void Clear();
    void Dump();
}
