#include "../core/StarPersistedSnapshot.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <stdexcept>
#include <cstdio>
int main(int argc,char** argv){
 assert(argc==2);std::filesystem::path path=argv[1];std::filesystem::remove(path);std::optional<std::string> saved;int writes=0;bool fail=false;
 auto save=[&](std::string const& value){return Star::persistChanged(saved,value,[&]{return std::filesystem::exists(path);},[&](std::string const& data){++writes;if(fail)throw std::runtime_error("injected failure");std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<data;file.close();if(!file)throw std::runtime_error("write failure");});};
 assert(save("first")&&writes==1);assert(!save("first")&&writes==1);assert(save("changed")&&writes==2);
 fail=true;try{save("failed");assert(false);}catch(std::runtime_error const&){}assert(*saved=="changed");
 fail=false;assert(save("failed")&&writes==4);assert(!save("failed"));
 std::filesystem::remove(path);assert(save("failed")&&writes==5); // Recreate missing destination.
 assert(save("")&&writes==6);assert(!save("")); // Clearing content must persist.
 std::ifstream file(path,std::ios::binary);std::string data((std::istreambuf_iterator<char>(file)),{});assert(data.empty());file.close();std::filesystem::remove(path);
 std::puts("Persisted snapshots passed: first/changed/unchanged saves, acknowledgement after success, failed-write retry, missing-file recreation and clearing content.");
}
