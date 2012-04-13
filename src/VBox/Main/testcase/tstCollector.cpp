/* $Id: tstCollector.cpp $ */

/** @file
 *
 * Collector classes test cases.
 */

/*
 * Copyright (C) 2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef RT_OS_DARWIN
# include "../src-server/darwin/PerformanceDarwin.cpp"
#endif
#ifdef RT_OS_FREEBSD
# include "../src-server/freebsd/PerformanceFreeBSD.cpp"
#endif
#ifdef RT_OS_LINUX
# include "../src-server/linux/PerformanceLinux.cpp"
#endif
#ifdef RT_OS_OS2
# include "../src-server/os2/PerformanceOS2.cpp"
#endif
#ifdef RT_OS_SOLARIS
# include "../src-server/solaris/PerformanceSolaris.cpp"
#endif
#ifdef RT_OS_WINDOWS
# define _WIN32_DCOM
# include <objidl.h>
# include <objbase.h>
# include "../src-server/win/PerformanceWin.cpp"
#endif

#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#define RUN_TIME_MS        1000

#define N_CALLS(n, fn) \
    for (int call = 0; call < n; ++call) \
        rc = collector->fn; \
    if (RT_FAILURE(rc)) \
        RTPrintf("tstCollector: "#fn" -> %Rrc\n", rc)

#define CALLS_PER_SECOND(fn) \
    nCalls = 0; \
    start = RTTimeMilliTS(); \
    do { \
        rc = collector->fn; \
        if (RT_FAILURE(rc)) \
            break; \
        ++nCalls; \
    } while(RTTimeMilliTS() - start < RUN_TIME_MS); \
    if (RT_FAILURE(rc)) \
    { \
        RTPrintf("tstCollector: "#fn" -> %Rrc\n", rc); \
    } \
    else \
        RTPrintf("%70s -- %u calls per second\n", #fn, nCalls)

void measurePerformance(pm::CollectorHAL *collector, const char *pszName, int cVMs)
{

    static const char * const args[] = { pszName, "-child", NULL };
    pm::CollectorHints hints;
    std::vector<RTPROCESS> processes;

    hints.collectHostCpuLoad();
    hints.collectHostRamUsage();
    /* Start fake VMs */
    for (int i = 0; i < cVMs; ++i)
    {
        RTPROCESS pid;
        int rc = RTProcCreate(pszName, args, RTENV_DEFAULT, 0, &pid);
        if (RT_FAILURE(rc))
        {
            hints.getProcesses(processes);
            std::for_each(processes.begin(), processes.end(), std::ptr_fun(RTProcTerminate));
            RTPrintf("tstCollector: RTProcCreate() -> %Rrc\n", rc);
            return;
        }
        hints.collectProcessCpuLoad(pid);
        hints.collectProcessRamUsage(pid);
    }

    hints.getProcesses(processes);
    RTThreadSleep(30000); // Let children settle for half a minute

    int rc;
    ULONG tmp;
    uint64_t tmp64;
    uint64_t start;
    unsigned int nCalls;
    /* Pre-collect */
    CALLS_PER_SECOND(preCollect(hints, 0));
    /* Host CPU load */
    CALLS_PER_SECOND(getRawHostCpuLoad(&tmp64, &tmp64, &tmp64));
    /* Process CPU load */
    CALLS_PER_SECOND(getRawProcessCpuLoad(processes[nCalls%cVMs], &tmp64, &tmp64, &tmp64));
    /* Host CPU speed */
    CALLS_PER_SECOND(getHostCpuMHz(&tmp));
    /* Host RAM usage */
    CALLS_PER_SECOND(getHostMemoryUsage(&tmp, &tmp, &tmp));
    /* Process RAM usage */
    CALLS_PER_SECOND(getProcessMemoryUsage(processes[nCalls%cVMs], &tmp));

    start = RTTimeNanoTS();

    int times;
    for (times = 0; times < 100; times++)
    {
        /* Pre-collect */
        N_CALLS(1, preCollect(hints, 0));
        /* Host CPU load */
        N_CALLS(1, getRawHostCpuLoad(&tmp64, &tmp64, &tmp64));
        /* Host CPU speed */
        N_CALLS(1, getHostCpuMHz(&tmp));
        /* Host RAM usage */
        N_CALLS(1, getHostMemoryUsage(&tmp, &tmp, &tmp));
        /* Process CPU load */
        N_CALLS(cVMs, getRawProcessCpuLoad(processes[call], &tmp64, &tmp64, &tmp64));
        /* Process RAM usage */
        N_CALLS(cVMs, getProcessMemoryUsage(processes[call], &tmp));
    }
    printf("\n%u VMs -- %.2f%% of CPU time\n", cVMs, (RTTimeNanoTS() - start) / 10000000. / times);

    /* Shut down fake VMs */
    std::for_each(processes.begin(), processes.end(), std::ptr_fun(RTProcTerminate));
}

int main(int argc, char *argv[])
{
    /*
     * Initialize the VBox runtime without loading
     * the support driver.
     */
    int rc = RTR3Init();
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: RTR3Init() -> %d\n", rc);
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "-child"))
    {
        /* We have spawned ourselves as a child process -- scratch the leg */
        RTThreadSleep(1000000);
        return 1;
    }
#ifdef RT_OS_WINDOWS
    HRESULT hRes = CoInitialize(NULL);
    /*
     * Need to initialize security to access performance enumerators.
     */
    hRes = CoInitializeSecurity(
        NULL,
        -1,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_NONE,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, 0);
#endif

    pm::CollectorHAL *collector = pm::createHAL();
    if (!collector)
    {
        RTPrintf("tstCollector: createMetricFactory() failed\n", rc);
        return 1;
    }
#if 1
    pm::CollectorHints hints;
    hints.collectHostCpuLoad();
    hints.collectHostRamUsage();
    hints.collectProcessCpuLoad(RTProcSelf());
    hints.collectProcessRamUsage(RTProcSelf());

    uint64_t start;

    uint64_t hostUserStart, hostKernelStart, hostIdleStart;
    uint64_t hostUserStop, hostKernelStop, hostIdleStop, hostTotal;

    uint64_t processUserStart, processKernelStart, processTotalStart;
    uint64_t processUserStop, processKernelStop, processTotalStop;

    RTPrintf("tstCollector: TESTING - CPU load, sleeping for 5 sec\n");

    rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawHostCpuLoad(&hostUserStart, &hostKernelStart, &hostIdleStart);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStart, &processKernelStart, &processTotalStart);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
        return 1;
    }

    RTThreadSleep(5000); // Sleep for 5 seconds

    rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawHostCpuLoad(&hostUserStop, &hostKernelStop, &hostIdleStop);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStop, &processKernelStop, &processTotalStop);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    hostTotal = hostUserStop - hostUserStart
        + hostKernelStop - hostKernelStart
        + hostIdleStop - hostIdleStart;
    /*printf("tstCollector: host cpu user      = %f sec\n", (hostUserStop - hostUserStart) / 10000000.);
    printf("tstCollector: host cpu kernel    = %f sec\n", (hostKernelStop - hostKernelStart) / 10000000.);
    printf("tstCollector: host cpu idle      = %f sec\n", (hostIdleStop - hostIdleStart) / 10000000.);
    printf("tstCollector: host cpu total     = %f sec\n", hostTotal / 10000000.);*/
    RTPrintf("tstCollector: host cpu user      = %llu %%\n", (hostUserStop - hostUserStart) * 100 / hostTotal);
    RTPrintf("tstCollector: host cpu kernel    = %llu %%\n", (hostKernelStop - hostKernelStart) * 100 / hostTotal);
    RTPrintf("tstCollector: host cpu idle      = %llu %%\n", (hostIdleStop - hostIdleStart) * 100 / hostTotal);
    RTPrintf("tstCollector: process cpu user   = %llu %%\n", (processUserStop - processUserStart) * 100 / (processTotalStop - processTotalStart));
    RTPrintf("tstCollector: process cpu kernel = %llu %%\n\n", (processKernelStop - processKernelStart) * 100 / (processTotalStop - processTotalStart));

    RTPrintf("tstCollector: TESTING - CPU load, looping for 5 sec\n");
    rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawHostCpuLoad(&hostUserStart, &hostKernelStart, &hostIdleStart);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStart, &processKernelStart, &processTotalStart);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    start = RTTimeMilliTS();
    while(RTTimeMilliTS() - start < 5000)
        ; // Loop for 5 seconds
    rc = collector->preCollect(hints, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: preCollect() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawHostCpuLoad(&hostUserStop, &hostKernelStop, &hostIdleStop);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawHostCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getRawProcessCpuLoad(RTProcSelf(), &processUserStop, &processKernelStop, &processTotalStop);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getRawProcessCpuLoad() -> %Rrc\n", rc);
        return 1;
    }
    hostTotal = hostUserStop - hostUserStart
        + hostKernelStop - hostKernelStart
        + hostIdleStop - hostIdleStart;
    RTPrintf("tstCollector: host cpu user      = %llu %%\n", (hostUserStop - hostUserStart) * 100 / hostTotal);
    RTPrintf("tstCollector: host cpu kernel    = %llu %%\n", (hostKernelStop - hostKernelStart) * 100 / hostTotal);
    RTPrintf("tstCollector: host cpu idle      = %llu %%\n", (hostIdleStop - hostIdleStart) * 100 / hostTotal);
    RTPrintf("tstCollector: process cpu user   = %llu %%\n", (processUserStop - processUserStart) * 100 / (processTotalStop - processTotalStart));
    RTPrintf("tstCollector: process cpu kernel = %llu %%\n\n", (processKernelStop - processKernelStart) * 100 / (processTotalStop - processTotalStart));

    RTPrintf("tstCollector: TESTING - Memory usage\n");

    ULONG total, used, available, processUsed;

    rc = collector->getHostMemoryUsage(&total, &used, &available);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getHostMemoryUsage() -> %Rrc\n", rc);
        return 1;
    }
    rc = collector->getProcessMemoryUsage(RTProcSelf(), &processUsed);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstCollector: getProcessMemoryUsage() -> %Rrc\n", rc);
        return 1;
    }
    RTPrintf("tstCollector: host mem total     = %lu kB\n", total);
    RTPrintf("tstCollector: host mem used      = %lu kB\n", used);
    RTPrintf("tstCollector: host mem available = %lu kB\n", available);
    RTPrintf("tstCollector: process mem used   = %lu kB\n", processUsed);
#endif
    RTPrintf("\ntstCollector: TESTING - Performance\n\n");

    measurePerformance(collector, argv[0], 100);

    delete collector;

    printf ("\ntstCollector FINISHED.\n");

    return rc;
}

