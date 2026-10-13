/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
pipeline.cpp -- verify, CIR-to-CIR passes, lowering to LLVM IR, GlobalDCE
(always, also at -O0), optimization and code generation.  Everything here
runs on one worker thread with a large stack (CrashRecoveryContext), inside
an MLIR context with multithreading disabled, so deep verifier/pass
recursion has stack to spare.  A fatal LLVM error or a crash is turned into
EDG_CIR_ERROR_INTERNAL.  Diagnostics are queued and delivered by the caller
after the worker has joined.
*/

#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <mutex>
#include <string>
#include <vector>

#include <pthread.h>

#include "clang/CIR/CIRToCIRPasses.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/LowerToLLVM.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"

#include "cir_module.h"
#include "emitter.h"

namespace edg_cir {

namespace {

struct a_fatal_state {
  std::string message;
};  /* a_fatal_state */

void fatal_error_handler(void *user_data, const char *reason, bool)
/*
LLVM fatal error handler (process-wide while installed): remember the
message and unwind to the worker's CrashRecoveryContext.
*/
{
  auto *state = static_cast<a_fatal_state *>(user_data);

  state->message = reason;
  if (llvm::CrashRecoveryContext *context =
                                    llvm::CrashRecoveryContext::GetCurrent())
    context->HandleExit(1);
}  /* fatal_error_handler */

class a_stage_timer {
/*
Adds the elapsed time of its scope to one of the session's stage counters
when statistics are requested (no clock is read otherwise).
*/
public:
  a_stage_timer(an_edg_cir_session &owner, uint64_t a_session_stats::*field,
                uint64_t a_session_stats::*peak_field = nullptr)
    : owner_(owner), field_(field), peak_field_(peak_field),
      start_(owner.stats_on ? stats_now_ns() : 0)
    {}
  ~a_stage_timer()
    {
      if (owner_.stats_on) {
        owner_.stats.*field_ += stats_now_ns() - start_;
        if (peak_field_ != nullptr)
          owner_.stats.*peak_field_ = stats_peak_kib();
      }
    }
private:
  an_edg_cir_session       &owner_;
  uint64_t a_session_stats::*field_;
  uint64_t a_session_stats::*peak_field_;   /* Peak RSS after the stage. */
  uint64_t                  start_;
};  /* a_stage_timer */

void llvm_diagnostic(const llvm::DiagnosticInfo *info, void *user_data)
/*
Queue an LLVM diagnostic raised during lowering or code generation.
*/
{
  auto *session = static_cast<an_edg_cir_session *>(user_data);
  std::string text;
  llvm::raw_string_ostream os(text);
  llvm::DiagnosticPrinterRawOStream printer(os);
  an_edg_cir_severity severity = EDG_CIR_SEVERITY_NOTE;

  /* Size and optimization remarks are not diagnostics. */
  if (info->getSeverity() != llvm::DS_Remark) {
    info->print(printer);
    os.flush();
    switch (info->getSeverity()) {
    case llvm::DS_Error:
      severity = EDG_CIR_SEVERITY_ERROR;
      break;
    case llvm::DS_Warning:
      severity = EDG_CIR_SEVERITY_WARNING;
      break;
    default:
      break;
    }
    session->report(severity, edc_llvm, text);
    if (severity == EDG_CIR_SEVERITY_ERROR && session->status == EDG_CIR_OK) {
      session->status = EDG_CIR_ERROR_PIPELINE;
      session->status_message = text;
    }
  }
}  /* llvm_diagnostic */

std::vector<mlir::StringAttr> guard_member_declarations(
    mlir::ModuleOp module)
/*
The CXX ABI lowering of the pin reads the initial value of a global of a
pointer-to-member type without a null check, so an external declaration of
one (`extern int (Z::*m)();`) crashes it.  Give each such declaration the null
constant for the CIR-to-CIR passes; unguard_member_declarations takes it away
again.  Returns the symbols of the guarded globals.
*/
{
  std::vector<mlir::StringAttr> guarded;

  for (mlir::Operation &op : module.getBody()->getOperations()) {
    auto global = mlir::dyn_cast<cir::GlobalOp>(&op);

    if (global && !global.getInitialValue().has_value() &&
        global.getCtorRegion().empty()) {
      mlir::Type type = global.getSymType();

      if (mlir::isa<cir::DataMemberType>(type)) {
        global.setInitialValueAttr(cir::DataMemberAttr::get(
            mlir::cast<cir::DataMemberType>(type)));
        guarded.push_back(global.getSymNameAttr());
      } else if (mlir::isa<cir::MethodType>(type)) {
        global.setInitialValueAttr(cir::MethodAttr::get(
            mlir::cast<cir::MethodType>(type)));
        guarded.push_back(global.getSymNameAttr());
      }
    }
  }
  return guarded;
}  /* guard_member_declarations */

void unguard_member_declarations(mlir::ModuleOp module,
                                 const std::vector<mlir::StringAttr> &guarded)
/*
Make the globals that guard_member_declarations gave a constant declarations
again.
*/
{
  for (mlir::StringAttr name : guarded) {
    if (auto global = module.lookupSymbol<cir::GlobalOp>(name))
      global->removeAttr("initial_value");
  }
}  /* unguard_member_declarations */

void lower_and_emit(an_edg_cir_session &session, mlir::ModuleOp module,
                    llvm::raw_string_ostream &out)
/*
Lower the (CIR-pass-processed) module to LLVM IR, run GlobalDCE and the
optimizer, and produce the requested LLVM-level output.  Sets
session.status on failure.
*/
{
  a_cir_module &cm = *session.module;
  llvm::LLVMContext llvm_context;
  std::unique_ptr<llvm::Module> lowered;
  bool go = true;

  std::unique_ptr<a_stage_timer> lower_timer(
      new a_stage_timer(session, &a_session_stats::lower_ns,
                        &a_session_stats::rss_lowered_kib));

  llvm_context.setDiagnosticHandlerCallBack(llvm_diagnostic, &session);
  lowered = cir::direct::lowerDirectlyFromCIRToLLVMIR(module, llvm_context,
                                                      /*enableOpenMP=*/false);
  if (!lowered) {
    session.fail(EDG_CIR_ERROR_PIPELINE, edc_pipeline,
                 "lowering CIR to LLVM IR failed");
    go = false;
  }
  if (go) {
    lowered->setTargetTriple(cm.triple);
    lowered->setDataLayout(cm.layout);
    if ((session.flags & EDG_CIR_FLAG_VERIFY_EACH) &&
        llvm::verifyModule(*lowered, &llvm::errs())) {
      session.fail(EDG_CIR_ERROR_VERIFY, edc_pipeline,
                   "the LLVM verifier rejected the lowered module");
      go = false;
    }
  }
  if (go) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    llvm::PassBuilder builder(cm.machine.get());
    llvm::ModulePassManager mpm;

    builder.registerModuleAnalyses(mam);
    builder.registerCGSCCAnalyses(cgam);
    builder.registerFunctionAnalyses(fam);
    builder.registerLoopAnalyses(lam);
    builder.crossRegisterProxies(lam, fam, cgam, mam);
    if (session.opt_level <= 0) {
      /* Dead internal functions of streamed regions must go even at -O0. */
      mpm.addPass(llvm::GlobalDCEPass());
    } else {
      llvm::OptimizationLevel level =
          session.opt_level == 1 ? llvm::OptimizationLevel::O1
          : session.opt_level == 2 ? llvm::OptimizationLevel::O2
                                   : llvm::OptimizationLevel::O3;
      mpm = builder.buildPerModuleDefaultPipeline(level);
    }
    mpm.run(*lowered, mam);
    go = session.status == EDG_CIR_OK;
  }
  lower_timer.reset();
  if (go) {
    a_stage_timer codegen_timer(session, &a_session_stats::codegen_ns);
    switch (session.output_kind) {
    case EDG_CIR_OUTPUT_LLVM_IR:
      lowered->print(out, nullptr);
      out.flush();
      break;
    case EDG_CIR_OUTPUT_LLVM_BC:
      llvm::WriteBitcodeToFile(*lowered, out);
      out.flush();
      break;
    case EDG_CIR_OUTPUT_ASM:
    case EDG_CIR_OUTPUT_OBJECT: {
      llvm::legacy::PassManager codegen;
      llvm::CodeGenFileType kind = session.output_kind == EDG_CIR_OUTPUT_ASM
                                     ? llvm::CodeGenFileType::AssemblyFile
                                     : llvm::CodeGenFileType::ObjectFile;
      llvm::SmallVector<char, 0> buffer;
      llvm::raw_svector_ostream stream(buffer);

      if (cm.machine->addPassesToEmitFile(codegen, stream, nullptr, kind)) {
        session.fail(EDG_CIR_ERROR_PIPELINE, edc_pipeline,
                     "the target cannot emit this output kind");
      } else {
        codegen.run(*lowered);
        session.output.assign(buffer.data(), buffer.size());
      }
      break;
    }
    default:
      break;
    }
  }
}  /* lower_and_emit */

void pipeline_body(an_edg_cir_session &session)
/*
Run the stages requested by the session's output kind (the worker thread's
body): verify, textual CIR, CIR-to-CIR passes, then lower_and_emit.
*/
{
  a_cir_module &cm = *session.module;
  mlir::ModuleOp module = cm.module.get();
  mlir::ScopedDiagnosticHandler diagnostics(
      &cm.context, [&session](mlir::Diagnostic &diagnostic) {
        an_edg_cir_severity severity = EDG_CIR_SEVERITY_NOTE;
        switch (diagnostic.getSeverity()) {
        case mlir::DiagnosticSeverity::Error:
          severity = EDG_CIR_SEVERITY_ERROR;
          break;
        case mlir::DiagnosticSeverity::Warning:
          severity = EDG_CIR_SEVERITY_WARNING;
          break;
        default:
          break;
        }
        std::string text = diagnostic.str();
        /* Debug aid: EDG_CIR_DEBUG_NOTES=1 appends the first line of each
           attached note (the verifier puts the offending operation there). */
        if (std::getenv("EDG_CIR_DEBUG_NOTES") != nullptr) {
          for (mlir::Diagnostic &note : diagnostic.getNotes()) {
            std::string line = note.str();
            line = line.substr(0, line.find('\n'));
            text += "\n  note: " + line.substr(0, 400);
          }
        }
        session.report(severity, edc_mlir, text);
        return mlir::success();
      });
  llvm::raw_string_ostream out(session.output);
  bool go = true;

#if EDG_CIR_TEST_HOOKS
  /* Test hooks: prove that a fatal LLVM error or a crash in the worker
     becomes EDG_CIR_ERROR_INTERNAL (see test/driver). */
  if (const char *hook = std::getenv("EDG_CIR_TEST_CRASH")) {
    if (std::strcmp(hook, "fatal") == 0)
      llvm::report_fatal_error("EDG_CIR_TEST_CRASH=fatal");
    else if (std::strcmp(hook, "usage") == 0)
      llvm::reportFatalUsageError("EDG_CIR_TEST_CRASH=usage");
    else if (std::strcmp(hook, "segv") == 0)
      std::raise(SIGSEGV);
    else if (std::strcmp(hook, "abort") == 0)
      std::abort();
  }
#endif

  if (session.stats_on)
    session.stats.rss_emitted_kib = stats_peak_kib();
  if (session.emitter) {
    a_stage_timer timer(session, &a_session_stats::finish_ns,
                        &a_session_stats::rss_finished_kib);
    if (!session.emitter->finish())
      go = false;
  }
  /* Debug aid: EDG_CIR_NO_VERIFY=1 lets an invalid module be printed. */
  if (go && std::getenv("EDG_CIR_NO_VERIFY") == nullptr) {
    a_stage_timer timer(session, &a_session_stats::verify_ns,
                        &a_session_stats::rss_verified_kib);
    if (mlir::failed(mlir::verify(module))) {
      session.fail(EDG_CIR_ERROR_VERIFY, edc_pipeline,
                   "the MLIR verifier rejected the module");
      go = false;
    }
  }
  if (go && session.output_kind == EDG_CIR_OUTPUT_NONE)
    go = false;
  if (go && session.output_kind == EDG_CIR_OUTPUT_CIR) {
    a_stage_timer timer(session, &a_session_stats::codegen_ns);
    module->print(out);
    out.flush();
    go = false;
  }
  if (go && !(session.flags & EDG_CIR_FLAG_DISABLE_CIR_PASSES)) {
    a_stage_timer timer(session, &a_session_stats::passes_ns,
                        &a_session_stats::rss_passed_kib);
    bool verify_each = (session.flags & EDG_CIR_FLAG_VERIFY_EACH) != 0;
    std::vector<mlir::StringAttr> guarded = guard_member_declarations(module);
    bool passed = mlir::succeeded(cir::runCIRToCIRPasses(
            module, cm.context, verify_each, /*enableIdiomRecognizer=*/false,
            /*enableCIRSimplify=*/session.opt_level > 0,
            /*enableLibOpt=*/false, /*libOptOptions=*/"",
            /*enableCallConvLowering=*/true));

    unguard_member_declarations(module, guarded);
    if (!passed) {
      session.fail(EDG_CIR_ERROR_PIPELINE, edc_pipeline,
                   "the CIR-to-CIR passes failed");
      go = false;
    }
  }
  if (go && session.output_kind == EDG_CIR_OUTPUT_CIR_LOWERED) {
    a_stage_timer timer(session, &a_session_stats::codegen_ns);
    module->print(out);
    out.flush();
    go = false;
  }
  if (go)
    lower_and_emit(session, module, out);
}  /* pipeline_body */

}  /* anonymous namespace */

namespace {

std::mutex &guard_mutex()
/*
The one mutex of every guarded section (crash recovery and the fatal error
handler are process-global).
*/
{
  static std::mutex guard;
  return guard;
}  /* guard_mutex */

unsigned crash_recovery_holders = 0;  /* Protected by guard_mutex(). */

thread_local bool on_guard_worker = false;   /* This thread is the worker. */

class a_guard_worker {
/*
A thread with a big stack that stays alive between guarded sections: the front
end hands the library one frame at a time, and a thread (with its stack
mapping) per frame cost 25 us each, 3% of a 6000-function translation unit.
The body runs under its own crash recovery context on that thread, as
CrashRecoveryContext::RunSafelyOnThread would run it on a fresh one.

Nothing here waits at process exit: the thread is detached and its state is
never destroyed (a static destructor, an exit() from the front end's error
path or from the worker itself must not block on it); the handle's destructor
only tells an idle worker to leave.
*/
public:
  ~a_guard_worker()
  { stop(); }

  void forget()
  /* After fork(): the child has no worker thread, so the next section starts
     one (the old state is leaked). */
  { state_ = nullptr; }

  bool run(const std::function<void()> &body, unsigned stack_mb)
  {
    bool completed = false;

    if (state_ != nullptr && stack_mb_ != stack_mb)
      stop();
    if (state_ == nullptr)
      start(stack_mb);
    if (state_ == nullptr) {
      llvm::CrashRecoveryContext context;

      completed = context.RunSafelyOnThread(body, stack_mb * 1024u * 1024u);
    } else {
      std::unique_lock<std::mutex> lock(state_->mutex);

      state_->task = &body;
      ++state_->requested;
      state_->cv.notify_all();
      state_->cv.wait(lock, [this] {
        return state_->finished == state_->requested;
      });
      state_->task = nullptr;
      completed = state_->completed;
    }
    return completed;
  }

private:
  struct a_state {
    std::mutex                    mutex;
    std::condition_variable       cv;
    const std::function<void()>  *task = nullptr;
    unsigned long                 requested = 0;   /* Sections handed over. */
    unsigned long                 finished = 0;    /* Sections completed. */
    bool                          stopping = false;
    bool                          completed = false;
  };  /* a_state */

  void start(unsigned stack_mb)
  {
    pthread_attr_t attr;

    if (pthread_attr_init(&attr) == 0) {
      /* The state lives in static storage that is never destroyed: it is
         reachable from a global (no leak report), and nothing runs at exit
         that could wait for the thread.  A slot is used once (the thread
         owns it until the process ends); a few cover a stack size change or
         forks, and after that sections run on a fresh thread each. */
      static const size_t slot_count = 4;
      alignas(a_state) static unsigned char slots[slot_count][sizeof(a_state)];
      static size_t used = 0;
      static bool at_fork_registered = false;
      pthread_t thread;

      if (!at_fork_registered) {
        at_fork_registered = true;
        pthread_atfork(nullptr, nullptr, &a_guard_worker::after_fork);
      }
      if (used < slot_count) {
        a_state *state = new (slots[used++]) a_state;

        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_attr_setstacksize(
                &attr, static_cast<size_t>(stack_mb) * 1024u * 1024u) == 0 &&
            pthread_create(&thread, &attr, &a_guard_worker::entry, state) ==
                0) {
          state_ = state;
          stack_mb_ = stack_mb;
        }
      }
      pthread_attr_destroy(&attr);
    }
  }

  void stop()
  {
    if (state_ != nullptr) {
      std::lock_guard<std::mutex> lock(state_->mutex);

      state_->stopping = true;
      state_->cv.notify_all();
      state_ = nullptr;     /* The thread owns the state from here on. */
    }
  }

  static void after_fork();

  static void *entry(void *state_pointer)
  {
    a_state *state = static_cast<a_state *>(state_pointer);
    std::unique_lock<std::mutex> lock(state->mutex);

    on_guard_worker = true;
    for (;;) {
      state->cv.wait(lock, [state] {
        return state->stopping || state->finished != state->requested;
      });
      if (state->stopping && state->finished == state->requested)
        break;
      const std::function<void()> *task = state->task;

      lock.unlock();
      {
        llvm::CrashRecoveryContext context;

        state->completed = context.RunSafely(*task);
      }
      lock.lock();
      state->finished = state->requested;
      state->cv.notify_all();
    }
    return nullptr;
  }

  a_state  *state_ = nullptr;
  unsigned  stack_mb_ = 0;
};  /* a_guard_worker */

a_guard_worker &guard_worker()
/* The one worker of the process (a guarded section holds guard_mutex). */
{
  static a_guard_worker worker;

  return worker;
}  /* guard_worker */

void a_guard_worker::after_fork()
{
  guard_worker().forget();
}  /* a_guard_worker::after_fork */

}  /* anonymous namespace */

void hold_crash_recovery()
/*
Keep LLVM crash recovery enabled until the matching release_crash_recovery.
A session holds it from its first consumed frame to finish, so the signal
handlers are installed once per session and not once per frame.
*/
{
  std::lock_guard<std::mutex> lock(guard_mutex());

  if (crash_recovery_holders++ == 0)
    llvm::CrashRecoveryContext::Enable();
}  /* hold_crash_recovery */

void release_crash_recovery()
/*
Drop one hold; the last one restores the previous signal handlers.
*/
{
  std::lock_guard<std::mutex> lock(guard_mutex());

  if (crash_recovery_holders != 0 && --crash_recovery_holders == 0)
    llvm::CrashRecoveryContext::Disable();
}  /* release_crash_recovery */

namespace {

bool run_guarded_here(const std::function<void()> &body, unsigned stack_mb,
                      std::string &message)
/*
Run body under LLVM crash recovery and a fatal-error handler.  Both are
process-global, so one mutex serializes every guarded section of every
session (one active session per process).  Crash recovery stays enabled while
a session holds it (hold_crash_recovery); otherwise it is enabled for this
call only.  stack_mb != 0 runs body on the persistent big-stack worker
(a_guard_worker).  Returns false (and the reason in message) when body
crashed or hit a fatal error.
*/
{
  std::lock_guard<std::mutex> lock(guard_mutex());
  a_fatal_state fatal;
  bool completed;
  bool temporary = crash_recovery_holders == 0;

  if (temporary)
    llvm::CrashRecoveryContext::Enable();
  llvm::install_fatal_error_handler(fatal_error_handler, &fatal);
  {
    llvm::CrashRecoveryContext context;
    completed = stack_mb != 0 ? guard_worker().run(body, stack_mb)
                              : context.RunSafely(body);
  }
  llvm::remove_fatal_error_handler();
  if (temporary)
    llvm::CrashRecoveryContext::Disable();
  message = fatal.message;
  return completed;
}  /* run_guarded_here */

}  /* anonymous namespace */

bool run_guarded(const std::function<void()> &body, unsigned stack_mb,
                 std::string &message)
/*
run_guarded_here, except that a guarded section started from inside the
worker (a callback that calls back into the library) runs on the worker as
it is: the worker is busy with the section that started this one, and the
guard mutex is held by the thread that waits for it.
*/
{
  bool completed;

  if (on_guard_worker) {
    llvm::CrashRecoveryContext inner;

    completed = inner.RunSafely(body);
    message.clear();
  } else {
    completed = run_guarded_here(body, stack_mb, message);
  }
  return completed;
}  /* run_guarded */

an_edg_cir_status run_pipeline(an_edg_cir_session &session)
/*
Run the pipeline on a big-stack worker thread and return the session status.
*/
{
  std::string reason;
  bool completed;

  session.in_worker = true;
  completed = run_guarded([&session] { pipeline_body(session); },
                          static_cast<unsigned>(EDG_CIR_WORKER_STACK_MB),
                          reason);
  session.in_worker = false;
  session.deliver_queue();
  if (!completed) {
    session.poisoned = true;
    session.fail(EDG_CIR_ERROR_INTERNAL, edc_internal,
                 reason.empty()
                     ? "the CIR pipeline crashed (signal)"
                     : "internal error in the CIR pipeline: " + reason);
  } else if (session.status != EDG_CIR_OK && session.status_message.empty()) {
    session.fail(session.status, edc_pipeline, "the CIR pipeline failed");
  }
  return session.status;
}  /* run_pipeline */

}  /* namespace edg_cir */
