#pragma once

#include <cerrno>
#include <cstdint>

namespace Star {

// Rename-over-existing fallback for filesystems whose rename does not replace.
// Operations return captured errors so stat/rollback cannot hide the cause.
struct FileReplaceError {
  int code = 0;
  uint32_t native = 0;
};

enum class FileReplaceStatus {
  Complete, DirectFailed, BackupFailed, PromotionFailed,
  RollbackFailed, CompleteWithBackup
};

struct FileReplaceResult {
  FileReplaceStatus status = FileReplaceStatus::Complete;
  FileReplaceError error, recoveryError;
};

template <typename Path, typename Operations>
FileReplaceResult replaceFileByRename(Path const& source, Path const& target, Path const& backup, Operations& ops) {
  auto direct = ops.rename(source, target);
  if (!direct.code) return {};
  if (direct.code != EEXIST || !ops.isFile(source) || !ops.isFile(target))
    return {FileReplaceStatus::DirectFailed, direct, {}};
  if (source == target) return {};

  // Never unlink the existing destination or reuse/delete an unknown backup.
  auto reserve = ops.rename(target, backup);
  if (reserve.code) return {FileReplaceStatus::BackupFailed, reserve, {}};
  auto promote = ops.rename(source, target);
  if (promote.code) {
    auto rollback = ops.rename(backup, target);
    return {rollback.code ? FileReplaceStatus::RollbackFailed : FileReplaceStatus::PromotionFailed, promote, rollback};
  }
  auto cleanup = ops.remove(backup);
  if (cleanup.code) return {FileReplaceStatus::CompleteWithBackup, cleanup, {}};
  return {};
}

}
