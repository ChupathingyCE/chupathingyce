#include "harness.h"
typedef unsigned long DWORD;
typedef void *LPDIRECTSOUNDSTREAM;
#define CALLBACK
struct sound_permutation { int unused; };
#include "types.inc"
/* (only what the two functions read of dsound_globals) */
static struct { short actual_channel_count; boolean paused; } dsound_globals = { 1, FALSE };
static struct sound_channel channel;
static struct sound_permutation permutation;
static int refills, unlocks, stops;
static struct sound_channel *channel_get(short index) { CHECK(index==0,"one channel"); return &channel; }
#define sound_cache_sound_hardware_unlock(packet) ((void)(packet), unlocks++)
#define interrupt_time_error(r, message) CHECK(FALSE, "%s", message)
static void dsound_channel_fill(short index) {
 (void)index; if (channel.playing_permutation) { refills++; channel.packet_count++; }
}
static void CALLBACK dsound_channel_callback(void *, void *, unsigned long);
/* the SDL backend's flush: an already mixed head completes as SUCCESS, the rest as FLUSHED, synchronously */
static void DirectSoundStopStream(LPDIRECTSOUNDSTREAM stream) {
 boolean first=TRUE; (void)stream; stops++;
 while (channel.packet_count>0) {
  dsound_channel_callback(NULL, NULL, first ? XMEDIAPACKET_STATUS_SUCCESS : XMEDIAPACKET_STATUS_FLUSHED);
  first=FALSE;
 }
}
#pragma clang diagnostic ignored "-Wvoid-pointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#include "under_test.inc"
static void reset(short state) {
 memset(&channel,0,sizeof(channel)); channel.state=state; channel.packet_count=3;
 channel.playing_permutation=channel.queued_permutation=&permutation;
 dsound_globals.paused=FALSE; refills=unlocks=stops=0;
}
int main(int argc,char **argv) {
 CHECK(argc==2,"case name"); const char *case_name=argv[1];
 CASE("stop") {
  reset(_sound_channel_playing); channel_stop(0);
  CHECK(refills==0,"the stop's callbacks queued %d more packets",refills);
  CHECK(unlocks==3 && channel.packet_count==0,"%d unlocked, %d left",unlocks,channel.packet_count);
  CHECK(!channel.playing_permutation && !channel.queued_permutation,"permutations cleared");
  CHECK(channel.state==_sound_channel_idle,"idle after the stop"); return 0;
 }
 CASE("idle-backlog") {
  reset(_sound_channel_idle); channel_stop(0);
  CHECK(stops==1 && channel.packet_count==0,"an idle channel's packets drained (%d left)",channel.packet_count);
  channel_stop(0); CHECK(stops==1,"a second stop is a no-op"); return 0;
 }
 CASE("paused") {
  reset(_sound_channel_playing); dsound_globals.paused=TRUE; channel_stop(0);
  CHECK(refills==0 && channel.packet_count==0,"paused stop drains without a refill"); return 0;
 }
 CASE("refill") {
  reset(_sound_channel_playing); dsound_channel_callback(NULL,NULL,XMEDIAPACKET_STATUS_SUCCESS);
  CHECK(refills==1 && channel.packet_count==3,"a finished packet is replaced (%d refills)",refills); return 0;
 }
 CHECK(FALSE,"unknown case");
}
