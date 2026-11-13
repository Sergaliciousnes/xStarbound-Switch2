#include "StarPerformanceMemory.hpp"
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
std::vector<std::string> lines;
void capture(char const* format,...){char buffer[1024];va_list args;va_start(args,format);std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);lines.emplace_back(buffer);}
bool yes(){return true;} bool no(){return false;}
int main(){
  using namespace Star::PerformanceMemory;
  if(!memory(capture,yes)||!allocation(capture,yes))return 1;
  unsigned copies=0,pointers=0,allocations=0,verified=0;
  for(auto const& line:lines){copies+=line.find("[memory-copy]")==0;pointers+=line.find("[pointer-chase]")==0;allocations+=line.find("[allocation]")==0;verified+=line.find("verified=true")!=std::string::npos;}
  if(copies!=3||pointers!=3||allocations!=3||verified!=3)return 2;
  lines.clear();if(memory(capture,no)||allocation(capture,no)||!lines.empty())return 3;
  std::puts("PASS: actual memory/allocator kernels, verified copies, all working sets/trials and early cancellation.");
}
