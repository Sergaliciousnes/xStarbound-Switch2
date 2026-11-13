#pragma once
#include "StarBTreeDatabase.hpp"
#include "StarBuffer.hpp"
#include <stdexcept>

namespace Star {
inline void checkBTreeReadCacheRegression() {
  class CountingBuffer : public Buffer {
  public:
    size_t readAbsolute(StreamOffset offset, char* data, size_t size) override {
      ++reads;
      return Buffer::readAbsolute(offset, data, size);
    }
    unsigned reads = 0;
  };
  auto check = [](bool passed) {
    if (!passed) throw std::runtime_error("B-tree read cache regression failed");
  };
  auto key = [](unsigned i) {
    char bytes[] = {char(i >> 8), char(i)};
    return ByteArray(bytes, 2);
  };
  auto device = make_shared<CountingBuffer>();
  BTreeDatabase db(-1.0f); // Exercise lifecycle without automatic compaction.
  db.setContentIdentifier("ReadCacheTest");
  db.setKeySize(2);
  db.setIODevice(device);
  db.setAutoCommit(false);
  db.setReadBlockCacheBudget(1024 * 1024);
  db.open();
  for (unsigned i = 0; i < 256; ++i) db.insert(key(i), ByteArray(96, char(i)));
  db.commit();
  auto checkValues = [&] {
    for (unsigned i = 0; i < 256; ++i) {
      auto value = db.find(key(i));
      check(value && *value == ByteArray(96, char(i)));
    }
  };
  checkValues();
  auto warmedReads = device->reads;
  checkValues();
  check(device->reads == warmedReads);
  db.insert(key(7), ByteArray(96, '!'));
  check(db.find(key(7)).value() == ByteArray(96, '!'));
  db.rollback();
  checkValues();
  db.insert(key(7), ByteArray(96, '!'));
  db.commit();
  check(db.find(key(7)).value() == ByteArray(96, '!'));
  db.remove(key(8));
  check(!db.find(key(8)));
  db.rollback();
  check(db.find(key(8)).value() == ByteArray(96, char(8)));
  db.remove(key(8));
  db.commit();
  db.close(false);
  db.open();
  check(db.find(key(7)).value() == ByteArray(96, '!') && !db.find(key(8)));
  db.setReadBlockCacheBudget(0);
  auto uncachedReads = device->reads;
  check(db.find(key(7)).value() == ByteArray(96, '!'));
  check(device->reads > uncachedReads);
  db.close(false);
}
}
