#include "bth_le_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); abort(); } ++checks; } while(0)
static bth_status_t event(bth_le_transport_t *t,uint8_t code,const uint8_t *p,size_t n,uint32_t owner) {
    const bth_h4_frame_t e={.type=BTH_H4_EVENT,.event={.code=code,.params={p,n}}};
    return bth_le_transport_event(t,&e,owner);
}
static void wire(bth_cmdq_t *q,uint16_t opcode,const uint8_t *p,size_t n) {
    bth_hci_flow_t f; bth_hci_flow_init(&f); uint8_t out[32];size_t written;
    CHECK(bth_cmdq_next(q,&f,out,sizeof out,&written)==BTH_OK);
    CHECK(written==n+4 && bth_read_le16(out+1)==opcode && out[3]==n && !memcmp(out+4,p,n));
}
int main(void) {
    bth_le_transport_t t; bth_cmdq_t q; bth_cmdq_init(&q); uint8_t out[16];size_t n;
    CHECK(!bth_le_transport_attach(&t,0,0x123,1,0x3f));
    CHECK(!bth_le_transport_attach(&t,42,0xf00,1,0x3f));
    CHECK(bth_le_transport_attach(&t,42,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK && !n);
    const uint8_t handle[]={0x23,1};wire(&q,0x2016,handle,sizeof handle);
    const uint8_t status[]={0,1,0x16,0x20};
    CHECK(event(&t,0x0f,status,sizeof status,41)==BTH_IGNORED);
    CHECK(event(&t,0x0f,status,sizeof status,42)==BTH_OK);
    uint8_t features[12]={4,0,0x23,1,0x3f};
    CHECK(event(&t,0x3e,features,sizeof features-1,0)==BTH_ERR_LENGTH);
    features[2]=0x24;CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_IGNORED);features[2]=0x23;
    CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    CHECK(bth_le_transport_next(&t,&q,102,out,sizeof out,&n)==BTH_OK && !n);
    const uint8_t params[]={0x23,1,18,0,24,0,0,0,0x90,1,0,0,0,0};wire(&q,0x2013,params,sizeof params);
    const uint8_t accepted[]={0,1,0x13,0x20};CHECK(event(&t,0x0f,accepted,sizeof accepted,42)==BTH_OK);
    uint8_t update[]={3,0,0x23,1,24,0,0,0,0x90,1};
    CHECK(event(&t,0x3e,update,sizeof update,0)==BTH_OK && t.interval==24 && t.latency==0 && t.timeout==400);
    CHECK(t.stage==BTH_LE_TRANSPORT_DONE && t.error==0);
    CHECK(bth_le_transport_next(&t,&q,200,out,sizeof out,&n)==BTH_IGNORED && !n);
    const uint8_t dle[]={7,0x23,1,0xfb,0,0x48,8,0xfb,0,0x48,8};
    CHECK(event(&t,0x3e,dle,sizeof dle,0)==BTH_OK && t.tx_octets==251 && t.rx_octets==251);
    // A4.0 peer needs the peripheral-to-central L2CAP fallback, not HCI2013.
    bth_cmdq_init(&q);CHECK(bth_le_transport_attach(&t,43,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,200,out,sizeof out,&n)==BTH_OK);wire(&q,0x2016,handle,2);
    CHECK(event(&t,0x0f,status,sizeof status,43)==BTH_OK);features[4]=1;
    CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    CHECK(bth_le_transport_next(&t,&q,201,out,11,&n)==BTH_ERR_CAPACITY && !n);
    CHECK(bth_le_transport_next(&t,&q,201,out,sizeof out,&n)==BTH_OK && n==12 && !q.used);
    const uint8_t request[]={0x12,1,8,0,18,0,24,0,0,0,0x90,1};CHECK(!memcmp(out,request,sizeof request));
    uint8_t response[]={0x13,2,2,0,0,0};CHECK(bth_le_transport_signal(&t,response,sizeof response)==BTH_IGNORED);
    response[1]=1;CHECK(bth_le_transport_signal(&t,response,5)==BTH_ERR_LENGTH);
    CHECK(bth_le_transport_signal(&t,response,sizeof response)==BTH_OK && t.stage==BTH_LE_TRANSPORT_UPDATE_WAIT);
    CHECK(event(&t,0x3e,update,sizeof update,0)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE);
    // Local4.0 fallback; reject/timeout are optional preference failures.
    CHECK(bth_le_transport_attach(&t,44,0x123,1,1));
    CHECK(bth_le_transport_next(&t,&q,0xfffffff0u,out,sizeof out,&n)==BTH_OK && n==12);
    CHECK(bth_le_transport_expire(&t,10)==BTH_MORE);
    CHECK(bth_le_transport_expire(&t,30000)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && t.error==0xff);
    CHECK(bth_le_transport_signal(&t,response,sizeof response)==BTH_IGNORED);
    CHECK(bth_le_transport_attach(&t,45,0x123,1,1));
    CHECK(bth_le_transport_next(&t,&q,0,out,sizeof out,&n)==BTH_OK);response[4]=1;
    CHECK(bth_le_transport_signal(&t,response,sizeof response)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE && t.error==1);
    // Queue backpressure cannot lose a feature or parameter request.
    bth_cmdq_init(&q);for(unsigned i=0;i<8;++i)CHECK(bth_cmdq_push(&q,0x0c03,NULL,0,false)==BTH_OK);
    CHECK(bth_le_transport_attach(&t,46,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,0,out,sizeof out,&n)==BTH_MORE && t.stage==BTH_LE_TRANSPORT_FEATURES);
    // Parameter support and peripheral-initiated feature support are distinct.
    // With localbit1 but no bit3, await the central's feature exchange.
    bth_cmdq_init(&q);CHECK(bth_le_transport_attach(&t,47,0x123,1,3));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK && !n && !q.used);
    features[4]=3;CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    CHECK(bth_le_transport_next(&t,&q,101,out,sizeof out,&n)==BTH_OK && !n);wire(&q,0x2013,params,sizeof params);
    bth_cmdq_init(&q);CHECK(bth_le_transport_attach(&t,48,0x123,1,3));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK && !q.used);
    features[4]=1;CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    CHECK(bth_le_transport_next(&t,&q,101,out,sizeof out,&n)==BTH_OK && n==12 && !q.used);
    CHECK(bth_le_transport_attach(&t,49,0x123,1,3));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK);
    CHECK(bth_le_transport_expire(&t,5100)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE);
    // Feature-exchange0x1a does NOT prove LL parameter requests unsupported.
    bth_cmdq_init(&q);CHECK(bth_le_transport_attach(&t,50,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK);wire(&q,0x2016,handle,2);
    const uint8_t unsupported_features[]={0x1a,1,0x16,0x20};
    CHECK(event(&t,0x0f,unsupported_features,sizeof unsupported_features,50)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE);
    CHECK(bth_le_transport_attach(&t,51,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK);
    features[1]=0x1a;CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK && t.stage==BTH_LE_TRANSPORT_DONE);features[1]=0;
    bth_cmdq_init(&q);CHECK(bth_le_transport_attach(&t,52,0x123,1,0x3f));
    CHECK(bth_le_transport_next(&t,&q,100,out,sizeof out,&n)==BTH_OK);features[4]=3;
    CHECK(event(&t,0x3e,features,sizeof features,0)==BTH_OK);
    CHECK(bth_le_transport_next(&t,&q,101,out,sizeof out,&n)==BTH_OK);
    const uint8_t unsupported_update[]={0x1a,1,0x13,0x20};
    CHECK(event(&t,0x0f,unsupported_update,sizeof unsupported_update,52)==BTH_OK && t.stage==BTH_LE_TRANSPORT_SIGNAL);
    printf("PASS LE transport: %u checks\n",checks);return 0;
}
