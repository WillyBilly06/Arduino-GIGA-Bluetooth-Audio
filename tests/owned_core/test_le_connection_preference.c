#include "bth_le_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#c);exit(3);}}while(0)
#ifdef PROBE_BEFORE
typedef struct {uint16_t interval_min,interval_max,latency,timeout;} bth_le_transport_policy_t;
#endif
static const bth_le_transport_policy_t audio={64,96,2,500};
static const uint8_t local[8]={0x0b,0xa1,0xb2,0xc3,0xd4,0xe5,0xf6,7};
static bth_status_t event(bth_le_transport_t* t,uint8_t code,const uint8_t* p,size_t n,uint32_t owner){
    const bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=code,.params={p,n}}};return bth_le_transport_event(t,&e,owner);
}
static bool configured(bth_le_transport_t* t,uint32_t owner,uint8_t role,const uint8_t* features,const bth_le_transport_policy_t* policy,uint32_t now){
#ifdef PROBE_BEFORE
    // Before has no configurable policy API. Exercise its real equivalent
    // connection/feature/update chain to demonstrate the missing wire behavior.
    (void)policy;(void)now;return bth_le_transport_attach(t,owner,0x123,role,features?features[0]:0);
#else
    if(!bth_le_transport_configure(t,owner,0x123,role,features,policy))return false;
    return bth_le_transport_start(t,now)==BTH_OK;
#endif
}
static void next_command(bth_le_transport_t* t,bth_cmdq_t* q,bth_hci_flow_t* flow,uint32_t now,uint16_t opcode,const uint8_t* params,size_t n){
    uint8_t signal[16],out[64];size_t written=99;
    CHECK(bth_le_transport_next(t,q,now,signal,sizeof signal,&written)==BTH_OK && !written);
    CHECK(bth_cmdq_next(q,flow,out,sizeof out,&written)==BTH_OK);
    if(written==18 && opcode==0x2013)printf("UPDATE_WIRE min=%u max=%u latency=%u timeout=%u ce=%u/%u\n",bth_read_le16(out+6),bth_read_le16(out+8),bth_read_le16(out+10),bth_read_le16(out+12),bth_read_le16(out+14),bth_read_le16(out+16));
    CHECK(written==n+4 && bth_read_le16(out+1)==opcode && !memcmp(out+4,params,n));
}
static void status(bth_le_transport_t* t,bth_cmdq_t* q,bth_hci_flow_t* flow,uint16_t opcode,uint8_t error,uint32_t expected_owner){
    uint8_t p[]={error,1,(uint8_t)opcode,(uint8_t)(opcode>>8)};const bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x0f,.params={p,sizeof p}}};
    uint32_t owner=0;unsigned before=q->sent_count;uint8_t credits=flow->command_credits;
    CHECK(bth_le_transport_event(t,&e,expected_owner)==BTH_OK);
    CHECK(q->sent_count==before && flow->command_credits==credits);
    CHECK(bth_cmdq_complete(q,&e,&owner)==BTH_OK && owner==expected_owner);
    CHECK(bth_hci_flow_on_event(flow,&e)==BTH_OK && flow->command_credits==1);
}
static void configured_wire(void){
    bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t flow;bth_cmdq_init(&q);bth_hci_flow_init(&flow);
    CHECK(configured(&t,42,1,local,&audio,100));
    const uint8_t handle[]={0x23,1};next_command(&t,&q,&flow,100,0x2016,handle,2);status(&t,&q,&flow,0x2016,0,42);
    const uint8_t features[]={4,0,0x23,1,3,0x12,0x23,0x34,0x45,0x56,0x67,0x78};
    CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    const uint8_t params[]={0x23,1,64,0,96,0,2,0,0xf4,1,0,0,0,0};next_command(&t,&q,&flow,101,0x2013,params,sizeof params);
    puts("CONFIGURED_LOW_AIRTIME_WIRE_PASS");
}
#ifndef PROBE_BEFORE
static void initial_observation(void){
    bth_le_transport_t t;CHECK(bth_le_transport_configure(&t,42,0x123,1,local,&audio));
    uint8_t initial[]={1,0,0x23,1,1,0,1,2,3,4,5,6,24,0,0,0,0x90,1,0};
    CHECK(event(&t,0x3e,initial,sizeof initial,0)==BTH_OK && t.parameters_known && t.interval==24 && t.latency==0 && t.timeout==400);
    CHECK(t.parameters_source==1 && !t.update_reported && t.last_update_status==0xff);
    for(unsigned len=1;len<sizeof initial;++len){bth_le_transport_t old=t;CHECK(event(&t,0x3e,initial,len,0)==BTH_ERR_LENGTH && !memcmp(&t,&old,sizeof t));}
    bth_le_transport_t old=t;initial[12]=40;initial[16]=10;initial[17]=0;
    CHECK(event(&t,0x3e,initial,sizeof initial,0)==BTH_ERR_PARAM && !memcmp(&t,&old,sizeof t));
    uint8_t enhanced[31]={0x0a,0,0x23,1,1,0};enhanced[24]=80;enhanced[26]=2;enhanced[28]=0xf4;enhanced[29]=1;
    CHECK(event(&t,0x3e,enhanced,sizeof enhanced,0)==BTH_OK && t.parameters_known && t.matches_preference && t.interval==80 && t.latency==2 && t.timeout==500);
    CHECK(t.parameters_source==0x0a && !t.update_reported);
    for(unsigned len=1;len<sizeof enhanced;++len){old=t;CHECK(event(&t,0x3e,enhanced,len,0)==BTH_ERR_LENGTH && !memcmp(&t,&old,sizeof t));}
    CHECK(bth_le_transport_start(&t,0)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && !t.error);
    const uint8_t failure[]={3,0x3b,0x23,1,0,0,0,0,0,0};
    CHECK(event(&t,0x3e,failure,sizeof failure,0)==BTH_OK && t.update_reported && t.last_update_status==0x3b && t.parameters_source==0x0a && t.interval==80 && t.timeout==500);
    const uint8_t success[]={3,0,0x23,1,96,0,2,0,0xf4,1};
    CHECK(event(&t,0x3e,success,sizeof success,0)==BTH_OK && t.parameters_source==3 && !t.last_update_status && t.interval==96);
    CHECK(bth_le_transport_configure(&t,43,0x123,1,NULL,&audio));
    CHECK(event(&t,0x3e,enhanced,sizeof enhanced,0)==BTH_OK && t.matches_preference);
    CHECK(bth_le_transport_start(&t,10)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && !t.error);
    puts("INITIAL_NORMAL_ENHANCED_ACTUAL_PARAMETERS_PASS");
}
static void features_event(bth_le_transport_t* t,uint8_t first){
    const uint8_t p[]={4,0,0x23,1,first,0x12,0x23,0x34,0x45,0x56,0x67,0x78};CHECK(event(t,0x3e,p,sizeof p,0)==BTH_OK);
}
static void feature_command(bth_le_transport_t* t,bth_cmdq_t* q,bth_hci_flow_t* f,uint32_t now,uint32_t owner){
    const uint8_t handle[]={0x23,1};next_command(t,q,f,now,0x2016,handle,2);status(t,q,f,0x2016,0,owner);
}
static void update_command(bth_le_transport_t* t,bth_cmdq_t* q,bth_hci_flow_t* f,uint32_t now){
    const uint8_t p[]={0x23,1,64,0,96,0,2,0,0xf4,1,0,0,0,0};next_command(t,q,f,now,0x2013,p,sizeof p);
}
static void configure_idle_and_observation(void){
    bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t f;bth_cmdq_init(&q);bth_hci_flow_init(&f);uint8_t out[16];size_t n;
    CHECK(bth_le_transport_configure(&t,42,0x123,1,local,&audio));
    CHECK(!memcmp(t.local_feature_set,local,8) && t.stage==BTH_LE_TRANSPORT_IDLE && !t.started);
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_IGNORED && !q.used && !n);
    features_event(&t,3);CHECK(t.remote_features_known && t.remote_feature_set[7]==0x78 && t.stage==BTH_LE_TRANSPORT_IDLE);
    CHECK(bth_le_transport_start(&t,100)==BTH_OK && bth_le_transport_start(&t,101)==BTH_IGNORED);
    update_command(&t,&q,&f,101);CHECK(!t.parameters_known && t.interval==0 && t.latency==0 && t.timeout==0);
    status(&t,&q,&f,0x2013,0,42);CHECK(!t.parameters_known && t.stage==BTH_LE_TRANSPORT_UPDATE_WAIT);
    uint8_t update[]={3,0,0x23,1,80,0,2,0,0xf4,1};
    CHECK(event(&t,0x3e,update,9,0)==BTH_ERR_LENGTH && !t.parameters_known);
    update[2]=0x24;CHECK(event(&t,0x3e,update,10,0)==BTH_IGNORED && !t.parameters_known);update[2]=0x23;
    CHECK(event(&t,0x3e,update,10,0)==BTH_OK && t.parameters_known && t.matches_preference && t.interval==80 && t.latency==2 && t.timeout==500);
    CHECK(t.stage==BTH_LE_TRANSPORT_DONE && !t.error && bth_le_transport_start(&t,200)==BTH_IGNORED);
    // A later central override is an observation, not an invitation to fight.
    update[4]=24;update[6]=0;CHECK(event(&t,0x3e,update,10,0)==BTH_OK && !t.matches_preference && t.interval==24);
    CHECK(bth_le_transport_next(&t,&q,300,out,sizeof out,&n)==BTH_IGNORED && !q.used);
    puts("IDLE_START_ONCE_FULL_FEATURES_ACTUAL_ONLY_OVERRIDE_PASS");
}
static void policies_and_replies(void){
    CHECK(bth_le_transport_policy_valid(&audio));
    const bth_le_transport_policy_t invalid[]={{5,96,2,500},{64,3201,2,500},{96,64,2,500},{64,96,500,500},{64,96,2,9},{64,96,2,3201},{40,40,0,10}};
    bth_le_transport_t t;
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i){CHECK(!bth_le_transport_policy_valid(&invalid[i]));CHECK(!bth_le_transport_configure(&t,42,0x123,1,local,&invalid[i]));}
    CHECK(!bth_le_transport_configure(&t,0,0x123,1,local,&audio));CHECK(!bth_le_transport_configure(&t,42,0xf00,1,local,&audio));CHECK(!bth_le_transport_configure(&t,42,0x123,2,local,&audio));
    CHECK(bth_le_transport_configure(&t,42,0x123,1,local,&audio));
    const struct {uint16_t min,max,latency,timeout,wantMin,wantMax;bool accept;} cases[]={
        {6,3200,0,1000,64,96,true},{80,160,2,500,80,96,true},{64,96,0,500,64,96,true},
        {96,96,0,500,96,96,true},{97,160,0,500,0,0,false},{6,63,0,500,0,0,false},
        {5,80,0,500,0,0,false},{96,64,0,500,0,0,false},{64,96,500,500,0,0,false},
        {40,40,0,10,0,0,false}};
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i){
        uint8_t p[]={6,0x23,1,0,0,0,0,0,0,0,0};
        const uint16_t values[]={cases[i].min,cases[i].max,cases[i].latency,cases[i].timeout};
        for(unsigned k=0;k<4;++k){p[3+2*k]=(uint8_t)values[k];p[4+2*k]=(uint8_t)(values[k]>>8);}
        bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x3e,.params={p,sizeof p}}};
        uint8_t out[16];memset(out,0xa5,sizeof out);size_t n=99;uint16_t op=99;bth_le_transport_t before=t;
        CHECK(bth_le_transport_peer_reply(&t,42,&e,true,out+1,14,&n,&op)==BTH_OK);
        CHECK(out[0]==0xa5 && out[15]==0xa5 && !memcmp(&t,&before,sizeof t));
        CHECK(n==(cases[i].accept?14u:3u) && op==(cases[i].accept?0x2020:0x2021));
        CHECK(out[1]==0x23 && out[2]==1);
        if(cases[i].accept){
            CHECK(bth_read_le16(out+3)==cases[i].wantMin && bth_read_le16(out+5)==cases[i].wantMax);
            CHECK(bth_read_le16(out+7)==cases[i].latency && bth_read_le16(out+9)==cases[i].timeout);
            CHECK(bth_read_le16(out+11)==0 && bth_read_le16(out+13)==0);
        }else CHECK(out[3]==0x3b);
        uint8_t untouched[14];memset(untouched,0x7b,sizeof untouched);size_t small=n-1;
        CHECK(bth_le_transport_peer_reply(&t,42,&e,true,untouched,small,&n,&op)==BTH_ERR_CAPACITY && n==0 && op==0);
        for(unsigned k=0;k<sizeof untouched;++k)CHECK(untouched[k]==0x7b);
        CHECK(bth_le_transport_peer_reply(&t,41,&e,true,out,sizeof out,&n,&op)==BTH_IGNORED && !n && !op);
    }
    // Preference disabled: retain valid fast peer values without substitution.
    uint8_t p[]={6,0x23,1,24,0,40,0,0,0,200,0};bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x3e,.params={p,sizeof p}}};uint8_t out[14];size_t n;uint16_t op;
    CHECK(bth_le_transport_peer_reply(&t,42,&e,false,out,sizeof out,&n,&op)==BTH_OK && op==0x2020 && n==14 && bth_read_le16(out+2)==24 && bth_read_le16(out+4)==40);
    for(unsigned len=0;len<sizeof p;++len){e.event.params.len=len;CHECK(bth_le_transport_peer_reply(&t,42,&e,true,out,sizeof out,&n,&op)==(len?BTH_ERR_LENGTH:BTH_IGNORED) && !n && !op);}
    e.event.params.len=sizeof p;p[1]=0x24;CHECK(bth_le_transport_peer_reply(&t,42,&e,true,out,sizeof out,&n,&op)==BTH_IGNORED && !n);
    puts("POLICY_STRICT_BOUNDARY_OVERLAP_REPLY_2020_2021_EXACT_PASS");
}
static void fallback_and_capabilities(void){
    const uint8_t request[]={0x12,1,8,0,64,0,96,0,2,0,0xf4,1};
    const uint8_t localFirst[]={0,1,3,0x0b};
    for(unsigned i=0;i<sizeof localFirst;++i)for(unsigned remote=0;remote<2;++remote){
        uint8_t bits[8]={localFirst[i],1,2,3,4,5,6,7};bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t f;bth_cmdq_init(&q);bth_hci_flow_init(&f);uint8_t out[16];size_t n;
        CHECK(configured(&t,42,1,bits,&audio,100));
        if(bits[0]&2){
            if(bits[0]&8)feature_command(&t,&q,&f,100,42);
            else CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK && !n && !q.used);
            features_event(&t,remote?3:1);
            if(remote){update_command(&t,&q,&f,101);continue;}
        }
        CHECK(bth_le_transport_next(&t,&q,101,out,11,&n)==BTH_ERR_CAPACITY && !n && t.stage==BTH_LE_TRANSPORT_SIGNAL);
        CHECK(bth_le_transport_next(&t,&q,101,out,sizeof out,&n)==BTH_OK && n==12 && !memcmp(out,request,12) && !q.used);
        uint8_t response[]={0x13,1,2,0,0,0};CHECK(bth_le_transport_signal(&t,response,sizeof response)==BTH_OK && !t.parameters_known);
        const uint8_t fail[]={3,0x1a,0x23,1,0,0,0,0,0,0};CHECK(event(&t,0x3e,fail,sizeof fail,0)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE);
        CHECK(bth_le_transport_next(&t,&q,200,out,sizeof out,&n)==BTH_IGNORED && !n); // no repeated fallback
    }
    bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t f;bth_cmdq_init(&q);bth_hci_flow_init(&f);uint8_t out[16];size_t n;
    CHECK(configured(&t,42,1,NULL,&audio,0));CHECK(t.stage==BTH_LE_TRANSPORT_DONE && t.error==0xfd && !t.local_features_known);
    CHECK(bth_le_transport_next(&t,&q,0,out,sizeof out,&n)==BTH_IGNORED && !q.used);
    CHECK(configured(&t,43,1,local,&audio,0));feature_command(&t,&q,&f,0,43);features_event(&t,3);update_command(&t,&q,&f,1);
    status(&t,&q,&f,0x2013,0x1a,43);CHECK(t.stage==BTH_LE_TRANSPORT_SIGNAL);
    CHECK(bth_le_transport_next(&t,&q,2,out,sizeof out,&n)==BTH_OK && n==12 && !memcmp(out,request,12));
    bth_cmdq_init(&q);const uint8_t no_ll[8]={1};CHECK(configured(&t,44,1,no_ll,&audio,0));
    CHECK(bth_le_transport_next(&t,&q,1,out,sizeof out,&n)==BTH_OK && n==12);
    const uint8_t accepted[]={0x13,1,2,0,0,0};CHECK(bth_le_transport_signal(&t,accepted,sizeof accepted)==BTH_OK && !t.parameters_known);
    const uint8_t actual[]={3,0,0x23,1,96,0,2,0,0xf4,1};CHECK(event(&t,0x3e,actual,sizeof actual,0)==BTH_OK && t.parameters_known && t.matches_preference && !t.error);
    puts("FEATURE_MATRIX_UNKNOWN_SKIP_SINGLE_CID5_FALLBACK_PASS");
}
static void queue_timers_and_collision(void){
    bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t f;bth_cmdq_init(&q);bth_hci_flow_init(&f);uint8_t out[32];size_t n;
    CHECK(configured(&t,42,1,local,&audio,100));
    for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push(&q,0xfc01,NULL,0,false)==BTH_OK);
    bth_le_transport_t copy=t;CHECK(bth_le_transport_next(&t,&q,101,out,sizeof out,&n)==BTH_MORE && !n && t.stage==copy.stage && q.used==8 && f.command_credits==1);
    CHECK(bth_le_transport_expire(&t,65100)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && t.error==0xff && q.used==8 && f.command_credits==1);
    bth_cmdq_init(&q);CHECK(configured(&t,43,1,local,&audio,0xfffffff0u));
    CHECK(bth_le_transport_next(&t,&q,0xfffffff0u,out,sizeof out,&n)==BTH_OK && q.used==1);
    CHECK(bth_le_transport_expire(&t,10)==BTH_MORE && q.used==1);
    CHECK(bth_le_transport_expire(&t,5000)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && !q.used && f.command_credits==1);
    // Expiring an actually sent request preserves its oldest-owner tombstone.
    CHECK(configured(&t,44,1,local,&audio,100));const uint8_t handle[]={0x23,1};next_command(&t,&q,&f,100,0x2016,handle,2);
    CHECK(bth_le_transport_expire(&t,5100)==BTH_OK && q.sent_count==1 && q.sent[0].owner==44 && f.command_credits==0);
    uint8_t ack[]={0,1,0x16,0x20};bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x0f,.params={ack,4}}};uint32_t owner;
    CHECK(bth_cmdq_complete(&q,&e,&owner)==BTH_OK && owner==44);CHECK(bth_hci_flow_on_event(&f,&e)==BTH_OK);
    for(unsigned error=0;error<2;++error){
        CHECK(configured(&t,50+error,1,local,&audio,200));feature_command(&t,&q,&f,200,50+error);features_event(&t,3);update_command(&t,&q,&f,201);
        status(&t,&q,&f,0x2013,error?0x2a:0x23,50+error);CHECK(t.stage==BTH_LE_TRANSPORT_DONE && t.error==(error?0x2a:0x23));
        CHECK(bth_le_transport_start(&t,5000)==BTH_IGNORED && bth_le_transport_next(&t,&q,5000,out,sizeof out,&n)==BTH_IGNORED && !q.used);
    }
    // Yield must cancel only still-queued optional commands, not another owner
    // or same-generation mandatory security/control replies.
    bth_cmdq_init(&q);CHECK(configured(&t,60,1,local,&audio,0));CHECK(bth_le_transport_next(&t,&q,0,out,sizeof out,&n)==BTH_OK);
    CHECK(bth_cmdq_push_owned(&q,0x201b,handle,2,true,60)==BTH_OK);CHECK(bth_cmdq_push_owned(&q,0x2013,handle,2,false,61)==BTH_OK);
    CHECK(bth_le_transport_yield(&t,0x23)==BTH_OK && q.used==2 && t.stage==BTH_LE_TRANSPORT_DONE);
    CHECK(bth_le_transport_cancel(&t,59)==BTH_IGNORED && t.owner==60);CHECK(bth_le_transport_cancel(&t,60)==BTH_OK && !t.owner && q.used==2);
    puts("QUEUE_BOUND_WRAP_CANCELLATION_TOMBSTONE_COLLISION_YIELD_PASS");
}
static void generation_and_lengths(void){
    bth_le_transport_t t;bth_cmdq_t q;bth_hci_flow_t f;bth_cmdq_init(&q);bth_hci_flow_init(&f);uint8_t out[32];size_t n;const uint8_t handle[]={0x23,1};
    CHECK(configured(&t,100,1,local,&audio,0));next_command(&t,&q,&f,0,0x2016,handle,2);
    CHECK(bth_le_transport_cancel(&t,100)==BTH_OK && q.sent_count==1 && f.command_credits==0);
    CHECK(configured(&t,101,1,local,&audio,1));CHECK(bth_le_transport_next(&t,&q,1,out,sizeof out,&n)==BTH_OK && q.used==1);
    uint8_t p[]={0x1a,1,0x16,0x20};bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=0x0f,.params={p,4}}};
    uint32_t owner=0;CHECK(bth_cmdq_complete(&q,&e,&owner)==BTH_OK && owner==100);bth_le_transport_t before=t;
    CHECK(bth_le_transport_event(&t,&e,owner)==BTH_IGNORED && !memcmp(&t,&before,sizeof t));CHECK(bth_hci_flow_on_event(&f,&e)==BTH_OK);
    CHECK(bth_cmdq_next(&q,&f,out,sizeof out,&n)==BTH_OK && q.sent[0].owner==101);
    uint8_t correct[]={0,1,0x16,0x20};
    for(unsigned len=0;len<4;++len){before=t;CHECK(event(&t,0x0f,correct,len,101)==BTH_ERR_LENGTH && !memcmp(&t,&before,sizeof t));}
    CHECK(event(&t,0x0f,correct,4,100)==BTH_IGNORED);status(&t,&q,&f,0x2016,0,101);
    const uint8_t feature[]={4,0,0x23,1,3,1,2,3,4,5,6,7};
    for(unsigned len=1;len<12;++len){before=t;CHECK(event(&t,0x3e,feature,len,0)==BTH_ERR_LENGTH && !memcmp(&t,&before,sizeof t));}
    features_event(&t,3);update_command(&t,&q,&f,2);status(&t,&q,&f,0x2013,0,101);
    uint8_t update[]={3,0,0x23,1,40,0,0,0,10,0};before=t;
    CHECK(event(&t,0x3e,update,10,0)==BTH_ERR_PARAM && !memcmp(&t,&before,sizeof t));
    const uint8_t cc[]={1,0x13,0x20,0};before=t;CHECK(event(&t,0x0e,cc,sizeof cc,101)==BTH_IGNORED && !memcmp(&t,&before,sizeof t));
    puts("SAME_HANDLE_NEW_GENERATION_OLDEST_ACK_STRICT_EVENTS_PASS");
}
static void late_observation_keeps_failure(void){
    bth_le_transport_t t;bth_cmdq_t q;bth_cmdq_init(&q);uint8_t out[16];size_t n;
    CHECK(configured(&t,42,1,local,&audio,100));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK && q.used==1);
    CHECK(bth_le_transport_expire(&t,5100)==BTH_OK && t.error==0xff && !q.used);
    const uint8_t update[]={3,0,0x23,1,80,0,2,0,0xf4,1};
    CHECK(event(&t,0x3e,update,sizeof update,0)==BTH_OK && t.parameters_known && t.matches_preference && t.error==0xff);
    CHECK(bth_le_transport_start(&t,6000)==BTH_IGNORED && t.error==0xff);
    puts("LATE_OBSERVATION_DISTINCT_FROM_REQUEST_FAILURE_PASS");
}
#endif
int main(void){setvbuf(stdout,NULL,_IONBF,0);configured_wire();
#ifndef PROBE_BEFORE
    initial_observation();configure_idle_and_observation();policies_and_replies();fallback_and_capabilities();queue_timers_and_collision();generation_and_lengths();late_observation_keeps_failure();
#endif
    printf("PASS LE preference %u checks transport_size=%zu\n",checks,sizeof(bth_le_transport_t));return 0;}
