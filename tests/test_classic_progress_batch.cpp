#define GBR_CLASSIC_PROGRESS_BATCH
#include "test_classic_media_progress.cpp"

int main(int argc,char** argv){
 const std::string group=argc>1?argv[1]:"all";std::vector<std::string> cases;
 if(group=="all"||group=="base"){
  for(const char* kind:{"avrcp","sdp"})for(const char* control:{"plain","fragment"})for(const char* media:{"plain","fragment"})for(bool ble:{false,true})cases.push_back(std::string(kind)+"_"+control+"_"+media+(ble?"_ble":""));
  for(const char* x:{"security_control","lifecycle_control","split_media_header","unsafe_after_safe","parked_detach","parked_cid_retire","stale_parked_owner","parked_timeout"})cases.push_back(x);
 }
 if(group=="all"||group=="read"){
  for(const char* kind:{"subunit","capability2","capability3","unit_operands","subunit_operands"})for(const char* control:{"plain","fragment"})for(const char* media:{"plain","fragment"})for(bool ble:{false,true})cases.push_back(std::string(kind)+"_"+control+"_"+media+(ble?"_ble":""));
  for(const char* x:{"avrcp_volume_fence","avrcp_notify_fence","avrcp_passthrough_fence","avrcp_bad_caps_fence","avrcp_fragment_start_fence"})cases.push_back(x);
 }
 if(cases.empty()){std::fprintf(stderr,"UNKNOWN_BATCH_GROUP\n");return 3;}
 unsigned passed=0;for(auto& name:cases){readonly_mode=0;checks=0;std::printf("CLASSIC_BATCH_CASE_START %s\n",name.c_str());std::fflush(stdout);char program[]="classic-test";char* argv_case[]={program,const_cast<char*>(name.c_str()),nullptr};int result=semantic_case_main(2,argv_case);if(result){std::printf("CLASSIC_BATCH_CASE_FAIL %s exit=%d\n",name.c_str(),result);return result;}++passed;std::printf("CLASSIC_BATCH_CASE_PASS %s\n",name.c_str());std::fflush(stdout);}
 std::printf("CLASSIC_BATCH_SUMMARY group=%s total=%zu passed=%u failed=0\n",group.c_str(),cases.size(),passed);return 0;
}
