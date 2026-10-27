#pragma once
namespace giga_bredr { namespace detail {
// Nonthrowing callbacks; caller owns sole-producer/detached-consumer invariants.
// Preserve incoming masking: no helper mask surrounds copy/preparation, and the
// caller's ArmGuard restores its saved state before afterArm can run.
template<class IO> bool admitTxBeforeIrqArm(IO&io){
 if(!io.copyIfReady())return false;
 io.prepare();
 {
  typename IO::ArmGuard guard(io);
  io.arm();
 }
 io.afterArm();return true;
}
}}
