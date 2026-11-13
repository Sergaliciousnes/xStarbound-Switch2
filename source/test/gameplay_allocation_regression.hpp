#pragma once
#include "StarItemDescriptor.hpp"
#include "StarNetElementGroup.hpp"
#include "StarNetElementBasicFields.hpp"
#include "StarNetElementTop.hpp"
#include "StarRandomPoint.hpp"
#include "StarRenderer_vulkan.hpp"
#include <stdexcept>

namespace Star {
inline void checkGameplayAllocationRegression() {
  auto check=[](bool ok, char const* label) { if(!ok)throw std::runtime_error(label); };
  ItemDescriptor a, b;
  check(a.isNull() && b.isNull() && a.count()==0 && a.parameters().isType(Json::Type::Object), "Empty item defaults changed");
  auto changed=a.applyParameters(JsonObject{{"test",42}});
  check(changed.parameters().getInt("test")==42 && a.parameters().toObject().empty() && b.parameters().toObject().empty(), "Empty item parameters alias mutation");
  check(DataStreamBuffer::serialize(a)==DataStreamBuffer::serialize(ItemDescriptor("",0,JsonObject{})), "Empty item wire bytes changed");

  // The original heap-backed serialization path is retained here as an
  // independent protocol reference, including blank deltas and version steps.
  class ReferenceTop : public NetElementGroup {
  public:
    ReferenceTop() { initNetVersion(&version); }
    pair<ByteArray,uint64_t> write(uint64_t from) {
      DataStreamBuffer stream;
      stream.write<bool>(from==0);
      if(from==0) { netStore(stream); version.increment(); }
      else if(writeNetDelta(stream,from)) version.increment();
      else return {ByteArray{},version.current()};
      return {stream.takeData(),version.current()};
    }
    NetElementVersion version;
  };
  NetElementTop<NetElementGroup> actual, receiver;
  ReferenceTop reference;
  NetElementData<int> number, oldNumber, receivedNumber;
  NetElementString text, oldText, receivedText;
  actual.addNetElement(&number);actual.addNetElement(&text);
  reference.addNetElement(&oldNumber);reference.addNetElement(&oldText);
  receiver.addNetElement(&receivedNumber);receiver.addNetElement(&receivedText);
  uint64_t from=0;
  for(unsigned i=0;i<80;++i) {
    if(i%3==0) { number.set(int(i));oldNumber.set(int(i)); }
    if(i%7==0) { text.set(strf("value {}",i));oldText.set(text.get()); }
    auto fresh=actual.writeNetState(from), old=reference.write(from);
    check(fresh.first==old.first && fresh.second==old.second, "Net-state wire bytes/version changed");
    receiver.readNetState(fresh.first);
    check(receivedNumber.get()==number.get() && receivedText.get()==text.get(), "Net-state receiver differs");
    from=fresh.second;
  }

  // Compare with the pre-streaming cell traversal, independently regenerated
  // from fixed seeds. Exercise negative coordinates, boundaries and rotations.
  constexpr uint64_t seed=0x12345678;
  constexpr float cellSize=17;
  Vec2I density{2,7};
  Random2dPointGenerator<uint64_t> generator(seed,cellSize,density);
  auto metadata=[](RandomSource& random) { return random.randu64(); };
  for(unsigned i=0;i<24;++i) {
    PolyF area(RectF::withSize({float(int(i)-12)*7,-33.0f},{91,73}));
    area.rotate(float(i)*0.11f);
    List<pair<Vec2F,uint64_t>> expected, streamed;
    auto bound=area.boundBox();
    for(int64_t x=std::floor(bound.xMin()/cellSize);x<=std::ceil(bound.xMax()/cellSize);++x)
      for(int64_t y=std::floor(bound.yMin()/cellSize);y<=std::ceil(bound.yMax()/cellSize);++y) {
        auto cell=RectF::withSize({x*cellSize,y*cellSize},Vec2F::filled(cellSize));
        if(!area.intersects(PolyF(cell)))continue;
        RandomSource random(staticRandomU64(seed,x,y));
        unsigned count=random.randInt(density[0],density[1]);
        for(unsigned j=0;j<count;++j) {
          Vec2F position=Vec2F(x+float(random.randd()),y+float(random.randd()))*cellSize;
          expected.append({position,metadata(random)});
        }
      }
    generator.forEach(area,metadata,[&](auto const& point){streamed.append(point);});
    check(streamed==expected && generator.generate(area,metadata)==expected, "Streamed star points/order differ from original");
    unsigned coldCalls=0;
    generator.forEach(area,[&](RandomSource& random){++coldCalls;return metadata(random);},[](auto const&){});
    check(coldCalls==0, "Streamed star cache regenerated warm points");
  }
  // Debris uses double coordinates and string metadata. Compare streaming
  // against independently generated original cell polygons/order too.
  Random2dPointGenerator<pair<String,float>,double> debris(seed,cellSize,density);
  auto debrisData=[](RandomSource& random){auto name=strf("debris-{}",random.randu32()%7);return pair<String,float>{name,random.randf(-2,2)};};
  for(unsigned i=0;i<12;++i){
    PolyD area(RectD::withSize({double(int(i)-6)*13,-47.0},{105,83}));area.rotate(double(i)*0.19);
    List<pair<Vec2D,pair<String,float>>> expected,streamed;
    auto bound=area.boundBox();
    for(int64_t x=std::floor(bound.xMin()/cellSize);x<=std::ceil(bound.xMax()/cellSize);++x)
      for(int64_t y=std::floor(bound.yMin()/cellSize);y<=std::ceil(bound.yMax()/cellSize);++y){
        auto cell=RectD::withSize({x*cellSize,y*cellSize},Vec2D::filled(cellSize));
        if(!area.intersects(PolyD(cell)))continue;
        RandomSource random(staticRandomU64(seed,x,y));auto count=random.randInt(density[0],density[1]);
        for(unsigned j=0;j<unsigned(count);++j){Vec2D point=Vec2D(x+random.randd(),y+random.randd())*cellSize;expected.append({point,debrisData(random)});}
      }
    debris.forEach(area,debrisData,[&](auto const& point){streamed.append(point);});
    check(streamed==expected&&debris.generate(area,debrisData)==expected,"Debris cell geometry/streamed metadata/order changed");
  }
  // Draw copies and cached-buffer replays must keep immutable texture versions
  // alive even when the source batch is cleared.
  VulkanRenderer::Draw draw{};
  for(unsigned i=0;i<4;++i) {
    auto texture=make_shared<VulkanTextureData>();texture->id=i;
    draw.textures.push_back(texture);
  }
  auto copy=draw;
  draw.textures.clear();
  check(copy.textures.size()==4, "Inline texture copy lost slots");
  for(unsigned i=0;i<4;++i)check(copy.textures[i]->id==i, "Inline texture copy lost lifetime/order");
  auto moved=std::move(copy);
  check(moved.textures.size()==4 && moved.textures[3]->id==3, "Inline texture move lost lifetime/order");
}
}
