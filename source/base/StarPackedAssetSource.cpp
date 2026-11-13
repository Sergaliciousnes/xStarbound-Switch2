#include "StarPackedAssetSource.hpp"
#include "StarDirectoryAssetSource.hpp"
#include "StarOrderedSet.hpp"
#include "StarDataStreamDevices.hpp"
#include "StarDataStreamExtra.hpp"
#include "StarSha256.hpp"
#include "StarFile.hpp"
#include "StarSwitch2IoProfile.hpp"
#include "StarSwitch2AssetStartupTrace.hpp"
#include "StarSwitch2ExplorationProfile.hpp"
#ifdef STAR_SYSTEM_SWITCH2
#include "StarSequentialReadBuffer.hpp"
#include "StarBoundedResourceCache.hpp"
#endif

namespace Star {

#ifdef STAR_SYSTEM_SWITCH2
// Archive indexes live in memory, but their native file handles need not stay
// open for the lifetime of Assets. Limit shared asset handles regardless of how
// many mods are installed. The lock covers a complete positional read, so an
// eviction cannot close a handle while another thread is using it.
class PackedAssetFile {
public:
  explicit PackedAssetFile(String filename):m_filename(std::move(filename)){}
  String deviceName() const{return m_filename;}
  void readFullAbsolute(StreamOffset offset,char* data,size_t size){
    if(!size)return;
    static BoundedResourceCache<String,FilePtr,8> files;
    using namespace Switch2ExplorationProfile;
    STAR_EXPLORATION_SCOPE(acquireTrace,Kind::ArchiveAcquire,m_filename.utf8Ptr());
    STAR_EXPLORATION_SCOPE(lockTrace,Kind::ArchiveWait,m_filename.utf8Ptr());
    files.with(m_filename,[&]{STAR_EXPLORATION_SCOPE(openTrace,Kind::ArchiveOpen,m_filename.utf8Ptr());return File::open(m_filename,IOMode::Read);},
      [&](FilePtr const& file){acquireTrace.stop();STAR_EXPLORATION_SCOPE(readTrace,Kind::ArchiveRead,m_filename.utf8Ptr(),size);file->readFullAbsolute(offset,data,size);},
      [&](ResourceCacheEvent event){
        if(event==ResourceCacheEvent::Locked)lockTrace.stop();
        else if(event==ResourceCacheEvent::Hit)STAR_EXPLORATION_COUNT(Counter::ArchiveHit);
        else if(event==ResourceCacheEvent::Miss)STAR_EXPLORATION_COUNT(Counter::ArchiveMiss);
        else if(event==ResourceCacheEvent::Evict)STAR_EXPLORATION_COUNT(Counter::ArchiveEvict);
      });
  }
private:
  String m_filename;
};
using PackedFileHandle=PackedAssetFilePtr;
#else
using PackedFileHandle=FilePtr;
#endif

void PackedAssetSource::build(DirectoryAssetSource& directorySource, String const& targetPackedFile,
    StringList const& extensionSorting, BuildProgressCallback progressCallback) {
  FilePtr file = File::open(targetPackedFile, IOMode::ReadWrite | IOMode::Truncate);

  DataStreamIODevice ds(file);

  ds.writeData("SBAsset6", 8);
  // Skip 8 bytes, this will be a pointer to the index once we are done.
  ds.seek(8, IOSeek::Relative);

  // Insert every found entry into the packed file, and also simultaneously
  // compute the full index.
  StringMap<pair<uint64_t, uint64_t>> index;

  OrderedHashSet<String> extensionOrdering;
  for (auto const& str : extensionSorting)
    extensionOrdering.add(str.toLower());

  StringList assetPaths = directorySource.assetPaths();

  // Returns a value for the asset that can be used to predictably sort assets
  // by name and then by extension, where every extension listed in
  // "extensionSorting" will come first, and then any extension not listed will
  // come after.
  auto getOrderingValue = [&extensionOrdering](String const& asset) -> pair<size_t, String> {
    String extension;
    auto lastDot = asset.findLast(".");
    if (lastDot != NPos)
      extension = asset.substr(lastDot + 1);

    if (auto i = extensionOrdering.indexOf(extension.toLower())) {
      return {*i, asset.toLower()};
    } else {
      return {extensionOrdering.size(), asset.toLower()};
    }
  };

  assetPaths.sort([&getOrderingValue](String const& a, String const& b) {
      return getOrderingValue(a) < getOrderingValue(b);
    });

  for (size_t i = 0; i < assetPaths.size(); ++i) {
    String const& assetPath = assetPaths[i];
    ByteArray contents = directorySource.read(assetPath);

    if (progressCallback)
      progressCallback(i, assetPaths.size(), directorySource.toFilesystem(assetPath), assetPath);
    index.add(assetPath, {ds.pos(), contents.size()});
    ds.writeBytes(contents);
  }

  uint64_t indexStart = ds.pos();
  ds.writeData("INDEX", 5);
  ds.write(directorySource.metadata());
  ds.write(index);

  ds.seek(8);
  ds.write(indexStart);
}

PackedAssetSource::PackedAssetSource(String const& filename, bool metadataOnly) {
  Switch2AssetStartupTrace::checkpoint(metadataOnly?"pak-metadata-open":"pak-index-open",filename.utf8Ptr());
  auto indexFile = File::open(filename, IOMode::Read);

  DataStreamIODevice ds(indexFile);
  if (ds.readBytes(8) != ByteArray("SBAsset6", 8))
    throw AssetSourceException("Packed assets file format unrecognized!");

  uint64_t indexStart = ds.read<uint64_t>();

#ifdef STAR_SYSTEM_SWITCH2
  auto fileSize=indexFile->size();
  if(fileSize<21 || indexStart<16 || indexStart>uint64_t(fileSize)-5)
    throw AssetSourceException::format("Packed asset '{}' has an out-of-bounds index offset {} (file size {})",filename,indexStart,fileSize);
  ds.seek(indexStart);
  Switch2AssetStartupTrace::checkpoint("pak-index-read",filename.utf8Ptr(),indexStart,fileSize);
  auto reader=[&](char* data,size_t count){indexFile->readFull(data,count);};
  SequentialReadBuffer<decltype(reader)> buffer(reader,uint64_t(fileSize)-indexStart);
  DataStreamFunctions indexStream([&](char* data,size_t count){buffer.read(data,count);return count;},{});
  auto& indexDs=indexStream;
#else
  ds.seek(indexStart);
  auto& indexDs=ds;
#endif
  ByteArray header = indexDs.readBytes(5);
  if (header != ByteArray("INDEX", 5))
    throw AssetSourceException("No index header found!");
  indexDs.read(m_metadata);
  Switch2AssetStartupTrace::checkpoint("pak-metadata-decoded",filename.utf8Ptr());
  if(!metadataOnly){
#ifdef STAR_SYSTEM_SWITCH2
    size_t entries=0;
    indexDs.readMapContainer(m_index,[&](DataStream& stream,String& path,pair<uint64_t,uint64_t>& entry){
      stream.read(path);stream.read(entry);
      auto offset=entry.first,size=entry.second;
      if(offset<16 || offset>indexStart || size>indexStart-offset)
        throw AssetSourceException::format("Packed asset '{}' contains an out-of-bounds entry '{}'",filename,path);
      if((++entries%16384)==0)
        Switch2AssetStartupTrace::checkpoint("pak-index-decode-progress",filename.utf8Ptr(),entries);
    });
#else
    indexDs.read(m_index);
#endif
  }
#ifdef STAR_SYSTEM_SWITCH2
  // Close even though DataStreamIODevice still holds a shared pointer. It is
  // no longer used after decoding. Metadata-only discovery needs no reader.
  indexFile->close();
  if(!metadataOnly)m_packedFile=make_shared<PackedAssetFile>(filename);
#else
  m_packedFile=std::move(indexFile);
#endif
  Switch2AssetStartupTrace::checkpoint(metadataOnly?"pak-metadata-done":"pak-index-done",filename.utf8Ptr(),m_index.size());
}

JsonObject PackedAssetSource::metadata() const {
  return m_metadata;
}

StringList PackedAssetSource::assetPaths() const {
  return m_index.keys();
}

IODevicePtr PackedAssetSource::open(String const& path) {
  struct AssetReader : public IODevice {
    AssetReader(PackedFileHandle file, String path, StreamOffset offset, StreamOffset size)
      : file(file), path(path), fileOffset(offset), assetSize(size), assetPos(0) {
      setMode(IOMode::Read);
    }

    size_t read(char* data, size_t len) override {
      Switch2IoProfile::Asset profileAsset(path.utf8Ptr());
      len = min<StreamOffset>(len, assetSize - assetPos);
      file->readFullAbsolute(fileOffset + assetPos, data, len);
      assetPos += len;
      return len;
    }

    size_t write(char const*, size_t) override {
      throw IOException("Assets IODevices are read-only");
    }

    StreamOffset size() override {
      return assetSize;
    }

    StreamOffset pos() override {
      return assetPos;
    }

    String deviceName() const override {
      return strf("{}:{}", file->deviceName(), path);
    }

    bool atEnd() override {
      return assetPos >= assetSize;
    }

    void seek(StreamOffset p, IOSeek mode) override {
      if (mode == IOSeek::Absolute)
        assetPos = p;
      else if (mode == IOSeek::Relative)
        assetPos = clamp<StreamOffset>(assetPos + p, 0, assetSize);
      else
        assetPos = clamp<StreamOffset>(assetSize - p, 0, assetSize);
    }

    PackedFileHandle file;
    String path;
    StreamOffset fileOffset;
    StreamOffset assetSize;
    StreamOffset assetPos;
  };

  auto p = m_index.ptr(path);
  if (!p)
    throw AssetSourceException::format("Requested file '{}' does not exist in the packed assets file", path);

  return make_shared<AssetReader>(m_packedFile, path, p->first, p->second);
}

ByteArray PackedAssetSource::read(String const& path) {
  Switch2IoProfile::Asset profileAsset(path.utf8Ptr());
  auto p = m_index.ptr(path);
  if (!p)
    throw AssetSourceException::format("Requested file '{}' does not exist in the packed assets file", path);

  ByteArray data(p->second, 0);
  m_packedFile->readFullAbsolute(p->first, data.ptr(), p->second);
  return data;
}

}
