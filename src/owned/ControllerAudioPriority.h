#pragma once
// Single actor metadata. No packet parsing, allocation or controller I/O.
#include <stdint.h>
namespace giga_bredr { namespace detail {
struct ControllerPriorityRequest {uint32_t owner=0;uint16_t handle=0;bool high=false;};
class ControllerAudioPriority {
 uint32_t owner_=0;uint16_t handle_=0;
 bool enabled_=false,unsupported_=false,supported_=false,fault_=false;
 bool desired_=false,known_=false,high_=false,pending_=false,pending_high_=false;
 uint8_t last_status_=0xff;
public:
 enum class Response {Ignored,Applied,Unsupported,Fault};
 void reset(bool enabled){*this=ControllerAudioPriority();enabled_=enabled;}
 void attach(uint32_t owner,uint16_t handle){
  owner_=owner;handle_=handle;fault_=desired_=known_=high_=pending_=pending_high_=false;
  if(!unsupported_)last_status_=0xff;
 }
 void detach(uint32_t owner){if(owner_==owner){owner_=0;handle_=0;fault_=desired_=known_=high_=pending_=pending_high_=false;}}
 void request(bool high){desired_=high;}
 bool readyHigh()const{return owner_&&!fault_&&(!enabled_||unsupported_||(known_&&high_&&!pending_));}
 bool next(ControllerPriorityRequest& request)const{
  if(!owner_||!enabled_||unsupported_||fault_||pending_||(known_&&high_==desired_))return false;
  request={owner_,handle_,desired_};return true;
 }
 bool queued(const ControllerPriorityRequest& request){
  ControllerPriorityRequest expected;
  if(!next(expected)||request.owner!=expected.owner||request.handle!=expected.handle||request.high!=expected.high)return false;
  pending_=true;pending_high_=request.high;return true;
 }
 // Caller must prove the copied request is no longer queued AND has no sent
 // owner record. A sent tombstone can never be canceled through this method.
 void cancelled(uint32_t owner){if(owner==owner_&&pending_)pending_=false;}
 Response completed(uint32_t owner,bool command_complete,uint8_t status){
  if(!owner_||owner!=owner_||!pending_)return Response::Ignored;
  pending_=false;last_status_=status;
  if(command_complete&&status==0){known_=true;high_=pending_high_;supported_=true;return Response::Applied;}
  known_=false;
  if(command_complete&&status==1&&!supported_){unsupported_=true;return Response::Unsupported;}
  fault_=true;return Response::Fault;
 }
 uint32_t owner()const{return owner_;}
 uint16_t handle()const{return handle_;}
 bool desiredHigh()const{return desired_;}
 bool pending()const{return pending_;}
 bool pendingHigh()const{return pending_high_;}
 // Last acknowledged value only. An opposite request may still be pending;
 // readyHigh deliberately refuses START until that transaction also completes.
 bool appliedKnown()const{return known_;}
 bool appliedHigh()const{return high_;}
 bool unsupported()const{return unsupported_;}
 bool fault()const{return fault_;}
 uint8_t lastStatus()const{return last_status_;}
};
}}
