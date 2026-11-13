// Host test of replacement/rollback semantics, without a console filesystem.
#include "StarFileReplacement.hpp"
#include <map>
#include <set>
#include <string>
#include <cstdio>
#include <stdexcept>

struct Files {
  std::map<std::string, std::string> files;
  std::set<int> failRenames;
  bool failCleanup = false, targetDirectory = false;
  int calls = 0;
  Star::FileReplaceError rename(std::string const& from, std::string const& to) {
    if (failRenames.count(++calls)) return {EIO, 123};
    auto file = files.find(from);
    if (file == files.end()) return {ENOENT, 202};
    if (files.count(to)) return {EEXIST, 402};
    files[to] = file->second;
    files.erase(file);
    return {};
  }
  bool isFile(std::string const& path) { return files.count(path) && !(targetDirectory && path == "save"); }
  Star::FileReplaceError remove(std::string const& path) {
    if (failCleanup) return {EIO, 124};
    return files.erase(path) ? Star::FileReplaceError{} : Star::FileReplaceError{ENOENT, 202};
  }
};

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}
Star::FileReplaceResult run(Files& fs) {
  return Star::replaceFileByRename(std::string("new"), std::string("save"), std::string("backup"), fs);
}
int main() {
  using Status = Star::FileReplaceStatus;
  try {
    Files fresh; fresh.files = {{"new", "NEW"}};
    require(run(fresh).status == Status::Complete && fresh.files.size() == 1 && fresh.files.at("save") == "NEW", "New destination");
    Files existing; existing.files = {{"new", "NEW"}, {"save", "OLD"}};
    require(run(existing).status == Status::Complete && existing.files.size() == 1 && existing.files.at("save") == "NEW", "Replace existing destination");
    Files missing; missing.files = {{"save", "OLD"}};
    require(run(missing).status == Status::DirectFailed && missing.files.at("save") == "OLD", "Missing source preserves destination");
    Files reserve; reserve.files = {{"new", "NEW"}, {"save", "OLD"}}; reserve.failRenames = {2};
    require(run(reserve).status == Status::BackupFailed && reserve.files.at("save") == "OLD" && reserve.files.at("new") == "NEW", "Backup failure preserves both files");
    Files promote; promote.files = {{"new", "NEW"}, {"save", "OLD"}}; promote.failRenames = {3};
    require(run(promote).status == Status::PromotionFailed && promote.files.at("save") == "OLD" && promote.files.at("new") == "NEW" && !promote.files.count("backup"), "Promotion failure restores old destination");
    Files rollback; rollback.files = {{"new", "NEW"}, {"save", "OLD"}}; rollback.failRenames = {3, 4};
    auto result = run(rollback);
    require(result.status == Status::RollbackFailed && rollback.files.at("backup") == "OLD" && rollback.files.at("new") == "NEW" && result.recoveryError.code == EIO, "Rollback failure retains recoverable old and new files");
    Files collision; collision.files = {{"new", "NEW"}, {"save", "OLD"}, {"backup", "KEEP"}};
    require(run(collision).status == Status::BackupFailed && collision.files.at("backup") == "KEEP" && collision.files.at("save") == "OLD", "Backup collision never deletes existing data");
    Files cleanup; cleanup.files = {{"new", "NEW"}, {"save", "OLD"}}; cleanup.failCleanup = true;
    require(run(cleanup).status == Status::CompleteWithBackup && cleanup.files.at("save") == "NEW" && cleanup.files.at("backup") == "OLD", "Cleanup failure keeps successful save and old backup");
    Files directory; directory.files = {{"new", "NEW"}, {"save", "DIRECTORY"}}; directory.targetDirectory = true;
    require(run(directory).status == Status::DirectFailed && directory.calls == 1 && directory.files.at("save") == "DIRECTORY", "Do not replace directories");
    std::puts("Switch 2 file replacement tests passed: create, replace, missing source, backup failure/collision, promotion rollback, rollback failure, cleanup failure, directory refusal.");
    return 0;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
