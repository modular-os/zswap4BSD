//===--- HIPAMD.cpp - HIP Tool and ToolChain Implementations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HIPAMD.h"
#include "AMDGPU.h"
#include "CommonArgs.h"
#include "HIPUtility.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetID.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetParser.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

#if defined(_WIN32) || defined(_WIN64)
#define NULL_FILE "nul"
#else
#define NULL_FILE "/dev/null"
#endif

static bool shouldSkipSanitizeOption(const ToolChain &TC,
                                     const llvm::opt::ArgList &DriverArgs,
                                     StringRef TargetID,
                                     const llvm::opt::Arg *A) {
  // For actions without targetID, do nothing.
  if (TargetID.empty())
    return false;
  Option O = A->getOption();
  if (!O.matches(options::OPT_fsanitize_EQ))
    return false;

  if (!DriverArgs.hasFlag(options::OPT_fgpu_sanitize,
                          -options::OPT_fno_gpu_sanitize))
    return true;

  auto &Diags = TC.getDriver().getDiags();

  // For simplicity, we only allow -fsanitize=address
  SanitizerMask K = parseSanitizerValue(A->getValue(), /*AllowGroups=*/false);
  if (K != SanitizerKind::Address)
    return true;

  llvm::StringMap<bool> FeatureMap;
  auto OptionalGpuArch = parseTargetID(TC.getTriple(), TargetID, &FeatureMap);

  assert(OptionalGpuArch && "Invalid Target ID");
  (void)OptionalGpuArch;
  auto Loc = FeatureMap.find("xnack");
  if (Loc == FeatureMap.end() || !Loc->second) {
    Diags.Report(
        clang::diag::warn_drv_unsupported_option_for_offload_arch_req_feature)
        << A->getAsString(DriverArgs) << TargetID << "xnack+";
    return true;
  }
  return false;
}

void AMDGCN::Linker::constructLldCommand(Compilation &C, const JobAction &JA,
                                         const InputInfoList &Inputs,
                                         const InputInfo &Output,
                                         const llvm::opt::ArgList &Args) const {
  // Construct lld command.
  // The output from ld.lld is an HSA code object file.
  ArgStringList LldArgs{"-flavor", "gnu", "--no-undefined", "-shared",
                        "-plugin-opt=-amdgpu-internalize-symbols"};

  auto &TC = getToolChain();
  auto &D = TC.getDriver();
  assert(!Inputs.empty() && "Must have at least one input.");
  bool IsThinLTO = D.getLTOMode(/*IsOffload=*/true) == LTOK_Thin;
  addLTOOptions(TC, Args, LldArgs, Output, Inputs[0], IsThinLTO);

  // Extract all the -m options
  std::vector<llvm::StringRef> Features;
  amdgpu::getAMDGPUTargetFeatures(D, TC.getTriple(), Args, Features);

  // Add features to mattr such as cumode
  std::string MAttrString = "-plugin-opt=-mattr=";
  for (auto OneFeature : unifyTargetFeatures(Features)) {
    MAttrString.append(Args.MakeArgString(OneFeature));
    if (OneFeature != Features.back())
      MAttrString.append(",");
  }
  if (!Features.empty())
    LldArgs.push_back(Args.MakeArgString(MAttrString));

  // ToDo: Remove this option after AMDGPU backend supports ISA-level linking.
  // Since AMDGPU backend currently does not support ISA-level linking, all
  // called functions need to be imported.
  if (IsThinLTO)
    LldArgs.push_back(Args.MakeArgString("-plugin-opt=-force-import-all"));

  for (const Arg *A : Args.filtered(options::OPT_mllvm)) {
    LldArgs.push_back(
        Args.MakeArgString(Twine("-plugin-opt=") + A->getValue(0)));
  }

  if (C.getDriver().isSaveTempsEnabled())
    LldArgs.push_back("-save-temps");

  addLinkerCompressDebugSectionsOption(TC, Args, LldArgs);

  LldArgs.append({"-o", Output.getFilename()});
  for (auto Input : Inputs)
    LldArgs.push_back(Input.getFilename());

  const char *Lld = Args.MakeArgString(getToolChain().GetProgramPath("lld"));
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Lld, LldArgs, Inputs, Output));
}

// For amdgcn the inputs of the linker job are device bitcode and output is
// object file. It calls llvm-link, opt, llc, then lld steps.
void AMDGCN::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                  const InputInfo &Output,
                                  const InputInfoList &Inputs,
                                  const ArgList &Args,
                                  const char *LinkingOutput) const {
  if (Inputs.size() > 0 &&
      Inputs[0].getType() == types::TY_Image &&
      JA.getType() == types::TY_Object)
    return HIP::constructGenerateObjFileFromHIPFatBinary(C, Output, Inputs,
                                                         Args, JA, *this);

  if (JA.getType() == types::TY_HIP_FATBIN)
    return HIP::constructHIPFatbinCommand(C, JA, Output.getFilename(), Inputs,
                                          Args, *this);

  return constructLldCommand(C, JA, Inputs, Output, Args);
}

HIPAMDToolChain::HIPAMDToolChain(const Driver &D, const llvm::Triple &Triple,
                                 const ToolChain &HostTC, const ArgList &Args)
    : ROCMToolChain(D, Triple, Args), HostTC(HostTC) {
  // Lookup binaries into the driver directory, this is used to
  // discover the clang-offload-bundler executable.
  getProgramPaths().push_back(getDriver().Dir);

  // Diagnose unsupported sanitizer options only once.
  for (auto A : Args.filtered(options::OPT_fsanitize_EQ)) {
    SanitizerMask K = parseSanitizerValue(A->getValue(), /*AllowGroups=*/false);
    if (K != SanitizerKind::Address)
      D.getDiags().Report(clang::diag::warn_drv_unsupported_option_for_target)
          << A->getAsString(Args) << getTriple().str();
  }
}

void HIPAMDToolChain::addClangTargetOptions(
    const llvm::opt::ArgList &DriverArgs, llvm::opt::ArgStringList &CC1Args,
    Action::OffloadKind DeviceOffloadingKind) const {
  HostTC.addClangTargetOptions(DriverArgs, CC1Args, DeviceOffloadingKind);

  assert(DeviceOffloadingKind == Action::OFK_HIP &&
         "Only HIP offloading kinds are supported for GPUs.");

  CC1Args.push_back("-fcuda-is-device");

  if (DriverArgs.hasFlag(options::OPT_fcuda_approx_transcendentals,
                         options::OPT_fno_cuda_approx_transcendentals, false))
    CC1Args.push_back("-fcuda-approx-transcendentals");

  if (!DriverArgs.hasFlag(options::OPT_fgpu_rdc, options::OPT_fno_gpu_rdc,
                          false))
    CC1Args.append({"-mllvm", "-amdgpu-internalize-symbols"});

  StringRef MaxThreadsPerBlock =
      DriverArgs.getLastArgValue(options::OPT_gpu_max_threads_per_block_EQ);
  if (!MaxThreadsPerBlock.empty()) {
    std::string ArgStr =
        std::string("--gpu-max-threads-per-block=") + MaxThreadsPerBlock.str();
    CC1Args.push_back(DriverArgs.MakeArgStringRef(ArgStr));
  }

  CC1Args.push_back("-fcuda-allow-variadic-functions");

  // Default to "hidden" visibility, as object level linking will not be
  // supported for the foreseeable future.
  if (!DriverArgs.hasArg(options::OPT_fvisibility_EQ,
                         options::OPT_fvisibility_ms_compat)) {
    CC1Args.append({"-fvisibility", "hidden"});
    CC1Args.push_back("-fapply-global-visibility-to-externs");
  }

  llvm::for_each(getHIPDeviceLibs(DriverArgs), [&](auto BCFile) {
    CC1Args.push_back(BCFile.ShouldInternalize ? "-mlink-builtin-bitcode"
                                               : "-mlink-bitcode-file");
    CC1Args.push_back(DriverArgs.MakeArgString(BCFile.Path));
  });
}

llvm::opt::DerivedArgList *
HIPAMDToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                               StringRef BoundArch,
                               Action::OffloadKind DeviceOffloadKind) const {
  DerivedArgList *DAL =
      HostTC.TranslateArgs(Args, BoundArch, DeviceOffloadKind);
  if (!DAL)
    DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  for (Arg *A : Args) {
    if (!shouldSkipArgument(A) &&
        !shouldSkipSanitizeOption(*this, Args, BoundArch, A))
      DAL->append(A);
  }

  if (!BoundArch.empty()) {
    DAL->eraseArg(options::OPT_mcpu_EQ);
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_mcpu_EQ), BoundArch);
    checkTargetID(*DAL);
  }

  return DAL;
}

Tool *HIPAMDToolChain::buildLinker() const {
  assert(getTriple().getArch() == llvm::Triple::amdgcn);
  return new tools::AMDGCN::Linker(*this);
}

void HIPAMDToolChain::addClangWarningOptions(ArgStringList &CC1Args) const {
  HostTC.addClangWarningOptions(CC1Args);
}

ToolChain::CXXStdlibType
HIPAMDToolChain::GetCXXStdlibType(const ArgList &Args) const {
  return HostTC.GetCXXStdlibType(Args);
}

void HIPAMDToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                                ArgStringList &CC1Args) const {
  HostTC.AddClangSystemIncludeArgs(DriverArgs, CC1Args);
}

void HIPAMDToolChain::AddClangCXXStdlibIncludeArgs(
    const ArgList &Args, ArgStringList &CC1Args) const {
  HostTC.AddClangCXXStdlibIncludeArgs(Args, CC1Args);
}

void HIPAMDToolChain::AddIAMCUIncludeArgs(const ArgList &Args,
                                          ArgStringList &CC1Args) const {
  HostTC.AddIAMCUIncludeArgs(Args, CC1Args);
}

void HIPAMDToolChain::AddHIPIncludeArgs(const ArgList &DriverArgs,
                                        ArgStringList &CC1Args) const {
  RocmInstallation.AddHIPIncludeArgs(DriverArgs, CC1Args);
}

SanitizerMask HIPAMDToolChain::getSupportedSanitizers() const {
  // The HIPAMDToolChain only supports sanitizers in the sense that it allows
  // sanitizer arguments on the command line if they are supported by the host
  // toolchain. The HIPAMDToolChain will actually ignore any command line
  // arguments for any of these "supported" sanitizers. That means that no
  // sanitization of device code is actually supported at this time.
  //
  // This behavior is necessary because the host and device toolchains
  // invocations often share the command line, so the device toolchain must
  // tolerate flags meant only for the host toolchain.
  return HostTC.getSupportedSanitizers();
}

VersionTuple HIPAMDToolChain::computeMSVCVersion(const Driver *D,
                                                 const ArgList &Args) const {
  return HostTC.computeMSVCVersion(D, Args);
}

llvm::SmallVector<ToolChain::BitCodeLibraryInfo, 12>
HIPAMDToolChain::getHIPDeviceLibs(const llvm::opt::ArgList &DriverArgs) const {
  llvm::SmallVector<BitCodeLibraryInfo, 12> BCLibs;
  if (DriverArgs.hasArg(options::OPT_nogpulib))
    return {};
  ArgStringList LibraryPaths;

  // Find in --hip-device-lib-path and HIP_LIBRARY_PATH.
  for (auto Path : RocmInstallation.getRocmDeviceLibPathArg())
    LibraryPaths.push_back(DriverArgs.MakeArgString(Path));

  addDirectoryList(DriverArgs, LibraryPaths, "", "HIP_DEVICE_LIB_PATH");

  // Maintain compatability with --hip-device-lib.
  auto BCLibArgs = DriverArgs.getAllArgValues(options::OPT_hip_device_lib_EQ);
  if (!BCLibArgs.empty()) {
    llvm::for_each(BCLibArgs, [&](StringRef BCName) {
      StringRef FullName;
      for (std::string LibraryPath : LibraryPaths) {
        SmallString<128> Path(LibraryPath);
        llvm::sys::path::append(Path, BCName);
        FullName = Path;
        if (llvm::sys::fs::exists(FullName)) {
          BCLibs.push_back(FullName);
          return;
        }
      }
      getDriver().Diag(diag::err_drv_no_such_file) << BCName;
    });
  } else {
    if (!RocmInstallation.hasDeviceLibrary()) {
      getDriver().Diag(diag::err_drv_no_rocm_device_lib) << 0;
      return {};
    }
    StringRef GpuArch = getGPUArch(DriverArgs);
    assert(!GpuArch.empty() && "Must have an explicit GPU arch.");

    // If --hip-device-lib is not set, add the default bitcode libraries.
    if (DriverArgs.hasFlag(options::OPT_fgpu_sanitize,
                           options::OPT_fno_gpu_sanitize) &&
        getSanitizerArgs(DriverArgs).needsAsanRt()) {
      auto AsanRTL = RocmInstallation.getAsanRTLPath();
      if (AsanRTL.empty()) {
        unsigned DiagID = getDriver().getDiags().getCustomDiagID(
            DiagnosticsEngine::Error,
            "AMDGPU address sanitizer runtime library (asanrtl) is not found. "
            "Please install ROCm device library which supports address "
            "sanitizer");
        getDriver().Diag(DiagID);
        return {};
      } else
        BCLibs.push_back({AsanRTL.str(), /*ShouldInternalize=*/false});
    }

    // Add the HIP specific bitcode library.
    BCLibs.push_back(RocmInstallation.getHIPPath());

    // Add common device libraries like ocml etc.
    for (auto N : getCommonDeviceLibNames(DriverArgs, GpuArch.str()))
      BCLibs.push_back(StringRef(N));

    // Add instrument lib.
    auto InstLib =
        DriverArgs.getLastArgValue(options::OPT_gpu_instrument_lib_EQ);
    if (InstLib.empty())
      return BCLibs;
    if (llvm::sys::fs::exists(InstLib))
      BCLibs.push_back(InstLib);
    else
      getDriver().Diag(diag::err_drv_no_such_file) << InstLib;
  }

  return BCLibs;
}

void HIPAMDToolChain::checkTargetID(
    const llvm::opt::ArgList &DriverArgs) const {
  auto PTID = getParsedTargetID(DriverArgs);
  if (PTID.OptionalTargetID && !PTID.OptionalGPUArch) {
    getDriver().Diag(clang::diag::err_drv_bad_target_id)
        << PTID.OptionalTargetID.getValue();
  }
}
