#
# Configure options
#

option (DISABLE_PORTABILITY "Disables the IO portability layer")
option (DISABLE_AOT "Disable AOT Compiler")
option (DISABLE_PROFILER "Disable default profiler support")
option (DISABLE_DECIMAL "Disable System.Decimal support")
option (DISABLE_PINVOKE "Disable P/Invoke support")
option (DISABLE_DEBUG "Disable runtime debugging support")
option (DISABLE_REFLECTION_EMIT "Disable reflection emit support")
option (DISABLE_LARGE_CODE "Disable support for huge assemblies")
option (DISABLE_LOGGING "Disable support debug logging")
option (DISABLE_COM "Disable COM support")
option (DISABLE_SSA "Disable advanced SSA JIT optimizations")
option (DISABLE_GENERICS "Disable generics support")
option (DISABLE_JIT "Disable the JIT, only full-aot mode or interpreter will be supported by the runtime.")
option (DISABLE_INTERPRETER "Disable the interpreter.")
option (DISABLE_SIMD "Disable SIMD intrinsics related optimizations.")
option (DISABLE_DEBUGGER_AGENT "Disable Soft Debugger Agent.")
option (DISABLE_PERFCOUNTERS "Disable Performance Counters.")
option (DISABLE_SHARED_PERFCOUNTERS "Disable shared perfcounters.")
option (DISABLE_LLDB "Disable support code for the LLDB plugin.")
option (DISABLE_MDB "Disable support for .mdb symbol files.")
option (DISABLE_ASSERT_MESSAGES "Disable assertion messages.")
option (DISABLE_SGEN_MAJOR_MARKSWEEP_CONC "Disable concurrent gc support in SGEN.")
option (DISABLE_SGEN_SPLIT_NURSERY "Disable minor=split support in SGEN.")
option (DISABLE_SGEN_GC_BRIDGE "Disable gc bridge support in SGEN.")
option (DISABLE_SGEN_DEBUG_HELPERS "Disable debug helpers in SGEN.")
option (DISABLE_SOCKETS "Disable sockets")
option (DISABLE_GAC "Disable GAC")
option (DISABLE_DLLMAP "Disables use of DllMaps in MonoVM")
option (DISABLE_THREADS "Disable Threads")
option (DISABLE_SGEN_TOGGLEREF "Disable toggleref support in SGEN")
option (DISABLE_SGEN_BINARY_PROTOCOL "Disable binary protocol logging in SGEN")
option (DISABLE_PROCESSES "Disable process support")
option (DISABLE_EVENTPIPE "Disable EventPipe support")
option (DISABLE_EXECUTABLES "Disable the build of the runtime executables")
option (DISABLE_CRASH_REPORTING "Disable crash reporting subsystem")
option (DISABLE_ICALL_TABLES "Enable separate icall table library")
option (DISABLE_QCALLS "Disable support for QCalls")
option (ENABLE_ICALL_EXPORT "Export icall functions")
option (ENABLE_ICALL_SYMBOL_MAP "Generate tables which map icall functions to their C symbols")
option (ENABLE_PERFTRACING "Enables support for eventpipe library")
option (ENABLE_INTERP_LIB "Enable separate interpreter library")
option (ENABLE_LAZY_GC_THREAD_CREATION "Enable lazy runtime thread creation, embedding host must do it explicitly")
option (ENABLE_WERROR "Compile with -Werror")
option (ENABLE_LLVM_RUNTIME "Enable runtime support code for LLVM.")
option (ENABLE_CHECKED_BUILD "Enable additional checks")
option (ENABLE_CHECKED_BUILD_PRIVATE_TYPES "Enable compile time checking that getter functions are used")
option (ENABLE_CHECKED_BUILD_GC "Enable runtime GC Safe / Unsafe mode assertion checks (must set env var MONO_CHECK_MODE=gc)")
option (ENABLE_CHECKED_BUILD_THREAD "Enable runtime history of per-thread coop state transitions (must set env var MONO_CHECK_MODE=thread)")
option (ENABLE_CHECKED_BUILD_METADATA "Enable runtime checks of mempool references between metadata images (must set env var MONO_CHECK_MODE=metadata)")
option (ENABLE_METADATA_UPDATE "Enable runtime support for metadata updates")

set (GC_SUSPEND "default" CACHE STRING "GC suspend method (default, preemptive, coop, hybrid)")
set (CHECKED_BUILD "" CACHE STRING "Set ENABLE_CHECKED_BUILD_ options at once.  Comma-separated list of lowercase ENABLE_CHECKED_BUILD_ options ie. 'gc,threads,private_types' etc.")
set (ENABLE_MINIMAL "" CACHE STRING "Set many DISABLE_ options at once. Comma-separated list of lowercase DISABLE_ options ie. 'jit,simd' etc.")
set (AOT_TARGET_TRIPLE "" CACHE STRING "Target triple for AOT cross compiler")
set (AOT_OFFSETS_FILE "" CACHE STRING "Offsets file for AOT cross compiler")
set (LLVM_PREFIX "" CACHE STRING "Enable LLVM support with LLVM installed at <LLVM_PREFIX>.")
