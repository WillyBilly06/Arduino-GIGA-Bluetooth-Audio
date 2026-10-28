#define GBR_CLASSIC_REVIEW_BATCH
#include "test_classic_review_edges.cpp"

int main(){const char* names[]={"four_piece_header","zero_assembly_deadline"};unsigned passed=0;for(auto name:names){checks=0;readonly_mode=0;std::printf("REVIEW_BATCH_CASE_START %s\n",name);std::fflush(stdout);char prog[]="review";char* args[]={prog,const_cast<char*>(name),nullptr};const int rc=review_case_main(2,args);if(rc)return rc;++passed;std::printf("REVIEW_BATCH_CASE_PASS %s\n",name);std::fflush(stdout);}std::printf("REVIEW_BATCH_SUMMARY total=2 passed=%u failed=0\n",passed);return 0;}
