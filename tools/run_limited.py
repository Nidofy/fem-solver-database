"""Run a worker under an OS memory envelope, including uninstrumented heaps.

Windows: Job Object aggregate committed-memory limit, assigned before main runs.
Linux: inherited per-process RLIMIT_AS (address space, not aggregate/RSS).
These modes are explicit in the JSON report; neither is mislabeled as an RSS cap.
Use a dedicated worker, never lower the interactive host's process limit.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import time


def windows(command, limit, timeout, log):
    import ctypes as C
    from ctypes import wintypes as W
    import msvcrt
    if not 0<limit<=ctypes.c_size_t(-1).value: raise ValueError("limit exceeds size_t")
    kernel = C.WinDLL("kernel32", use_last_error=True)
    class Basic(C.Structure):
        _fields_ = [("process_time",C.c_longlong),("job_time",C.c_longlong),("flags",W.DWORD),
                    ("min_ws",C.c_size_t),("max_ws",C.c_size_t),("active",W.DWORD),
                    ("affinity",C.c_size_t),("priority",W.DWORD),("scheduling",W.DWORD)]
    class IO(C.Structure):
        _fields_ = [(name,C.c_ulonglong) for name in ("read_ops","write_ops","other_ops","read_bytes","write_bytes","other_bytes")]
    class Extended(C.Structure):
        _fields_ = [("basic",Basic),("io",IO),("process_limit",C.c_size_t),("job_limit",C.c_size_t),
                    ("peak_process",C.c_size_t),("peak_job",C.c_size_t)]
    class Startup(C.Structure):
        _fields_ = [("cb",W.DWORD),("reserved",W.LPWSTR),("desktop",W.LPWSTR),("title",W.LPWSTR),
                    *[(n,W.DWORD) for n in ("x","y","width","height","xchars","ychars","fill","flags")],
                    ("show",W.WORD),("reserved_count",W.WORD),("reserved_bytes",C.POINTER(C.c_byte)),
                    ("stdin",W.HANDLE),("stdout",W.HANDLE),("stderr",W.HANDLE)]
    class Process(C.Structure):
        _fields_ = [("process",W.HANDLE),("thread",W.HANDLE),("pid",W.DWORD),("tid",W.DWORD)]
    signatures = {
        "CreateJobObjectW": ([C.c_void_p,W.LPCWSTR],W.HANDLE),
        "SetInformationJobObject": ([W.HANDLE,C.c_int,C.c_void_p,W.DWORD],W.BOOL),
        "QueryInformationJobObject": ([W.HANDLE,C.c_int,C.c_void_p,W.DWORD,C.c_void_p],W.BOOL),
        "CreateProcessW": ([W.LPCWSTR,W.LPWSTR,C.c_void_p,C.c_void_p,W.BOOL,W.DWORD,C.c_void_p,W.LPCWSTR,C.POINTER(Startup),C.POINTER(Process)],W.BOOL),
        "AssignProcessToJobObject": ([W.HANDLE,W.HANDLE],W.BOOL),
        "ResumeThread": ([W.HANDLE],W.DWORD), "WaitForSingleObject": ([W.HANDLE,W.DWORD],W.DWORD),
        "GetExitCodeProcess": ([W.HANDLE,C.POINTER(W.DWORD)],W.BOOL),
        "TerminateProcess": ([W.HANDLE,W.UINT],W.BOOL), "TerminateJobObject": ([W.HANDLE,W.UINT],W.BOOL),
        "CloseHandle": ([W.HANDLE],W.BOOL)}
    for name,(args,result) in signatures.items():
        fn=getattr(kernel,name);fn.argtypes=args;fn.restype=result
    def checked(value):
        if not value: raise C.WinError(C.get_last_error())
        return value
    job=checked(kernel.CreateJobObjectW(None,None)); process=Process()
    try:
        policy=Extended();policy.basic.flags=0x2000|0x200;policy.job_limit=limit
        checked(kernel.SetInformationJobObject(job,9,C.byref(policy),C.sizeof(policy)))
        with log.open("xb") as output:
            handle=msvcrt.get_osfhandle(output.fileno());os.set_handle_inheritable(handle,True)
            startup=Startup();startup.cb=C.sizeof(startup);startup.flags=0x100;startup.stdout=handle;startup.stderr=handle
            text=C.create_unicode_buffer(subprocess.list2cmdline(command))
            try:
                checked(kernel.CreateProcessW(None,text,None,None,True,0x4|0x08000000,None,None,C.byref(startup),C.byref(process)))
            finally:
                os.set_handle_inheritable(handle,False)
            checked(kernel.AssignProcessToJobObject(job,process.process))
            if kernel.ResumeThread(process.thread)==0xffffffff: raise C.WinError(C.get_last_error())
            wait=kernel.WaitForSingleObject(process.process,int(timeout*1000))
            timed_out=wait==258
            if timed_out:
                checked(kernel.TerminateJobObject(job,124));kernel.WaitForSingleObject(process.process,10000)
            elif wait!=0: raise C.WinError(C.get_last_error())
            code=W.DWORD();checked(kernel.GetExitCodeProcess(process.process,C.byref(code)))
            usage=Extended();checked(kernel.QueryInformationJobObject(job,9,C.byref(usage),C.sizeof(usage),None))
            return {"mode":"windows_job_committed_memory","limit_bytes":limit,"peak_job_committed_bytes":usage.peak_job,
                    "returncode":code.value,"timed_out":timed_out}
    except BaseException:
        if process.process: kernel.TerminateProcess(process.process,125)
        raise
    finally:
        if process.thread: kernel.CloseHandle(process.thread)
        if process.process: kernel.CloseHandle(process.process)
        kernel.CloseHandle(job)


def linux(command, limit, timeout, log):
    import resource
    import signal
    def setup(): resource.setrlimit(resource.RLIMIT_AS,(limit,limit))
    with log.open("xb") as output:
        proc=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT,preexec_fn=setup,start_new_session=True)
        timed_out=False
        try: code=proc.wait(timeout)
        except subprocess.TimeoutExpired:
            timed_out=True;os.killpg(proc.pid,signal.SIGKILL);code=proc.wait()
        finally:
            # End the dedicated worker group, including descendants left behind.
            try: os.killpg(proc.pid,signal.SIGKILL)
            except ProcessLookupError: pass
    return {"mode":"linux_per_process_address_space","limit_bytes":limit,"returncode":code,"timed_out":timed_out,
            "peak_child_rss_kib":resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}


def main():
    p=argparse.ArgumentParser();p.add_argument("--limit-mib",type=int,required=True);p.add_argument("--timeout",type=float,default=60)
    p.add_argument("--report",type=Path,required=True);p.add_argument("--log",type=Path,required=True);p.add_argument("command",nargs=argparse.REMAINDER)
    a=p.parse_args();command=a.command[1:] if a.command[:1]==["--"] else a.command
    if not command or not 0<a.limit_mib*1024*1024<=min(ctypes.c_size_t(-1).value,(1<<63)-1) or not 0<a.timeout<=86400: p.error("positive bounded limits and command required")
    if a.report.exists() or a.log.exists(): p.error("refusing to replace earlier evidence")
    start=time.monotonic()
    result=(windows if os.name=="nt" else linux)(command,a.limit_mib*1024*1024,a.timeout,a.log)
    result["elapsed_seconds"]=time.monotonic()-start
    with a.report.open("x",encoding="utf-8") as stream: json.dump(result,stream,indent=2)
    print(json.dumps(result));return 0 if result["returncode"]==0 else 1


if __name__=="__main__": raise SystemExit(main())
