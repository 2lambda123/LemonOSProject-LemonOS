#include <Syscalls.h>

#include <CPU.h>
#include <Debug.h>
#include <Device.h>
#include <Errno.h>
#include <Framebuffer.h>
#include <HAL.h>
#include <IDT.h>
#include <Lemon.h>
#include <Lock.h>
#include <Logging.h>
#include <Math.h>
#include <Modules.h>
#include <Net/Socket.h>
#include <Objects/Service.h>
#include <OnCleanup.h>
#include <Pair.h>
#include <PhysicalAllocator.h>
#include <SMP.h>
#include <Scheduler.h>
#include <SharedMemory.h>
#include <Signal.h>
#include <StackTrace.h>
#include <TTY/PTY.h>
#include <Timer.h>
#include <UserPointer.h>
#include <Video/Video.h>

#include <ABI/Process.h>

#include <abi-bits/vm-flags.h>
#include <abi-bits/wait.h>

#define EXEC_CHILD 1

long SysRead(RegisterContext* r);
long SysWrite(RegisterContext* r);
long SysOpen(RegisterContext* r);
long SysCreate(RegisterContext* r);
long SysLink(RegisterContext* r);
long SysUnlink(RegisterContext* r);
long SysChdir(RegisterContext* r);
long SysChmod(RegisterContext* r);
long SysFStat(RegisterContext* r);
long SysStat(RegisterContext* r);
long SysLSeek(RegisterContext* r);
long SysMount(RegisterContext* r);
long SysMkdir(RegisterContext* r);
long SysRmdir(RegisterContext* r);
long SysRename(RegisterContext* r);
long SysReadDirNext(RegisterContext* r);
long SysRenameAt(RegisterContext* r);
long SysReadDir(RegisterContext* r);
long SysGetCWD(RegisterContext* r);
long SysPRead(RegisterContext* r);
long SysPWrite(RegisterContext* r);
long SysIoctl(RegisterContext* r);
long SysPoll(RegisterContext* r);
long SysReadLink(RegisterContext* r);
long SysDup(RegisterContext* r);
long SysGetFileStatusFlags(RegisterContext* r);
long SysSetFileStatusFlags(RegisterContext* r);
long SysSelect(RegisterContext* r);
long SysEpollCreate(RegisterContext* r);
long SysEPollCtl(RegisterContext* r);
long SysEpollWait(RegisterContext* r);
long SysPipe(RegisterContext* r);
long SysFChdir(RegisterContext* r);

long SysExit(RegisterContext* r) {
    int code = SC_ARG0(r);

    Log::Info("Process %s (PID: %d) exiting with code %d", Scheduler::GetCurrentProcess()->name,
              Scheduler::GetCurrentProcess()->PID(), code);

    IF_DEBUG(debugLevelSyscalls >= DebugLevelVerbose, {
        Log::Info("rip: %x", r->rip);
        UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
    });

    Process::Current()->exitCode = code;
    Process::Current()->Die();
    return 0;
}

long SysExec(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    size_t filePathLength;
    long filePathInvalid =
        strlenSafe(reinterpret_cast<char*>(SC_ARG0(r)), filePathLength, currentProcess->addressSpace);
    if (filePathInvalid) {
        Log::Warning("SysExec: Reached unallocated memory reading file path");
        return -EFAULT;
    }

    char filepath[filePathLength + 1];

    strncpy(filepath, (char*)SC_ARG0(r), filePathLength);
    int argc = SC_ARG1(r);
    char** argv = (char**)SC_ARG2(r);
    uint64_t flags = SC_ARG3(r);
    char** envp = (char**)SC_ARG4(r);

    FsNode* node = fs::ResolvePath(filepath, currentProcess->workingDir->node, true /* Follow Symlinks */);
    if (!node) {
        return -ENOENT;
    }

    Vector<String> kernelEnvp;
    if (envp) {
        int i = 0;
        while (envp[i]) {
            size_t len;
            if (strlenSafe(envp[i], len, currentProcess->addressSpace)) {
                Log::Warning("SysExec: Reached unallocated memory reading environment");
                return -EFAULT;
            }

            kernelEnvp.add_back(envp[i]);
            i++;
        }
    }

    Log::Info("Loading: %s", (char*)SC_ARG0(r));

    Vector<String> kernelArgv;
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) { // Some programs may attempt to terminate argv with a null pointer
            argc = i;
            break;
        }

        size_t len;
        if (strlenSafe(argv[i], len, currentProcess->addressSpace)) {
            Log::Warning("SysExec: Reached unallocated memory reading argv");
            return -EFAULT;
        }

        kernelArgv.add_back(String(argv[i]));
    }

    if (!kernelArgv.size()) {
        kernelArgv.add_back(filepath); // Ensure at least argv[0] is set
    }

    timeval tv = Timer::GetSystemUptimeStruct();
    uint8_t* buffer = (uint8_t*)kmalloc(node->size);
    size_t read = fs::Read(node, 0, node->size, buffer);
    if (read != node->size) {
        Log::Warning("Could not read file: %s", filepath);
        return 0;
    }
    timeval tvnew = Timer::GetSystemUptimeStruct();
    Log::Info("Done (took %d us)", Timer::TimeDifference(tvnew, tv));
    FancyRefPtr<Process> proc = Process::CreateELFProcess((void*)buffer, kernelArgv, kernelEnvp, filepath,
                                                          ((flags & EXEC_CHILD) ? currentProcess : nullptr));
    kfree(buffer);

    if (!proc) {
        Log::Warning("SysExec: Proc is null!");
        return -EIO; // Failed to create process
    }

    proc->workingDir = currentProcess->workingDir;
    strncpy(proc->workingDirPath, currentProcess->workingDirPath, PATH_MAX);

    if (flags & EXEC_CHILD) {
        currentProcess->RegisterChildProcess(proc);

        // Copy handles
        proc->m_handles.resize(currentProcess->HandleCount());
        for (unsigned i = 0; i < proc->m_handles.size(); i++) {
            if (!currentProcess->m_handles[i].closeOnExec) {
                proc->m_handles[i] = currentProcess->m_handles[i];
            } else {
                proc->m_handles[i] = HANDLE_NULL;
            }
        }
    }

    proc->Start();
    return proc->PID();
}

long SysCloseHandle(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    int err = currentProcess->DestroyHandle(SC_ARG0(r));
    if (err) {
        return -EBADF;
    }

    return 0;
}

long SysSleep(RegisterContext* r) { return 0; }

long SysExecve(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();
    if (currentProcess->m_threads.get_length() > 1) {
        Log::Error("SysExecve does not kill other threads");
        return -ENOSYS;
    }

    size_t filePathLength;
    long filePathInvalid =
        strlenSafe(reinterpret_cast<char*>(SC_ARG0(r)), filePathLength, currentProcess->addressSpace);
    if (filePathInvalid) {
        Log::Warning("SysExec: Reached unallocated memory reading file path");
        return -EFAULT;
    }

    char filepath[filePathLength + 1];

    strncpy(filepath, (char*)SC_ARG0(r), filePathLength);
    char** argv = (char**)SC_ARG1(r);
    char** envp = (char**)SC_ARG2(r);

    FsNode* node = fs::ResolvePath(filepath, currentProcess->workingDir->node, true /* Follow Symlinks */);
    if (!node) {
        return -ENOENT;
    }

    Vector<String> kernelEnvp;
    if (envp) {
        int i = 0;
        while (envp[i]) {
            size_t len;
            if (strlenSafe(envp[i], len, currentProcess->addressSpace)) {
                Log::Warning("SysExecve: Reached unallocated memory reading environment");
                return -EFAULT;
            }

            kernelEnvp.add_back(envp[i]);
            i++;
        }
    }

    Log::Info("Loading: %s", (char*)SC_ARG0(r));

    Vector<String> kernelArgv;
    if (argv) {
        int i = 0;
        while (argv[i]) {
            size_t len;
            if (strlenSafe(argv[i], len, currentProcess->addressSpace)) {
                Log::Warning("SysExecve: Reached unallocated memory reading argv");
                return -EFAULT;
            }

            kernelArgv.add_back(argv[i]);
            i++;
        }
    }

    if (!kernelArgv.size()) {
        kernelArgv.add_back(filepath); // Ensure at least argv[0] is set
    }

    timeval tv = Timer::GetSystemUptimeStruct();
    uint8_t* buffer = (uint8_t*)kmalloc(node->size);
    size_t read = fs::Read(node, 0, node->size, buffer);
    if (read != node->size) {
        Log::Warning("Could not read file: %s", filepath);
        return -EIO;
    }
    timeval tvnew = Timer::GetSystemUptimeStruct();
    Log::Info("Done (took %d us)", Timer::TimeDifference(tvnew, tv));

    Thread* currentThread = Thread::Current();
    ScopedSpinLock lockProcess(currentProcess->m_processLock);

    // Create a fresh AddressSpace for the new process image
    AddressSpace* newSpace = new AddressSpace(Memory::CreatePageMap());
    AddressSpace* oldSpace = currentProcess->addressSpace;

    // Reset register state
    memset(r, 0, sizeof(RegisterContext));

    MappedRegion* stackRegion =
        newSpace->AllocateAnonymousVMObject(0x200000, 0, false); // 2MB max stacksize
    currentThread->stack = reinterpret_cast<void*>(stackRegion->base);               // 256KB stack size
    r->rsp = (uintptr_t)currentThread->stack + 0x200000;

    stackRegion->vmObject->Hit(stackRegion->base, 0x200000 - 0x1000, newSpace->GetPageMap());
    stackRegion->vmObject->Hit(stackRegion->base, 0x200000 - 0x2000, newSpace->GetPageMap());

    asm volatile("cli");
    currentProcess->addressSpace = newSpace;
    asm volatile("mov %%rax, %%cr3; sti" ::"a"(newSpace->GetPageMap()->pml4Phys));

    delete oldSpace;

    currentProcess->MapSignalTrampoline();

    // Force the first 8KB to be allocated
    // TODO: PageMap race cond

    elf_info_t elfInfo = LoadELFSegments(currentProcess, buffer, 0);
    r->rip = currentProcess->LoadELF(&r->rsp, elfInfo, kernelArgv, kernelEnvp, filepath);
    kfree(buffer);

    if (!r->rip) {
        // Its really important that we kill the process afterwards,
        // Otherwise the process pointer reference counter will not be decremeneted,
        // and the ScopedSpinlocks will not be released.
        currentThread->Signal(SIGKILL);
        return -1;
    }

    strncpy(currentProcess->name, kernelArgv[0].c_str(), sizeof(currentProcess->name));

    assert(!(r->rsp & 0xF));

    r->cs = USER_CS;
    r->ss = USER_SS;

    r->rbp = r->rsp;
    r->rflags = 0x202; // IF - Interrupt Flag, bit 1 should be 1
    memset(currentThread->fxState, 0, 4096);

    ((fx_state_t*)currentThread->fxState)->mxcsr = 0x1f80; // Default MXCSR (SSE Control Word) State
    ((fx_state_t*)currentThread->fxState)->mxcsrMask = 0xffbf;
    ((fx_state_t*)currentThread->fxState)->fcw = 0x33f; // Default FPU Control Word State

    // Restore default FPU state
    asm volatile("fxrstor64 (%0)" ::"r"((uintptr_t)currentThread->fxState) : "memory");

    ScopedSpinLock lockProcessFds(currentProcess->m_handleLock);
    for (Handle& fd : currentProcess->m_handles) {
        if (fd.closeOnExec) {
            fd = HANDLE_NULL;
        }
    }

    return 0;
}

long SysTime(RegisterContext* r) { return 0; }

long SysMapFB(RegisterContext* r) {
    video_mode_t vMode = Video::GetVideoMode();
    Process* process = Scheduler::GetCurrentProcess();

    MappedRegion* region = nullptr;
    region = process->addressSpace->MapVMO(Video::GetFramebufferVMO(), 0, false);

    if (!region || !region->Base()) {
        return -1;
    }

    uintptr_t fbVirt = region->Base();

    fb_info_t fbInfo;
    fbInfo.width = vMode.width;
    fbInfo.height = vMode.height;
    fbInfo.bpp = vMode.bpp;
    fbInfo.pitch = vMode.pitch;

    if (HAL::debugMode)
        fbInfo.height = vMode.height / 3 * 2;

    UserPointer<uintptr_t> virt = SC_ARG0(r);
    UserPointer<fb_info_t> info = SC_ARG1(r);

    TRY_STORE_UMODE_VALUE(virt, fbVirt);
    TRY_STORE_UMODE_VALUE(info, fbInfo);

    Log::Info("fb mapped at %x", region->Base());

    return 0;
}

/////////////////////////////
/// \name SysGetTID - Get thread ID
///
/// \return Current thread's ID
/////////////////////////////
long SysGetTID(RegisterContext* r) { return Thread::Current()->tid; }
long SysGetPID(RegisterContext* r) {
    uint64_t* pid = (uint64_t*)SC_ARG0(r);

    *pid = Scheduler::GetCurrentProcess()->PID();

    return 0;
}

long SysYield(RegisterContext* r) {
    Scheduler::Yield();
    return 0;
}

// SendMessage(message_t* msg) - Sends an IPC message to a process
long SysSendMessage(RegisterContext* r) {
    Log::Warning("SysSendMessage is a stub!");
    return -ENOSYS;
}

// RecieveMessage(message_t* msg) - Grabs next message on queue and copies it to msg
long SysReceiveMessage(RegisterContext* r) {
    Log::Warning("SysReceiveMessage is a stub!");
    return -ENOSYS;
}

long SysUptime(RegisterContext* r) {
    uint64_t* ns = (uint64_t*)SC_ARG0(r);
    if (ns) {
        *ns = Timer::UsecondsSinceBoot() * 1000;
    }
    return 0;
}

long SysDebug(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    size_t sLen;
    if (strlenSafe((char*)SC_ARG0(r), sLen, proc->addressSpace)) {
        return -EFAULT;
    }

    Log::Info("(%s): %s", proc->name, (char*)SC_ARG0(r));
    return 0;
}

long SysGetVideoMode(RegisterContext* r) {
    video_mode_t vMode = Video::GetVideoMode();
    fb_info_t fbInfo;
    fbInfo.width = vMode.width;
    fbInfo.height = vMode.height;
    if (HAL::debugMode)
        fbInfo.height = vMode.height / 3 * 2;
    fbInfo.bpp = vMode.bpp;
    fbInfo.pitch = vMode.pitch;

    *((fb_info_t*)SC_ARG0(r)) = fbInfo;

    return 0;
}

long SysUName(RegisterContext* r) {
    if (Memory::CheckUsermodePointer(SC_ARG0(r), strlen(Lemon::versionString),
                                     Scheduler::GetCurrentProcess()->addressSpace)) {
        strcpy((char*)SC_ARG0(r), Lemon::versionString);
        return 0;
    }

    return -EFAULT;
}

long SysSetFsBase(RegisterContext* r) {
    asm volatile("wrmsr" ::"a"(SC_ARG0(r) & 0xFFFFFFFF) /*Value low*/,
                 "d"((SC_ARG0(r) >> 32) & 0xFFFFFFFF) /*Value high*/, "c"(0xC0000100) /*Set FS Base*/);
    Thread::Current()->fsBase = SC_ARG0(r);
    return 0;
}

long SysMmap(RegisterContext* r) {
    uint64_t* address = (uint64_t*)SC_ARG0(r);
    size_t size = SC_ARG1(r);
    uintptr_t hint = SC_ARG2(r);
    uint64_t flags = SC_ARG3(r);

    if (!size) {
        return -EINVAL; // We do not accept 0-length mappings
    }

    Process* proc = Scheduler::GetCurrentProcess();

    bool fixed = flags & MAP_FIXED;
    bool anon = flags & MAP_ANON;
    // bool privateMapping = flags & MAP_PRIVATE;

    uint64_t unknownFlags = flags & ~static_cast<uint64_t>(MAP_ANON | MAP_FIXED | MAP_PRIVATE);
    if (unknownFlags || !anon) {
        Log::Warning("SysMmap: Unsupported mmap flags %x", flags);
        return -EINVAL;
    }

    if (size > 0x40000000) { // size > 1GB
        Log::Warning("MMap size: %u (>1GB)", size);
        Log::Info("rip: %x", r->rip);
        UserPrintStackTrace(r->rbp, proc->addressSpace);
        return -EINVAL;
    }

    MappedRegion* region = proc->addressSpace->AllocateAnonymousVMObject(size, hint, fixed);
    if (!region || !region->base) {
        IF_DEBUG((debugLevelSyscalls >= DebugLevelNormal), {
            Log::Error("SysMmap: Failed to map region (hint %x)!", hint);
            Log::Info("rip: %x", r->rip);
            UserPrintStackTrace(r->rbp, proc->addressSpace);
        });

        return -1;
    }

    *address = region->base;
    return 0;
}

long SysWaitPID(RegisterContext* r) {
    int pid = SC_ARG0(r);
    int* status = reinterpret_cast<int*>(SC_ARG1(r));
    int64_t flags = SC_ARG2(r);

    Process* currentProcess = Scheduler::GetCurrentProcess();
    FancyRefPtr<Process> child = nullptr;

    if (pid == -1) {
        child = currentProcess->RemoveDeadChild();
        if (child.get()) {
            return child->PID();
        }

        if (flags & WNOHANG) {
            return 0;
        }

        if (int e = currentProcess->WaitForChildToDie(child); e) {
            return e; // Error waiting for a child to die
        }

        assert(child.get());
        if (status) {
            *status = child->exitCode;
        }

        return child->PID();
    }

    if ((child = currentProcess->FindChildByPID(pid)).get()) {
        if (child->IsDead()) {
            pid = child->PID();
            currentProcess->RemoveDeadChild(child->PID());
            return pid;
        }

        if (flags & WNOHANG) {
            return 0;
        }

        pid = 0;
        KernelObjectWatcher bl;

        bl.WatchObject(static_pointer_cast<KernelObject>(child), 0);

        if (bl.Wait()) {
            if (child->IsDead()) { // Could have been SIGCHLD signal
                currentProcess->RemoveDeadChild(child->PID());
                return child->PID();
            } else {
                return -EINTR;
            }
        }

        assert(child->IsDead());
        if (status) {
            *status = child->exitCode;
        }

        currentProcess->RemoveDeadChild(child->PID());

        return child->PID();
    }

    return -ECHILD;
}

long SysNanoSleep(RegisterContext* r) {
    uint64_t nanoseconds = SC_ARG0(r);
    if (!nanoseconds) {
        return 0;
    }

    Thread::Current()->Sleep(nanoseconds / 1000);

    return 0;
}

long SysInfo(RegisterContext* r) {
    lemon_sysinfo_t* s = (lemon_sysinfo_t*)SC_ARG0(r);

    if (!s) {
        return -EFAULT;
    }

    s->usedMem = Memory::usedPhysicalBlocks * 4;
    s->totalMem = HAL::mem_info.totalMemory / 1024;
    s->cpuCount = static_cast<uint16_t>(SMP::processorCount);

    return 0;
}

/*
 * SysMunmap - Unmap memory (addr, size)
 *
 * On success - return 0
 * On failure - return -1
 */
long SysMunmap(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    uint64_t address = SC_ARG0(r);
    size_t size = SC_ARG1(r);

    if (address & (PAGE_SIZE_4K - 1) || size & (PAGE_SIZE_4K - 1)) {
        return -EINVAL; // Must be aligned
    }

    return process->addressSpace->UnmapMemory(address, size);
}

/*
 * SysCreateSharedMemory (key, size, flags, recipient) - Create Shared Memory
 * key - Pointer to memory key
 * size - memory size
 * flags - flags
 * recipient - (if private flag) PID of the process that can access memory
 *
 * On Success - Return 0, key greater than 1
 * On Failure - Return -1, key null
 */
long SysCreateSharedMemory(RegisterContext* r) {
    int64_t* key = (int64_t*)SC_ARG0(r);
    uint64_t size = SC_ARG1(r);
    uint64_t flags = SC_ARG2(r);
    uint64_t recipient = SC_ARG3(r);

    *key = Memory::CreateSharedMemory(size, flags, Scheduler::GetCurrentProcess()->PID(), recipient);
    assert(*key);

    return 0;
}

/*
 * SysMapSharedMemory (ptr, key, hint) - Map Shared Memory
 * ptr - Pointer to pointer of mapped memory
 * key - Memory key
 * key - Address hint
 *
 * On Success - ptr > 0
 * On Failure - ptr = 0
 */
long SysMapSharedMemory(RegisterContext* r) {
    void** ptr = (void**)SC_ARG0(r);
    int64_t key = SC_ARG1(r);
    uint64_t hint = SC_ARG2(r);

    *ptr = Memory::MapSharedMemory(key, Scheduler::GetCurrentProcess(), hint);

    return 0;
}

/*
 * SysUnmapSharedMemory (address, key) - Map Shared Memory
 * address - address of mapped memory
 * key - Memory key
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysUnmapSharedMemory(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    uint64_t address = SC_ARG0(r);
    int64_t key = SC_ARG1(r);

    {
        FancyRefPtr<SharedVMObject> sMem = Memory::GetSharedMemory(key);
        if (!sMem.get())
            return -EINVAL;

        MappedRegion* region = proc->addressSpace->AddressToRegionWriteLock(address);
        if (!region || region->vmObject != sMem) {
            region->lock.ReleaseWrite();
            return -EINVAL; // Invalid memory region
        }

        long r = proc->addressSpace->UnmapRegion(region);
        assert(!r);
    }

    Memory::DestroySharedMemory(key); // Active shared memory will not be destroyed and this will return
    return 0;
}

/*
 * SysDestroySharedMemory (key) - Destroy Shared Memory
 * key - Memory key
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysDestroySharedMemory(RegisterContext* r) {
    uint64_t key = SC_ARG0(r);

    if (Memory::CanModifySharedMemory(Scheduler::GetCurrentProcess()->PID(), key)) {
        Memory::DestroySharedMemory(key);
    } else
        return -EPERM;

    return 0;
}

/*
 * SysSocket (domain, type, protocol) - Create socket
 * domain - socket domain
 * type - socket type
 * protcol - socket protocol
 *
 * On Success - return file descriptor
 * On Failure - return -1
 */
long SysSocket(RegisterContext* r) {
    int domain = SC_ARG0(r);
    int type = SC_ARG1(r);
    int protocol = SC_ARG2(r);

    Socket* sock = nullptr;
    int e = Socket::CreateSocket(domain, type, protocol, &sock);

    if (e < 0)
        return e;
    assert(sock);

    UNIXOpenFile* fDesc = SC_TRY_OR_ERROR(fs::Open(sock, 0));

    if (type & SOCK_NONBLOCK)
        fDesc->mode |= O_NONBLOCK;

    return Scheduler::GetCurrentProcess()->AllocateHandle(fDesc);
}

/*
 * SysBind (sockfd, addr, addrlen) - Bind address to socket
 * sockfd - Socket file descriptor
 * addr - sockaddr structure
 * addrlen - size of addr
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysBind(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        Log::Warning("SysBind: Invalid File Descriptor: %d", SC_ARG0(r));
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        Log::Warning("SysBind: File (Descriptor: %d) is not a socket", SC_ARG0(r));
        return -ENOTSOCK;
    }

    socklen_t len = SC_ARG2(r);

    sockaddr_t* addr = (sockaddr_t*)SC_ARG1(r);
    if (!Memory::CheckUsermodePointer(SC_ARG1(r), len, proc->addressSpace)) {
        Log::Warning("SysBind: Invalid sockaddr ptr");
        return -EINVAL;
    }

    Socket* sock = (Socket*)handle->node;
    return sock->Bind(addr, len);
}

/*
 * SysListen (sockfd, backlog) - Marks socket as passive
 * sockfd - socket file descriptor
 * backlog - maximum amount of pending connections
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysListen(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        Log::Warning("SysListen: Invalid File Descriptor: %d", SC_ARG0(r));
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        Log::Warning("SysListen: File (Descriptor: %d) is not a socket", SC_ARG0(r));
        return -ENOTSOCK;
    }

    Socket* sock = (Socket*)handle->node;
    return sock->Listen(SC_ARG1(r));
}

/////////////////////////////
/// \name SysAccept (sockfd, addr, addrlen) - Accept socket connection
/// \param sockfd - Socket file descriptor
/// \param addr - sockaddr structure
/// \param addrlen - pointer to size of addr
///
/// \return File descriptor of accepted socket or negative error code
/////////////////////////////
long SysAccept(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        Log::Warning("SysAccept: Invalid File Descriptor: %d", SC_ARG0(r));
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        Log::Warning("SysAccept: File (Descriptor: %d) is not a socket", SC_ARG0(r));
        return -ENOTSOCK;
    }

    socklen_t* len = (socklen_t*)SC_ARG2(r);
    if (len && !Memory::CheckUsermodePointer(SC_ARG2(r), sizeof(socklen_t), proc->addressSpace)) {
        Log::Warning("SysAccept: Invalid socklen ptr");
        return -EFAULT;
    }

    sockaddr_t* addr = (sockaddr_t*)SC_ARG1(r);
    if (addr && !Memory::CheckUsermodePointer(SC_ARG1(r), *len, proc->addressSpace)) {
        Log::Warning("SysAccept: Invalid sockaddr ptr");
        return -EFAULT;
    }

    Socket* sock = (Socket*)handle->node;

    Socket* newSock = sock->Accept(addr, len, handle->mode);
    if (!newSock) {
        return -EAGAIN;
    }

    UNIXOpenFile* newHandle = SC_TRY_OR_ERROR(fs::Open(newSock));
    return proc->AllocateHandle(newHandle);
}

/////////////////////////////
/// \name SysConnect (sockfd, addr, addrlen) - Initiate socket connection
/// \param sockfd - Socket file descriptor
/// \param addr - sockaddr structure
/// \param addrlen - size of addr
///
/// \return 0 on success or negative error code on failure
/////////////////////////////
long SysConnect(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        Log::Warning("SysConnect: Invalid File Descriptor: %d", SC_ARG0(r));
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        Log::Warning("sys_connect: File (Descriptor: %d) is not a socket", SC_ARG0(r));
        return -EFAULT;
    }

    socklen_t len = SC_ARG2(r);

    sockaddr_t* addr = (sockaddr_t*)SC_ARG1(r);
    if (!Memory::CheckUsermodePointer(SC_ARG1(r), len, proc->addressSpace)) {
        Log::Warning("sys_connect: Invalid sockaddr ptr");
        return -EFAULT;
    }

    Socket* sock = (Socket*)handle->node;
    return sock->Connect(addr, len);
}

/*
 * SetGetUID () - Get Process UID
 *
 * On Success - Return process UID
 * On Failure - Does not fail
 */
long SysGetUID(RegisterContext* r) { return Scheduler::GetCurrentProcess()->uid; }

/*
 * SetSetUID () - Set Process UID
 *
 * On Success - Return process UID
 * On Failure - Return negative value
 */
long SysSetUID(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    uid_t requestedUID = SC_ARG0(r);

    if (proc->uid == requestedUID) {
        return 0;
    }

    if (proc->euid == 0) {
        proc->uid = requestedUID;
        proc->euid = requestedUID;
    } else {
        return -EPERM;
    }

    return 0;
}

/*
 * SysSend (sockfd, msg, flags) - Send data through a socket
 * sockfd - Socket file descriptor
 * msg - Message Header
 * flags - flags
 *
 * On Success - return amount of data sent
 * On Failure - return -1
 */
long SysSendMsg(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysSendMsg: Invalid File Descriptor: %d", SC_ARG0(r)); });
        return -EBADF;
    }

    msghdr* msg = (msghdr*)SC_ARG1(r);
    uint64_t flags = SC_ARG3(r);

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {

        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysSendMsg: File (Descriptor: %d) is not a socket", SC_ARG0(r)); });
        return -ENOTSOCK;
    }

    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(msghdr), proc->addressSpace)) {

        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, { Log::Warning("SysSendMsg: Invalid msg ptr"); });
        return -EFAULT;
    }

    if (!Memory::CheckUsermodePointer((uintptr_t)msg->msg_iov, sizeof(iovec) * msg->msg_iovlen, proc->addressSpace)) {

        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, { Log::Warning("SysSendMsg: msg: Invalid iovec ptr"); });
        return -EFAULT;
    }

    if (msg->msg_name && msg->msg_namelen &&
        !Memory::CheckUsermodePointer((uintptr_t)msg->msg_name, msg->msg_namelen, proc->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysSendMsg: msg: Invalid name ptr and name not null"); });
        return -EFAULT;
    }

    if (msg->msg_control && msg->msg_controllen &&
        !Memory::CheckUsermodePointer((uintptr_t)msg->msg_control, msg->msg_controllen, proc->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysSendMsg: msg: Invalid control ptr and control null"); });
        return -EFAULT;
    }

    long sent = 0;
    Socket* sock = (Socket*)handle->node;

    for (unsigned i = 0; i < msg->msg_iovlen; i++) {
        if (!Memory::CheckUsermodePointer((uintptr_t)msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len,
                                          proc->addressSpace)) {
            Log::Warning("SysSendMsg: msg: Invalid iovec entry base");
            return -EFAULT;
        }

        long ret = sock->SendTo(msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, flags, (sockaddr*)msg->msg_name,
                                msg->msg_namelen, msg->msg_control, msg->msg_controllen);

        if (ret < 0)
            return ret;

        sent += ret;
    }

    return sent;
}

/*
 * SysRecvMsg (sockfd, msg, flags) - Recieve data through socket
 * sockfd - Socket file descriptor
 * msg - Message Header
 * flags - flags
 *
 * On Success - return amount of data received
 * On Failure - return -1
 */
long SysRecvMsg(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(proc->GetHandleAs<UNIXOpenFile>(SC_ARG0(r)));
    if (!handle) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysRecvMsg: Invalid File Descriptor: %d", SC_ARG0(r)); });
        return -EBADF;
    }

    msghdr* msg = (msghdr*)SC_ARG1(r);
    uint64_t flags = SC_ARG3(r);

    if (handle->mode & O_NONBLOCK) {
        flags |= MSG_DONTWAIT; // Don't wait if socket marked as nonblock
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysRecvMsg: File (Descriptor: %d) is not a socket", SC_ARG0(r)); });
        return -ENOTSOCK;
    }

    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(msghdr), proc->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, { Log::Warning("SysRecvMsg: Invalid msg ptr"); });
        return -EFAULT;
    }

    if (!Memory::CheckUsermodePointer((uintptr_t)msg->msg_iov, sizeof(iovec) * msg->msg_iovlen, proc->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, { Log::Warning("SysRecvMsg: msg: Invalid iovec ptr"); });
        return -EFAULT;
    }

    if (msg->msg_control && msg->msg_controllen &&
        !Memory::CheckUsermodePointer((uintptr_t)msg->msg_control, msg->msg_controllen, proc->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                 { Log::Warning("SysRecvMsg: msg: Invalid control ptr and control null"); });
        return -EFAULT;
    }

    long read = 0;
    Socket* sock = (Socket*)handle->node;

    for (unsigned i = 0; i < msg->msg_iovlen; i++) {
        if (!Memory::CheckUsermodePointer((uintptr_t)msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len,
                                          proc->addressSpace)) {
            Log::Warning("SysRecvMsg: msg: Invalid iovec entry base");
            return -EFAULT;
        }

        socklen_t len = msg->msg_namelen;
        long ret =
            sock->ReceiveFrom(msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, flags,
                              reinterpret_cast<sockaddr*>(msg->msg_name), &len, msg->msg_control, msg->msg_controllen);
        msg->msg_namelen = len;

        if (ret < 0) {
            return ret;
        }

        read += ret;
    }

    return read;
}

/////////////////////////////
/// \brief SysGetEUID () - Set effective process UID
///
/// \return Process EUID (int)
/////////////////////////////
long SysGetEUID(RegisterContext* r) { return Scheduler::GetCurrentProcess()->euid; }

/////////////////////////////
/// \brief SysSetEUID () - Set effective process UID
///
/// \return On success return 0, otherwise return negative error code
/////////////////////////////
long SysSetEUID(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    uid_t requestedEUID = SC_ARG0(r);

    // Unprivileged processes may only set the effective user ID to the real user ID or the effective user ID
    if (proc->euid == requestedEUID) {
        return 0;
    }

    if (proc->uid == 0 || proc->uid == requestedEUID) {
        proc->euid = requestedEUID;
    } else {
        return -EPERM;
    }

    return 0;
}

/////////////////////////////
/// \brief SysGetProcessInfo (pid, pInfo)
///
/// \param pid - Process PID
/// \param pInfo - Pointer to lemon_process_info_t structure
///
/// \return On Success - Return 0
/// \return On Failure - Return error as negative value
/////////////////////////////
long SysGetProcessInfo(RegisterContext* r) {
    uint64_t pid = SC_ARG0(r);
    lemon_process_info_t* pInfo = reinterpret_cast<lemon_process_info_t*>(SC_ARG1(r));

    Process* cProcess = Scheduler::GetCurrentProcess();
    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(lemon_process_info_t), cProcess->addressSpace)) {
        return -EFAULT;
    }

    FancyRefPtr<Process> reqProcess;
    if (!(reqProcess = Scheduler::FindProcessByPID(pid)).get()) {
        return -EINVAL;
    }

    pInfo->pid = pid;

    pInfo->threadCount = reqProcess->Threads().get_length();

    pInfo->uid = reqProcess->uid;
    pInfo->gid = reqProcess->gid;

    pInfo->state = reqProcess->GetMainThread()->state;

    strcpy(pInfo->name, reqProcess->name);

    pInfo->runningTime = Timer::GetSystemUptime() - reqProcess->creationTime.tv_sec;
    pInfo->activeUs = reqProcess->activeTicks * 1000000 / Timer::GetFrequency();

    pInfo->usedMem = reqProcess->addressSpace->UsedPhysicalMemory();
    pInfo->isCPUIdle = reqProcess->IsCPUIdleProcess();

    return 0;
}

/////////////////////////////
/// \brief SysGetNextProcessInfo (pidP, pInfo)
///
/// \param pidP - Pointer to an unsigned integer holding a PID
/// \param pInfo - Pointer to process_info_t struct
///
/// \return On Success - Return 0
/// \return No more processes - Return 1
/// On Failure - Return error as negative value
/////////////////////////////
long SysGetNextProcessInfo(RegisterContext* r) {
    pid_t* pidP = reinterpret_cast<pid_t*>(SC_ARG0(r));
    lemon_process_info_t* pInfo = reinterpret_cast<lemon_process_info_t*>(SC_ARG1(r));

    Process* cProcess = Scheduler::GetCurrentProcess();
    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(lemon_process_info_t), cProcess->addressSpace)) {
        return -EFAULT;
    }

    if (!Memory::CheckUsermodePointer(SC_ARG0(r), sizeof(pid_t), cProcess->addressSpace)) {
        return -EFAULT;
    }

    *pidP = Scheduler::GetNextProcessPID(*pidP);

    if (!(*pidP)) {
        return 1; // No more processes
    }

    FancyRefPtr<Process> reqProcess;
    if (!(reqProcess = Scheduler::FindProcessByPID(*pidP))) {
        return -EINVAL;
    }

    pInfo->pid = *pidP;

    pInfo->threadCount = reqProcess->Threads().get_length();

    pInfo->uid = reqProcess->uid;
    pInfo->gid = reqProcess->gid;

    pInfo->state = reqProcess->GetMainThread()->state;

    strcpy(pInfo->name, reqProcess->name);

    pInfo->runningTime = Timer::GetSystemUptime() - reqProcess->creationTime.tv_sec;
    pInfo->activeUs = reqProcess->activeTicks * 1000000 / Timer::GetFrequency();

    pInfo->usedMem = reqProcess->addressSpace->UsedPhysicalMemory();
    pInfo->isCPUIdle = reqProcess->IsCPUIdleProcess();

    return 0;
}

/////////////////////////////
/// \brief SysSpawnThread(entry, stack) Spawn a new thread under current process
///
/// \param entry - (uintptr_t) Thread entry point
/// \param stack - (uintptr_t) Thread's stack
///
/// \return (pid_t) thread id
/////////////////////////////
long SysSpawnThread(RegisterContext* r) {
    auto tid = Process::Current()->CreateChildThread(SC_ARG0(r), SC_ARG1(r), USER_CS, USER_SS);

    return tid;
}

/////////////////////////////
/// \brief SysExitThread(retval) Exit a thread
///
/// \param retval - (void*) Return value pointer
///
/// \return Undefined, always succeeds
/////////////////////////////
long SysExitThread(RegisterContext* r) {
    Thread* thread = Thread::Current();
    thread->blocker = nullptr;
    acquireLockIntDisable(&thread->stateLock);

    if (thread->state == ThreadStateRunning) {
        thread->state = ThreadStateBlocked;
    }

    thread->state = ThreadStateDying;

    releaseLock(&thread->stateLock);
    releaseLock(&thread->kernelLock);
    asm volatile("sti");

    return 0;
}

/////////////////////////////
/// \brief SysFutexWake(futex) Wake a thread waiting on a futex
///
/// \param futex - (int*) Futex pointer
///
/// \return 0 on success, error code on failure
/////////////////////////////
long SysFutexWake(RegisterContext* r) {
    int* futex = reinterpret_cast<int*>(SC_ARG0(r));

    if (!Memory::CheckUsermodePointer(SC_ARG0(r), sizeof(int), Scheduler::GetCurrentProcess()->addressSpace)) {
        return EFAULT;
    }

    Process* currentProcess = Scheduler::GetCurrentProcess();

    acquireLock(&currentProcess->m_futexLock);
    List<FutexThreadBlocker*>* blocked = nullptr;
    if (!currentProcess->futexWaitQueue.get(reinterpret_cast<uintptr_t>(futex), blocked) || !blocked->get_length()) {
        releaseLock(&currentProcess->m_futexLock);
        return 0;
    }

    auto front = blocked->remove_at(0);
    if (front && front->ShouldBlock())
        front->Unblock();

    releaseLock(&currentProcess->m_futexLock);
    return 0;
}

/////////////////////////////
/// \brief SysFutexWait(futex, expected) Wait on a futex.
///
/// Will wait on the futex if the value is equal to expected
///
/// \param futex (void*) Futex pointer
/// \param expected (int) Expected futex value
///
/// \return 0 on success, error code on failure
/////////////////////////////
long SysFutexWait(RegisterContext* r) {
    int* futex = reinterpret_cast<int*>(SC_ARG0(r));

    Process* currentProcess = Scheduler::GetCurrentProcess();
    Thread* currentThread = Thread::Current();

    if (!Memory::CheckUsermodePointer(SC_ARG0(r), sizeof(int), currentProcess->addressSpace)) {
        return EFAULT;
    }

    int expected = static_cast<int>(SC_ARG1(r));

    List<FutexThreadBlocker*>* blocked;

    acquireLock(&currentProcess->m_futexLock);
    if (!currentProcess->futexWaitQueue.get(reinterpret_cast<uintptr_t>(futex), blocked)) {
        blocked = new List<FutexThreadBlocker*>();

        currentProcess->futexWaitQueue.insert(reinterpret_cast<uintptr_t>(futex), blocked);
    }

    FutexThreadBlocker blocker;
    blocked->add_back(&blocker);

    releaseLock(&currentProcess->m_futexLock);

    while (*futex == expected) {
        if (currentThread->Block(&blocker)) {
            acquireLock(&currentProcess->m_futexLock);
            blocked->remove(&blocker);
            releaseLock(&currentProcess->m_futexLock);

            return -EINTR; // We were interrupted
        }
    }

    blocked->remove(&blocker);

    return 0;
}

/////////////////////////////
/// \brief SysCreateService (name) - Create a service
///
/// Create a new service. Services are essentially named containers for interfaces, allowing one program to implement
/// multiple interfaces under a service.
///
/// \param name (const char*) Service name to be used, must be unique
///
/// \return Handle ID on success, error code on failure
/////////////////////////////
long SysCreateService(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    size_t nameLength;
    if (strlenSafe(reinterpret_cast<const char*>(SC_ARG0(r)), nameLength, currentProcess->addressSpace)) {
        return -EFAULT;
    }

    char name[nameLength + 1];
    strncpy(name, reinterpret_cast<const char*>(SC_ARG0(r)), nameLength);
    name[nameLength] = 0;

    for (auto& svc : ServiceFS::Instance()->services) {
        if (strncmp(svc->GetName(), name, nameLength) == 0) {
            Log::Warning("SysCreateService: Service '%s' already exists!", svc->GetName());
            return -EEXIST;
        }
    }

    FancyRefPtr<Service> svc = ServiceFS::Instance()->CreateService(name);
    return currentProcess->AllocateHandle(static_pointer_cast<KernelObject, Service>(svc));
}

/////////////////////////////
/// \brief SysCreateInterface (service, name, msgSize) - Create a new interface
///
/// Create a new interface. Interfaces allow clients to open connections to a service
///
/// \param service (handle_id_t) Handle ID of the service hosting the interface
/// \param name (const char*) Name of the interface,
/// \param msgSize (uint16_t) Maximum message size for all connections
///
/// \return Handle ID of service on success, negative error code on failure
/////////////////////////////
long SysCreateInterface(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle svcHandle;
    if (!(svcHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("SysCreateInterface: Invalid handle ID %d", SC_ARG0(r));
        return -EBADF;
    }

    if (!svcHandle.ko->IsType(Service::TypeID())) {
        Log::Warning("SysCreateInterface: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    size_t nameLength;
    if (strlenSafe(reinterpret_cast<const char*>(SC_ARG1(r)), nameLength, currentProcess->addressSpace)) {
        return -EFAULT;
    }

    char name[nameLength + 1];
    strncpy(name, reinterpret_cast<const char*>(SC_ARG1(r)), nameLength);
    name[nameLength] = 0;

    Service* svc = reinterpret_cast<Service*>(svcHandle.ko.get());

    FancyRefPtr<MessageInterface> interface;
    long ret = svc->CreateInterface(interface, name, SC_ARG2(r));

    if (ret) {
        return ret;
    }

    return currentProcess->AllocateHandle(static_pointer_cast<KernelObject, MessageInterface>(interface));
}

/////////////////////////////
/// \brief SysInterfaceAccept (interface) - Accept connections on an interface
///
/// Accept a pending connection on an interface and return a new MessageEndpoint
///
/// \param interface (handle_id_t) Handle ID of the interface
///
/// \return Handle ID of endpoint on success, 0 when no pending connections, negative error code on failure
/////////////////////////////
long SysInterfaceAccept(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle ifHandle;
    if (!(ifHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("SysInterfaceAccept: Invalid handle ID %d", SC_ARG0(r));
        return -EBADF;
    }

    if (!ifHandle.ko->IsType(MessageInterface::TypeID())) {
        Log::Warning("SysInterfaceAccept: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    MessageInterface* interface = reinterpret_cast<MessageInterface*>(ifHandle.ko.get());
    FancyRefPtr<MessageEndpoint> endp;
    if (long ret = interface->Accept(endp); ret <= 0) {
        return ret;
    }

    return currentProcess->AllocateHandle(static_pointer_cast<KernelObject, MessageEndpoint>(endp));
}

/////////////////////////////
/// \brief SysInterfaceConnect (path) - Open a connection to an interface
///
/// Open a new connection on an interface and return a new MessageEndpoint
///
/// \param interface (const char*) Path of interface in format servicename/interfacename (e.g. lemon.lemonwm/wm)
///
/// \return Handle ID of endpoint on success, negative error code on failure
/////////////////////////////
long SysInterfaceConnect(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    size_t sz = 0;
    if (strlenSafe(reinterpret_cast<const char*>(SC_ARG0(r)), sz, currentProcess->addressSpace)) {
        return -EFAULT;
    }

    char path[sz + 1];
    strncpy(path, reinterpret_cast<const char*>(SC_ARG0(r)), sz);
    path[sz] = 0;

    if (!strchr(path, '/')) { // Interface name given by '/' separator
        Log::Warning("SysInterfaceConnect: No interface name given!");
        return -EINVAL;
    }

    FancyRefPtr<MessageInterface> interface;
    {
        FancyRefPtr<Service> svc;
        if (ServiceFS::Instance()->ResolveServiceName(svc, path)) {
            return -ENOENT; // No such service
        }

        if (svc->ResolveInterface(interface, strchr(path, '/') + 1)) {
            return -ENOENT; // No such interface
        }
    }

    FancyRefPtr<MessageEndpoint> endp = interface->Connect();
    if (!endp.get()) {
        Log::Warning("SysInterfaceConnect: Failed to connect!");
        return -EINVAL; // Some error connecting, interface destroyed?
    }

    return currentProcess->AllocateHandle(static_pointer_cast<KernelObject>(endp));
}

/////////////////////////////
/// \brief SysEndpointQueue (endpoint, id, size, data) - Queue a message on an endpoint
///
/// Queues a new message on the specified endpoint.
///
/// \param endpoint (handle_id_t) Handle ID of specified endpoint
/// \param id (uint64_t) Message ID
/// \param size (uint64_t) Message Size
/// \param data (uint8_t*) Pointer to message data
///
/// \return 0 on success, negative error code on failure
/////////////////////////////
long SysEndpointQueue(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle endpHandle;
    if (!(endpHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, {
            Log::Warning("(%s): SysEndpointQueue: Invalid handle ID %d", currentProcess->name, SC_ARG0(r));
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
        });
        return -EBADF;
    }

    if (!endpHandle.ko->IsType(MessageEndpoint::TypeID())) {
        Log::Warning("SysEndpointQueue: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    size_t size = SC_ARG2(r);
    if (size && !Memory::CheckUsermodePointer(SC_ARG3(r), size, currentProcess->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, {
            Log::Warning("(%s): SysEndpointQueue: Invalid data buffer %x", currentProcess->name, SC_ARG3(r));
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
        });
        return -EFAULT; // Data greater than 8 and invalid pointer
    }

    MessageEndpoint* endpoint = reinterpret_cast<MessageEndpoint*>(endpHandle.ko.get());

    return endpoint->Write(SC_ARG1(r), size, SC_ARG3(r));
}

/////////////////////////////
/// \brief SysEndpointDequeue (endpoint, id, size, data)
///
/// Accept a pending connection on an interface and return a new MessageEndpoint
///
/// \param endpoint (handle_id_t) Handle ID of specified endpoint
/// \param id (uint64_t*) Returned message ID
/// \param size (uint32_t*) Returned message Size
/// \param data (uint8_t*) Message data buffer
///
/// \return 0 on empty, 1 on success, negative error code on failure
/////////////////////////////
long SysEndpointDequeue(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle endpHandle;
    if (!(endpHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("(%s): SysEndpointDequeue: Invalid handle ID %d", currentProcess->name, SC_ARG0(r));
        return -EINVAL;
    }

    if (!endpHandle.ko->IsType(MessageEndpoint::TypeID())) {
        Log::Warning("SysEndpointDequeue: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(uint64_t), currentProcess->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, {
            Log::Warning("(%s): SysEndpointDequeue: Invalid ID pointer %x", currentProcess->name, SC_ARG3(r));
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
        });
        return -EFAULT;
    }

    if (!Memory::CheckUsermodePointer(SC_ARG2(r), sizeof(uint16_t), currentProcess->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, {
            Log::Warning("(%s): SysEndpointDequeue: Invalid size pointer %x", currentProcess->name, SC_ARG3(r));
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
        });
        return -EFAULT;
    }

    MessageEndpoint* endpoint = reinterpret_cast<MessageEndpoint*>(endpHandle.ko.get());

    if (!Memory::CheckUsermodePointer(SC_ARG3(r), endpoint->GetMaxMessageSize(), currentProcess->addressSpace)) {
        IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal, {
            Log::Warning("(%s): SysEndpointDequeue: Invalid data buffer %x", currentProcess->name, SC_ARG3(r));
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, Scheduler::GetCurrentProcess()->addressSpace);
        });
        return -EFAULT;
    }

    return endpoint->Read(reinterpret_cast<uint64_t*>(SC_ARG1(r)), reinterpret_cast<uint16_t*>(SC_ARG2(r)),
                          reinterpret_cast<uint8_t*>(SC_ARG3(r)));
}

/////////////////////////////
/// \brief SysEndpointCall (endpoint, id, data, rID, rData, size, timeout)
///
/// Accept a pending connection on an interface and return a new MessageEndpoint
///
/// \param endpoint (handle_id_t) Handle ID of specified endpoint
/// \param id (uint64_t) id of message to send
/// \param data (uint8_t*) Message data to be sent
/// \param rID (uint64_t) id of expected returned message
/// \param rData (uint8_t*) Return message data buffer
/// \param size (uint32_t*) Pointer containing size of message to be sent and size of returned message
/// \param timeout (int64_t) timeout in us
///
/// \return 0 on success, negative error code on failure
/////////////////////////////
long SysEndpointCall(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle endpHandle;
    if (!(endpHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("(%s): SysEndpointCall: Invalid handle ID %d", currentProcess->name, SC_ARG0(r));
        return -EINVAL;
    }

    if (!endpHandle.ko->IsType(MessageEndpoint::TypeID())) {
        Log::Warning("SysEndpointCall: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    uint16_t* size = reinterpret_cast<uint16_t*>(SC_ARG5(r));
    if (*size && !Memory::CheckUsermodePointer(SC_ARG2(r), *size, currentProcess->addressSpace)) {
        return -EFAULT; // Invalid data pointer
    }

    MessageEndpoint* endpoint = reinterpret_cast<MessageEndpoint*>(endpHandle.ko.get());

    return endpoint->Call(SC_ARG1(r), *size, SC_ARG2(r), SC_ARG3(r), size, reinterpret_cast<uint8_t*>(SC_ARG4(r)), -1);
}

/////////////////////////////
/// \brief SysEndpointInfo (endpoint, info)
///
/// Get endpoint information
///
/// \param endpoint Endpoint handle
/// \param info Pointer to endpoint info struct
///
/// \return 0 on success, negative error code on failure
/////////////////////////////
long SysEndpointInfo(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    MessageEndpointInfo* info = reinterpret_cast<MessageEndpointInfo*>(SC_ARG1(r));

    if (!Memory::CheckUsermodePointer(SC_ARG1(r), sizeof(MessageEndpointInfo), currentProcess->addressSpace)) {
        return -EFAULT; // Data greater than 8 and invalid pointer
    }

    Handle endpHandle;
    if (!(endpHandle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("(%s): SysEndpointInfo: Invalid handle ID %d", currentProcess->name, SC_ARG0(r));
        return -EINVAL;
    }

    if (!endpHandle.ko->IsType(MessageEndpoint::TypeID())) {
        Log::Warning("SysEndpointInfo: Invalid handle type (ID %d)", SC_ARG0(r));
        return -EINVAL;
    }

    MessageEndpoint* endpoint = reinterpret_cast<MessageEndpoint*>(endpHandle.ko.get());

    *info = {.msgSize = endpoint->GetMaxMessageSize()};
    return 0;
}

/////////////////////////////
/// \brief SysKernelObjectWaitOne (object)
///
/// Wait on one KernelObject
///
/// \param object (handle_t) Object to wait on
/// \param timeout (long) Timeout in microseconds
///
/// \return negative error code on failure
/////////////////////////////
long SysKernelObjectWaitOne(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle handle;
    if (!(handle = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("SysKernelObjectWaitOne: Invalid handle ID %d", SC_ARG0(r));
        return -EINVAL;
    }

    long timeout = SC_ARG1(r);

    KernelObjectWatcher watcher;

    watcher.WatchObject(handle.ko, 0);

    if (timeout > 0) {
        if (watcher.WaitTimeout(timeout)) {
            return -EINTR;
        }
    } else {
        if (watcher.Wait()) {
            return -EINTR;
        }
    }

    return 0;
}

/////////////////////////////
/// \brief SysKernelObjectWait (objects, count)
///
/// Wait on one KernelObject
///
/// \param objects (handle_t*) Pointer to array of handles to wait on
/// \param count (size_t) Amount of objects to wait on
/// \param timeout (long) Timeout in microseconds
///
/// \return negative error code on failure
/////////////////////////////
long SysKernelObjectWait(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();
    unsigned count = SC_ARG1(r);
    long timeout = SC_ARG2(r);

    if (!Memory::CheckUsermodePointer(SC_ARG0(r), count * sizeof(handle_id_t), currentProcess->addressSpace)) {
        return -EFAULT;
    }

    Handle handles[count];
    UserBuffer<handle_id_t> handleIDs(SC_ARG0(r));

    KernelObjectWatcher watcher;
    for (unsigned i = 0; i < count; i++) {
        handle_id_t id;
        if (handleIDs.GetValue(i, id)) {
            return -EFAULT;
        }

        if (!(handles[i] = currentProcess->GetHandle(id))) {
            IF_DEBUG(debugLevelSyscalls >= DebugLevelNormal,
                     { Log::Warning("SysKernelObjectWait: Invalid handle ID %d", SC_ARG0(r)); });
            return -EINVAL;
        }

        watcher.WatchObject(handles[i].ko, 0);
    }

    if (timeout > 0) {
        if (watcher.WaitTimeout(timeout)) {
            return -EINTR;
        }
    } else {
        if (watcher.Wait()) {
            return -EINTR;
        }
    }

    return 0;
}

/////////////////////////////
/// \brief SysKernelObjectDestroy (object)
///
/// Destroy a KernelObject
///
/// \param object (handle_t) Object to destroy
///
/// \return negative error code on failure
/////////////////////////////
long SysKernelObjectDestroy(RegisterContext* r) {
    Process* currentProcess = Scheduler::GetCurrentProcess();

    Handle h;
    if (!(h = currentProcess->GetHandle(SC_ARG0(r)))) {
        Log::Warning("SysKernelObjectDestroy: Invalid handle ID %d", SC_ARG0(r));

        IF_DEBUG(debugLevelSyscalls >= DebugLevelVerbose, {
            Log::Info("Process %s (PID: %d), Stack Trace:", currentProcess->name, currentProcess->PID());
            Log::Info("%x", r->rip);
            UserPrintStackTrace(r->rbp, currentProcess->addressSpace);
        });
        return -EBADF;
    }

    Log::Warning("SysKernelObjectDestroy: destrying ko %d (type %d)", SC_ARG0(r), h.ko->InstanceTypeID());

    Log::Info("Process %s (PID: %d), Stack Trace:", currentProcess->name, currentProcess->PID());
    Log::Info("%x", r->rip);
    UserPrintStackTrace(r->rbp, currentProcess->addressSpace);

    currentProcess->DestroyHandle(SC_ARG0(r));
    return 0;
}

/////////////////////////////
/// \brief SysSetSocketOptions(sockfd, level, optname, optval, optlen)
///
/// Set options on sockets
///
/// \param sockfd Socket file desscriptor
/// \param level
/// \param optname
/// \param optval
/// \param optlen
///
/// \return 0 on success negative error code on failure
/////////////////////////////
long SysSetSocketOptions(RegisterContext* r) {
    int fd = SC_ARG0(r);
    int level = SC_ARG1(r);
    int opt = SC_ARG2(r);
    const void* optVal = reinterpret_cast<void*>(SC_ARG3(r));
    socklen_t optLen = SC_ARG4(r);

    Process* currentProcess = Scheduler::GetCurrentProcess();
    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(currentProcess->GetHandleAs<UNIXOpenFile>(fd));
    if (!handle) {
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        return -ENOTSOCK; //  Not a socket
    }

    if (optLen && !Memory::CheckUsermodePointer(SC_ARG3(r), optLen, currentProcess->addressSpace)) {
        return -EFAULT;
    }

    Socket* sock = reinterpret_cast<Socket*>(handle->node);
    return sock->SetSocketOptions(level, opt, optVal, optLen);
}

/////////////////////////////
/// \brief SysGetSocketOptions(sockfd, level, optname, optval, optlen)
///
/// Get options on sockets
///
/// \param sockfd Socket file desscriptor
/// \param level
/// \param optname
/// \param optval
/// \param optlen
///
/// \return 0 on success negative error code on failure
/////////////////////////////
long SysGetSocketOptions(RegisterContext* r) {
    int fd = SC_ARG0(r);
    int level = SC_ARG1(r);
    int opt = SC_ARG2(r);
    void* optVal = reinterpret_cast<void*>(SC_ARG3(r));
    socklen_t* optLen = reinterpret_cast<socklen_t*>(SC_ARG4(r));

    Process* currentProcess = Scheduler::GetCurrentProcess();
    FancyRefPtr<UNIXOpenFile> handle = SC_TRY_OR_ERROR(currentProcess->GetHandleAs<UNIXOpenFile>(fd));
    if (!handle) {
        return -EBADF;
    }

    if ((handle->node->flags & FS_NODE_TYPE) != FS_NODE_SOCKET) {
        return -ENOTSOCK; //  Not a socket
    }

    if (!Memory::CheckUsermodePointer(SC_ARG4(r), sizeof(socklen_t), currentProcess->addressSpace)) {
        return -EFAULT;
    }

    if (*optLen && !Memory::CheckUsermodePointer(SC_ARG3(r), *optLen, currentProcess->addressSpace)) {
        return -EFAULT;
    }

    Socket* sock = reinterpret_cast<Socket*>(handle->node);
    return sock->GetSocketOptions(level, opt, optVal, optLen);
}

long SysDeviceManagement(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    int64_t request = SC_ARG0(r);

    if (request == DeviceManager::RequestDeviceManagerGetRootDeviceCount) {
        return DeviceManager::DeviceCount();
    } else if (request == DeviceManager::RequestDeviceManagerEnumerateRootDevices) {
        int64_t offset = SC_ARG1(r);
        int64_t requestedDeviceCount = SC_ARG2(r);
        int64_t* idList = reinterpret_cast<int64_t*>(SC_ARG3(r));

        if (!Memory::CheckUsermodePointer(SC_ARG3(r), sizeof(int64_t) * requestedDeviceCount, process->addressSpace)) {
            return -EFAULT;
        }

        return DeviceManager::EnumerateDevices(offset, requestedDeviceCount, idList);
    }

    switch (request) {
    case DeviceManager::RequestDeviceResolveFromPath: {
        const char* path = reinterpret_cast<const char*>(SC_ARG1(r));

        size_t pathLen;
        if (strlenSafe(path, pathLen, process->addressSpace)) {
            return -EFAULT;
        }

        Device* dev = DeviceManager::ResolveDevice(path);
        if (!dev) {
            return -ENOENT; // No such device
        }

        return dev->ID();
    }
    case DeviceManager::RequestDeviceGetName: {
        int64_t deviceID = SC_ARG1(r);
        char* name = reinterpret_cast<char*>(SC_ARG2(r));
        size_t nameBufferSize = SC_ARG3(r);

        if (!Memory::CheckUsermodePointer(SC_ARG2(r), nameBufferSize, process->addressSpace)) {
            return -EFAULT;
        }

        Device* dev = DeviceManager::DeviceFromID(deviceID);
        if (!dev) {
            return -ENOENT;
        }

        strncpy(name, dev->DeviceName().c_str(), nameBufferSize);
        return 0;
    }
    case DeviceManager::RequestDeviceGetInstanceName: {
        int64_t deviceID = SC_ARG1(r);
        char* name = reinterpret_cast<char*>(SC_ARG2(r));
        size_t nameBufferSize = SC_ARG3(r);

        if (!Memory::CheckUsermodePointer(SC_ARG2(r), nameBufferSize, process->addressSpace)) {
            return -EFAULT;
        }

        Device* dev = DeviceManager::DeviceFromID(deviceID);
        if (!dev) {
            return -ENOENT;
        }

        strncpy(name, dev->InstanceName().c_str(), nameBufferSize);
        return 0;
    }
    case DeviceManager::RequestDeviceGetPCIInformation:
        return -ENOSYS;
    case DeviceManager::RequestDeviceIOControl:
        return -ENOSYS;
    case DeviceManager::RequestDeviceGetType: {
        long deviceID = SC_ARG1(r);

        if (!Memory::CheckUsermodePointer(SC_ARG2(r), sizeof(long), process->addressSpace)) {
            return -EFAULT;
        }

        Device* dev = DeviceManager::DeviceFromID(deviceID);
        if (!dev) {
            return -ENOENT;
        }

        return dev->Type();
    }
    case DeviceManager::RequestDeviceGetChildCount:
        return -ENOSYS;
    case DeviceManager::RequestDeviceEnumerateChildren:
        return -ENOSYS;
    default:
        return -EINVAL;
    }
}

long SysInterruptThread(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    long tid = SC_ARG0(r);

    auto th = process->GetThreadFromTID(tid);
    if (!th.get()) {
        return -ESRCH; // Thread has already been killed
    }

    if (th->blocker) {
        th->blocker->Interrupt(); // Stop the thread from blocking
        th->Unblock();
    }

    return 0;
}

/////////////////////////////
/// \brief SysLoadKernelModule(path)
///
/// Load a kernel module
///
/// \param path Filepath of module to load

/// \return 0 on success negative error code on failure
/////////////////////////////
long SysLoadKernelModule(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    if (process->euid != 0) {
        return -EPERM; // Must be root
    }

    size_t filePathLength;
    long filePathInvalid = strlenSafe(reinterpret_cast<char*>(SC_ARG0(r)), filePathLength, process->addressSpace);
    if (filePathInvalid) {
        Log::Warning("SysLoadKernelModule: Reached unallocated memory reading file path");
        return -EFAULT;
    }

    char filepath[filePathLength + 1];
    strncpy(filepath, (char*)SC_ARG0(r), filePathLength);

    ModuleLoadStatus status = ModuleManager::LoadModule(filepath);
    if (status.status != ModuleLoadStatus::ModuleOK) {
        return -EINVAL;
    } else {
        return 0;
    }
}

/////////////////////////////
/// \brief SysUnloadKernelModule(name)
///
/// Unload a kernel module
///
/// \param name Name of kernel module to unload

/// \return 0 on success negative error code on failure
/////////////////////////////
long SysUnloadKernelModule(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    if (process->euid != 0) {
        return -EPERM; // Must be root
    }

    size_t nameLength;
    long nameInvalid = strlenSafe(reinterpret_cast<char*>(SC_ARG0(r)), nameLength, process->addressSpace);
    if (nameInvalid) {
        Log::Warning("SysUnloadKernelModule: Reached unallocated memory reading file path");
        return -EFAULT;
    }

    char name[nameLength + 1];
    strncpy(name, (char*)SC_ARG0(r), nameLength);

    return ModuleManager::UnloadModule(name);
}

/////////////////////////////
/// \brief SysFork()
///
///	Clone's a process's address space (with copy-on-write), file descriptors and register state
///
/// \return Child PID to the calling process
/// \return 0 to the newly forked child
/////////////////////////////
long SysFork(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();
    Thread* currentThread = Thread::Current();

    FancyRefPtr<Process> newProcess = process->Fork();
    FancyRefPtr<Thread> thread = newProcess->GetMainThread();
    void* threadKStack = thread->kernelStack; // Save the allocated kernel stack
    void* threadKStackBase = thread->kernelStackBase;

    *thread = *currentThread;
    thread->kernelStack = threadKStack;
    thread->kernelStackBase = threadKStackBase;
    thread->state = ThreadStateRunning;
    thread->parent = newProcess.get();
    thread->registers = *r;

    thread->kernelLock = 0;
    thread->stateLock = 0;

    thread->tid = 1;

    thread->blocker = nullptr;
    thread->blockTimedOut = false;

    thread->registers.rax = 0; // To the child we return 0

    newProcess->Start();
    return newProcess->PID(); // Return PID to parent process
}

/////////////////////////////
/// \brief SysGetGID()
///
/// \return Current process's Group ID
/////////////////////////////
long SysGetGID(RegisterContext* r) { return Scheduler::GetCurrentProcess()->gid; }

/////////////////////////////
/// \brief SysGetEGID()
///
/// \return Current process's effective Group ID
/////////////////////////////
long SysGetEGID(RegisterContext* r) { return Scheduler::GetCurrentProcess()->egid; }

/////////////////////////////
/// \brief SysGetEGID()
///
/// \return PID of parent process, if there is no parent then -1
/////////////////////////////
long SysGetPPID(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();
    if (process->Parent()) {
        return process->Parent()->PID();
    } else {
        return -1;
    }
}

/////////////////////////////
/// \brief SysGetEntropy(buf, length)
///
/// \param buf Buffer to be filled
/// \param length Size of buf, cannot be greater than 256
///
/// \return Negative error code on error, otherwise 0
///
/// \note Length cannot be greater than 256, if so -EIO is reutrned
/////////////////////////////
long SysGetEntropy(RegisterContext* r) {
    size_t length = SC_ARG1(r);
    if (length > 256) {
        return -EIO;
    }

    uint8_t* buffer = reinterpret_cast<uint8_t*>(SC_ARG0(r));
    if (!Memory::CheckUsermodePointer(SC_ARG0(r), length, Scheduler::GetCurrentProcess()->addressSpace)) {
        return -EFAULT; // buffer is invalid
    }

    while (length >= 8) {
        uint64_t value = Hash<uint64_t>(rand() % 65535 * (Timer::UsecondsSinceBoot() % 65535));

        *(reinterpret_cast<uint64_t*>(buffer)) = value;
        buffer += 8;
        length -= 8;
    }

    if (length > 0) {
        uint64_t value = Hash<uint64_t>(rand() % 65535 * (Timer::UsecondsSinceBoot() % 65535));
        memcpy(buffer, &value, length);
    }

    return 0;
}

/////////////////////////////
/// \brief SysSocketPair(int domain, int type, int protocol, int sv[2]);
///
/// \param domain Socket domain (muct be AF_UNIX/AF_LOCAL)
/// \param type Socket type
/// \param protocol Socket protocol
/// \param sv file descriptors for the created socket pair
///
/// \return Negative error code on failure, otherwise 0
/////////////////////////////
long SysSocketPair(RegisterContext* r) {
    Process* process = Scheduler::GetCurrentProcess();

    int domain = SC_ARG0(r);
    int type = SC_ARG1(r);
    int protocol = SC_ARG2(r);
    int* sv = reinterpret_cast<int*>(SC_ARG3(r));

    if (!Memory::CheckUsermodePointer(SC_ARG3(r), sizeof(int) * 2, process->addressSpace)) {
        Log::Warning("SysSocketPair: Invalid fd array!");
        return -EFAULT;
    }

    bool nonBlock = type & SOCK_NONBLOCK;
    type &= ~SOCK_NONBLOCK;

    if (domain != UnixDomain && domain != InternetProtocol) {
        Log::Warning("SysSocketPair: domain %d is not supported", domain);
        return -EAFNOSUPPORT;
    }
    if (type != StreamSocket && type != DatagramSocket) {
        Log::Warning("SysSocketPair: type %d is not supported", type);
        return -EPROTONOSUPPORT;
    }

    LocalSocket* s1 = new LocalSocket(type, protocol);
    LocalSocket* s2 = LocalSocket::CreatePairedSocket(s1);

    UNIXOpenFile* s1Handle = SC_TRY_OR_ERROR(fs::Open(s1));
    UNIXOpenFile* s2Handle = SC_TRY_OR_ERROR(fs::Open(s2));

    s1Handle->mode = nonBlock * O_NONBLOCK;
    s2Handle->mode = s1Handle->mode;

    sv[0] = process->AllocateHandle(s1Handle);
    sv[1] = process->AllocateHandle(s2Handle);

    assert(s1->IsConnected() && s2->IsConnected());
    return 0;
}

/////////////////////////////
/// \brief SysPeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) - get name of peer socket
///
/// Returns the address of the peer connected to the socket \a sockfd in \a addr
/// The \a addrlen argument should indicate the amount of space pointed to by \a addr.
/// On return it will contain the size of the name
/// The address will be truncated if the addr buffer is too small
///
/// \param sockfd File descriptor of socket
/// \param addr Address buffer
/// \param addrlen
///
/// \return Negative error code on failure, otherwise 0
/////////////////////////////
long SysPeername(RegisterContext* r) { return -ENOSYS; }

/////////////////////////////
/// \brief SysPeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) - get name of peer socket
///
/// Returns the address that the socket is bound to
/// The \a addrlen argument should indicate the amount of space pointed to by \a addr.
/// On return it will contain the size of the name
/// The address will be truncated if the addr buffer is too small
///
/// \param sockfd File descriptor of socket
/// \param addr Address buffer
/// \param addrlen
///
/// \return Negative error code on failure, otherwise 0
/////////////////////////////
long SysSockname(RegisterContext* r) { return -ENOSYS; }

long SysSignalAction(RegisterContext* r) {
    int signal = SC_ARG0(r); // Signal number
    switch (signal) {
    // SIGCANCEL is a special case
    // Can be used for pthread cancellation (we dont support this yet)
    case SIGCANCEL:
        Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysSignalAction: SIGCANCEL unsupported, returning ENOSYS");
        return -ENOSYS;
    // Supported and overridable signals
    case SIGINT:
    case SIGWINCH:
    case SIGABRT:
    case SIGALRM:
    case SIGCHLD:
    case SIGPIPE:
    case SIGUSR1:
    case SIGUSR2:
        break;
    // Cannot be overriden
    case SIGKILL:
    case SIGCONT:
    case SIGSTOP:
        Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysSignalAction: Signal %d cannot be overriden or ignored!",
                   signal);
        return -EINVAL; // Invalid signal for sigaction
    // Unsupported signal
    default:
        Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysSignalAction: Unsupported signal %d!", signal);
        return -EINVAL; // Invalid signal for sigaction
    }
    assert(signal < SIGNAL_MAX);

    const struct sigaction* sa = reinterpret_cast<struct sigaction*> SC_ARG1(r); // If non-null, new sigaction to set
    struct sigaction* oldSA = reinterpret_cast<struct sigaction*> SC_ARG2(r);    // If non-null, filled with old sigaction

    Process* proc = Scheduler::GetCurrentProcess();

    if (sa && !Scheduler::CheckUsermodePointer(sa)) {
        Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysSignalAction: Invalid sigaction pointer!");
        return -EFAULT;
    }

    if (oldSA) {
        if (!Scheduler::CheckUsermodePointer(oldSA)) {
            Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysSignalAction: Invalid old sigaction pointer!");
            return -EFAULT;
        }

        const SignalHandler& sigHandler = proc->signalHandlers[signal - 1];
        if (sigHandler.action == SignalHandler::HandlerAction::UsermodeHandler) {
            // if (sigHandler.flags & SignalHandler::FlagSignalInfo) is true, doesn't matter, both types are just
            // pointers in a union
            *oldSA = {
                .__sa_handler = {reinterpret_cast<void (*)(int)>(sigHandler.userHandler)},
                .sa_mask = sigHandler.mask,
                .sa_flags = sigHandler.flags,
                .sa_restorer = nullptr,
            };
        } else if (sigHandler.action == SignalHandler::HandlerAction::Default) {
            *oldSA = {
                .__sa_handler = {SIG_DFL},
                .sa_mask = sigHandler.mask,
                .sa_flags = sigHandler.flags,
                .sa_restorer = nullptr,
            };
        } else if (sigHandler.action == SignalHandler::HandlerAction::Ignore) {
            *oldSA = {
                .__sa_handler = {SIG_IGN},
                .sa_mask = sigHandler.mask,
                .sa_flags = sigHandler.flags,
                .sa_restorer = nullptr,
            };
        } else {
            Log::Error("Invalid signal action!");
            assert(!"Invalid signal action");
        }
    }

    if (!sa) {
        return 0; // sa not specified, nothing left to do
    }

    if (sa->sa_flags & (~SignalHandler::supportedFlags)) {
        Log::Error("SysSignalAction: (sig %d) sa_flags %x not supported!", signal, sa->sa_flags);
        return -EINVAL;
    }

    SignalHandler handler;
    if (sa->sa_handler == SIG_DFL) {
        handler.action = SignalHandler::HandlerAction::Default;
    } else if (sa->sa_handler == SIG_IGN) {
        handler.action = SignalHandler::HandlerAction::Ignore;
    } else {
        handler.action = SignalHandler::HandlerAction::UsermodeHandler;
        if (sa->sa_flags & SignalHandler::FlagSignalInfo) {
            handler.userHandler = (void*)sa->sa_sigaction;
        } else {
            handler.userHandler = (void*)sa->sa_handler;
        }
    }
    handler.mask = sa->sa_mask;

    proc->signalHandlers[signal - 1] = handler;
    return 0;
}

long SysProcMask(RegisterContext* r) {
    Thread* t = Thread::Current();

    (void)t;
    Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysProcMask is a stub!");

    return -ENOSYS;
}

long SysKill(RegisterContext* r) {
    pid_t pid = SC_ARG0(r);
    int signal = SC_ARG1(r);

    FancyRefPtr<Process> victim = Scheduler::FindProcessByPID(pid);
    if (!victim.get()) {
        Log::Debug(debugLevelSyscalls, DebugLevelNormal, "SysKill: Process with PID %d does not exist!", pid);
        return -ESRCH; // Process does not exist
    }

    victim->GetMainThread()->Signal(signal);
    return 0;
}

long SysSignalReturn(RegisterContext* r) {
    Thread* th = Thread::Current();
    uint64_t* threadStack = reinterpret_cast<uint64_t*>(r->rsp);

    threadStack++;                     // Discard signal handler address
    th->signalMask = *(threadStack++); // Get the old signal mask

    asm volatile("fxrstor64 (%0)" ::"r"((uintptr_t)threadStack) : "memory");
    threadStack += 512 / sizeof(uint64_t);

    threadStack++; // Discard padding

    // Do not allow the thread to modify CS or SS
    memcpy(r, threadStack, offsetof(RegisterContext, cs));
    r->rsp = reinterpret_cast<RegisterContext*>(threadStack)->rsp;
    // Only allow the following to be changed:
    // Carry, parity, aux carry, zero, sign, direction, overflow
    r->rflags = reinterpret_cast<RegisterContext*>(threadStack)->rflags & 0xcd5;

    return r->rax; // Ensure we keep the RAX value from before
}

long SysAlarm(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();

    proc->SetAlarm(SC_ARG0(r));

    return 0;
}

long SysGetResourceLimit(RegisterContext* r) {
    Process* proc = Scheduler::GetCurrentProcess();
    int resource = SC_ARG0(r);
    // struct rlimit* = SC_ARG1(r);

    (void)proc;
    (void)resource;

    return -ENOSYS;
}

// clang-format off
syscall_t syscalls[NUM_SYSCALLS]{
    SysDebug,
    SysExit, // 1
    SysExec,
    SysRead,
    SysWrite,
    SysOpen, // 5
    SysCloseHandle,
    SysSleep,
    SysCreate,
    SysLink,
    SysUnlink, // 10
    SysExecve,
    SysChdir,
    SysTime,
    SysMapFB,
    SysGetTID, // 15
    SysChmod,
    SysFStat,
    SysStat,
    SysLSeek,
    SysGetPID, // 20
    SysMount,
    SysMkdir,
    SysRmdir,
    SysRename,
    SysYield, // 25
    SysReadDirNext,
    SysRenameAt,
    SysSendMessage,
    SysReceiveMessage,
    SysUptime, // 30
    SysGetVideoMode,
    SysUName,
    SysReadDir,
    SysSetFsBase,
    SysMmap, // 35
    nullptr,
    SysGetCWD,
    SysWaitPID,
    SysNanoSleep,
    SysPRead, // 40
    SysPWrite,
    SysIoctl,
    SysInfo,
    SysMunmap,
    SysCreateSharedMemory, // 45
    SysMapSharedMemory,
    SysUnmapSharedMemory,
    SysDestroySharedMemory,
    SysSocket,
    SysBind, // 50
    SysListen,
    SysAccept,
    SysConnect,
    nullptr,
    nullptr, // 55
    nullptr,
    nullptr,
    SysGetUID,
    SysSetUID,
    SysPoll, // 60
    SysSendMsg,
    SysRecvMsg,
    SysGetEUID,
    SysSetEUID,
    SysGetProcessInfo, // 65
    SysGetNextProcessInfo,
    SysReadLink,
    SysSpawnThread,
    SysExitThread,
    SysFutexWake, // 70
    SysFutexWait,
    SysDup,
    SysGetFileStatusFlags,
    SysSetFileStatusFlags,
    SysSelect, // 75
    SysCreateService,
    SysCreateInterface,
    SysInterfaceAccept,
    SysInterfaceConnect,
    SysEndpointQueue, // 80
    SysEndpointDequeue,
    SysEndpointCall,
    SysEndpointInfo,
    SysKernelObjectWaitOne,
    SysKernelObjectWait, // 85
    SysKernelObjectDestroy,
    SysSetSocketOptions,
    SysGetSocketOptions,
    SysDeviceManagement,
    SysInterruptThread, // 90
    SysLoadKernelModule,
    SysUnloadKernelModule,
    SysFork,
    SysGetGID,
    SysGetEGID, // 95
    SysGetPPID,
    SysPipe,
    SysGetEntropy,
    SysSocketPair,
    SysPeername, // 100
    SysSockname,
    SysSignalAction,
    SysProcMask,
    SysKill,
    SysSignalReturn, // 105
    SysAlarm,
    SysGetResourceLimit,
    SysEpollCreate,
    SysEPollCtl,
    SysEpollWait, // 110
    SysFChdir
};
// clang-format on

void DumpLastSyscall(Thread* t) {
    RegisterContext& lastSyscall = t->lastSyscall.regs;
    Log::Info("Last syscall:\nCall: %d, arg0: %i (%x), arg1: %i (%x), arg2: %i (%x), arg3: %i (%x), arg4: %i (%x), "
              "arg5: %i (%x) result: %i (%x)",
              lastSyscall.rax, SC_ARG0(&lastSyscall), SC_ARG0(&lastSyscall), SC_ARG1(&lastSyscall),
              SC_ARG1(&lastSyscall), SC_ARG2(&lastSyscall), SC_ARG2(&lastSyscall), SC_ARG3(&lastSyscall),
              SC_ARG3(&lastSyscall), SC_ARG4(&lastSyscall), SC_ARG4(&lastSyscall), SC_ARG5(&lastSyscall),
              SC_ARG5(&lastSyscall), t->lastSyscall.result, t->lastSyscall.result);
}

#define MSR_STAR 0xC000'0081
#define MSR_LSTAR 0xC000'0082
#define MSR_CSTAR 0xC000'0083
#define MSR_SFMASK 0xC000'0084

extern "C" void syscall_entry();

void syscall_init() {
    uint64_t low = ((uint64_t)syscall_entry) & 0xFFFFFFFF;
    uint64_t high = ((uint64_t)syscall_entry) >> 32;
    asm volatile("wrmsr" ::"a"(low), "d"(high), "c"(MSR_LSTAR));

    // User CS selector set to this field + 16, SS this field + 8 
    uint32_t sysretSelectors = USER_SS - 8;
    // When syscall is called, CS set to field, SS this field + 8
    uint32_t syscallSelectors = KERNEL_CS;
    asm volatile("wrmsr" ::"a"(0), "d"((sysretSelectors << 16) | syscallSelectors), "c"(MSR_STAR));
    // SFMASK masks flag values
    // mask everything except reserved flags to disable IRQs when syscall is called
    asm volatile("wrmsr" ::"a"(0x3F7FD5U), "d"(0), "c"(MSR_SFMASK));

    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080));
    low |= 1; // SCE (syscall enable)
    asm volatile("wrmsr" :: "a"(low), "d"(high), "c"(0xC0000080));
}

extern "C" void SyscallHandler(RegisterContext* regs) {
    assert(!CheckInterrupts());

    if (__builtin_expect(regs->rax >= NUM_SYSCALLS || !syscalls[regs->rax],
                         0)) { // If syscall is non-existant then return
        regs->rax = -ENOSYS;
        return;
    }

    Thread* thread = Thread::Current();
    if (__builtin_expect(acquireTestLock(&thread->kernelLock), 0)) {
        for (;;)
            Scheduler::Yield();
    }

    asm("sti"); // By reenabling interrupts, a thread in a syscall can be preempted

    if (__builtin_expect(thread->state == ThreadStateZombie, 0)) {
        releaseLock(&thread->kernelLock);

        for (;;)
            Scheduler::Yield();
    }

#ifdef KERNEL_DEBUG
    if (debugLevelSyscalls >= DebugLevelNormal) {
        thread->lastSyscall.regs = *regs;
    }
#endif

    regs->rax = syscalls[regs->rax](regs); // Call syscall

#ifdef KERNEL_DEBUG
    if (debugLevelSyscalls >= DebugLevelNormal) {
        thread->lastSyscall.result = regs->rax;
    }
#endif

    IF_DEBUG((debugLevelSyscalls >= DebugLevelVerbose), {
        if (regs->rax < 0)
            Log::Info("Syscall %d exiting with value %d", thread->lastSyscall.regs.rax, regs->rax);
    });

    if (__builtin_expect(thread->pendingSignals & (~thread->EffectiveSignalMask()), 0)) {
        Log::Info("handing singals: %x", thread->pendingSignals & (~thread->EffectiveSignalMask()));
        thread->HandlePendingSignal(regs);
    }
    releaseLock(&thread->kernelLock);

    while (thread->state == ThreadStateDying) {
        Scheduler::Yield();
    }
}
