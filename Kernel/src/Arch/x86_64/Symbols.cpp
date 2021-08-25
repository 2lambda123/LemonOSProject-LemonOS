#include <Symbols.h>

#include <Debug.h>

#include <CString.h>
#include <Hash.h>
#include <StringView.h>
#include <Vector.h>

HashMap<StringView, KernelSymbol*> symbolHashMap;

void LoadSymbolsFromFile(FsNode* node) {
    unsigned bufferSize = node->size;
    char* buffer = new char[node->size];

    ssize_t read = 0;
    if ((read = fs::Read(node, 0, bufferSize, buffer)) > 0) {
        size_t bufferPos = 0;
        while (bufferPos < read) {
            char* line = buffer + bufferPos;

            char* lineEnd = strnchr(line, '\n', read - bufferPos);
            if (!lineEnd) {
                break; // It is expected that there is a newline at the end of the file
                // So if there is not a newline here then we should either be at end of file or end of buffer
            }

            *lineEnd = 0; // Replace newline with null-terminator

            // Lines are in format <address> <type> <name>
            if (strlen(line) < sizeof(uintptr_t) * 2 +
                                   3) { // Must be at least length of address + space + length of type field + space
                bufferPos += (lineEnd - line) + 1; // Invalid or empty line
                continue;
            }

            char* addressEnd = strchr(line, ' ');
            if (!addressEnd) {
                bufferPos += (lineEnd - line) + 1; // Invalid or empty line
                continue;
            }

            KernelSymbol* sym = new KernelSymbol;
            if (HexStringToPointer(
                    line, sizeof(uintptr_t) * 2,
                    sym->address)) {           // The size of an address as a hex string should be sizeof(uintptr_t) * 2
                bufferPos += (lineEnd - line); // Invalid address string
                continue;
            }

            sym->mangledName = strdup(addressEnd + 3);

            Log::Debug(debugLevelSymbols, DebugLevelVerbose, "Found kernel symbol: %x, name: '%s'", sym->address,
                       sym->mangledName);

            symbolHashMap.insert(sym->mangledName, sym);

            bufferPos += (lineEnd - line) + 1;
        }
    }

    delete[] buffer;
    assert(read >= 0); // Ensure that there was no read errors
}

int ResolveKernelSymbol(const char* mangledName, KernelSymbol*& symbolPtr) {
    return symbolHashMap.get(mangledName, symbolPtr);
}

void AddKernelSymbol(KernelSymbol* sym) {
    assert(!symbolHashMap.find(sym->mangledName));

    symbolHashMap.insert(sym->mangledName, sym);
}

void RemoveKernelSymbol(const char* mangledName) { symbolHashMap.remove(mangledName); }