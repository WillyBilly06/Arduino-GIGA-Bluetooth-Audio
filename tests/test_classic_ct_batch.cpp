#define GBR_CLASSIC_CT_BATCH
#include "test_classic_matched_response.cpp"

int main(){const char* names[]={"ct_caps_plain","ct_caps_fragment","ct_status_plain","ct_status_fragment","ct_metadata_plain","ct_metadata_fragment","ct_unmatched_fence","ct_malformed_fence","ct_expired_fence","ct_wrong_cid_fence","ct_fragmented_avctp_fence","ct_notification_fence","ct_key_fence"};unsigned passed=0;for(auto name:names){checks=0;std::printf("CT_BATCH_CASE_START %s\n",name);std::fflush(stdout);char prog[]="ct";char* args[]={prog,const_cast<char*>(name),nullptr};const int rc=controller_case_main(2,args);if(rc)return rc;++passed;std::printf("CT_BATCH_CASE_PASS %s\n",name);std::fflush(stdout);}std::printf("CT_BATCH_SUMMARY total=13 passed=%u failed=0\n",passed);return 0;}
