#pragma once
#include "windows.h"
#include <cstddef>
#include <deque>
#include <vector>
#include <stdexcept>
using MMRESULT=UINT;
using HWAVEOUT=void*;
using HWAVEIN=void*;
inline constexpr MMRESULT MMSYSERR_NOERROR=0;
inline constexpr UINT WAVE_MAPPER=static_cast<UINT>(-1);
inline constexpr WORD WAVE_FORMAT_PCM=1;
inline constexpr DWORD CALLBACK_EVENT=0x50000;
inline constexpr DWORD WHDR_DONE=1,WHDR_PREPARED=2,WHDR_INQUEUE=16;
struct WAVEFORMATEX { WORD wFormatTag{},nChannels{};DWORD nSamplesPerSec{},nAvgBytesPerSec{};WORD nBlockAlign{},wBitsPerSample{},cbSize{}; };
struct WAVEHDR { LPSTR lpData{};DWORD dwBufferLength{},dwBytesRecorded{};DWORD_PTR dwUser{};DWORD dwFlags{},dwLoops{};WAVEHDR* lpNext{};DWORD_PTR reserved{}; };
struct WAVEINCAPSA { char szPname[32]{}; };
struct WAVEOUTCAPSA { char szPname[32]{}; };
namespace winmm_test {
enum class Failure { None,Prepare,Write,Add,Start,Restart,Stop,Release,Close };
struct State {
    Failure failure=Failure::None;
    bool recording=false,paused=false,started=false,timeout=false,gap=false,bad_capture_length=false;
    unsigned events=0,opened=0,prepared=0,resets=0,writes=0,initial_queue=0;
    std::size_t captured=0;
    std::deque<WAVEHDR*> pending;
    std::vector<std::int16_t> played;
};
inline State state;
inline bool fail(Failure failure) { if(state.failure!=failure) return false;state.failure=Failure::None;return true; }
inline void reset() { state=State{}; }
inline MMRESULT prepare(WAVEHDR* h) { if(fail(Failure::Prepare)) return 1;h->dwFlags|=WHDR_PREPARED;++state.prepared;return 0; }
inline MMRESULT release(WAVEHDR* h) {
    if(fail(Failure::Release)) return 1;
    if(h->dwFlags&WHDR_INQUEUE) throw std::runtime_error("buffer released while queued");
    h->dwFlags&=~WHDR_PREPARED;--state.prepared;return 0;
}
inline MMRESULT reset_device() {
    ++state.resets;
    for(auto* h:state.pending) {h->dwFlags&=~WHDR_INQUEUE;h->dwFlags|=WHDR_DONE;}
    state.pending.clear();return 0;
}
inline MMRESULT close() {
    if(fail(Failure::Close)) return 1;
    if(!state.pending.empty() || state.prepared) throw std::runtime_error("closing with live audio buffers");
    --state.opened;return 0;
}
inline MMRESULT queue(WAVEHDR* h,bool recording) {
    if(fail(recording?Failure::Add:Failure::Write)) return 1;
    if(!(h->dwFlags&WHDR_PREPARED) || (h->dwFlags&WHDR_INQUEUE)) throw std::runtime_error("invalid audio queue lifecycle");
    if(state.started && !state.paused && state.pending.empty()) state.gap=true;
    h->dwFlags&=~WHDR_DONE;h->dwFlags|=WHDR_INQUEUE;
    state.pending.push_back(h);
    if(!recording) {++state.writes;const auto* pcm=reinterpret_cast<std::int16_t*>(h->lpData);state.played.insert(state.played.end(),pcm,pcm+h->dwBufferLength/2);}
    return 0;
}
}
inline HANDLE CreateEventA(void*,BOOL,BOOL,const char*) {++winmm_test::state.events;return reinterpret_cast<HANDLE>(1);}
inline BOOL CloseHandle(HANDLE) {--winmm_test::state.events;return 1;}
inline DWORD WaitForSingleObject(HANDLE,DWORD timeout) {
    auto& s=winmm_test::state;
    if(timeout==0 || timeout>3000) throw std::runtime_error("unbounded audio wait");
    if(s.timeout) return WAIT_TIMEOUT;
    if(s.pending.empty()) throw std::runtime_error("wait with no pending audio");
    auto* h=s.pending.front();s.pending.pop_front();
    h->dwFlags&=~WHDR_INQUEUE;h->dwFlags|=WHDR_DONE;
    if(s.recording) {
        auto* pcm=reinterpret_cast<std::int16_t*>(h->lpData);
        for(std::size_t i=0;i<h->dwBufferLength/2;++i) pcm[i]=static_cast<std::int16_t>((s.captured++)%32768);
        h->dwBytesRecorded=h->dwBufferLength+(s.bad_capture_length?2u:0u);
    }
    return WAIT_OBJECT_0;
}
inline UINT waveInGetNumDevs(){return 0;}
inline UINT waveOutGetNumDevs(){return 0;}
inline MMRESULT waveInGetDevCapsA(UINT,WAVEINCAPSA*,UINT){return 0;}
inline MMRESULT waveOutGetDevCapsA(UINT,WAVEOUTCAPSA*,UINT){return 0;}
inline MMRESULT waveOutOpen(HWAVEOUT* h,UINT,const WAVEFORMATEX*,DWORD_PTR,DWORD_PTR,DWORD flags) {
    if(flags!=CALLBACK_EVENT) throw std::runtime_error("event callback required");
    *h=reinterpret_cast<HWAVEOUT>(1);++winmm_test::state.opened;winmm_test::state.recording=false;return 0;
}
inline MMRESULT waveInOpen(HWAVEIN* h,UINT,const WAVEFORMATEX* f,DWORD_PTR c,DWORD_PTR d,DWORD flags) {
    auto result=waveOutOpen(h,0,f,c,d,flags);winmm_test::state.recording=true;return result;
}
inline MMRESULT waveOutPrepareHeader(HWAVEOUT,WAVEHDR* h,UINT){return winmm_test::prepare(h);}
inline MMRESULT waveInPrepareHeader(HWAVEIN,WAVEHDR* h,UINT){return winmm_test::prepare(h);}
inline MMRESULT waveOutUnprepareHeader(HWAVEOUT,WAVEHDR* h,UINT){return winmm_test::release(h);}
inline MMRESULT waveInUnprepareHeader(HWAVEIN,WAVEHDR* h,UINT){return winmm_test::release(h);}
inline MMRESULT waveOutReset(HWAVEOUT){return winmm_test::reset_device();}
inline MMRESULT waveInReset(HWAVEIN){return winmm_test::reset_device();}
inline MMRESULT waveOutClose(HWAVEOUT){return winmm_test::close();}
inline MMRESULT waveInClose(HWAVEIN){return winmm_test::close();}
inline MMRESULT waveOutPause(HWAVEOUT){winmm_test::state.paused=true;return 0;}
inline MMRESULT waveOutRestart(HWAVEOUT){if(winmm_test::fail(winmm_test::Failure::Restart))return 1;auto& s=winmm_test::state;s.paused=false;s.started=true;s.initial_queue=static_cast<unsigned>(s.pending.size());return 0;}
inline MMRESULT waveInStart(HWAVEIN){if(winmm_test::fail(winmm_test::Failure::Start))return 1;auto& s=winmm_test::state;s.started=true;s.initial_queue=static_cast<unsigned>(s.pending.size());return 0;}
inline MMRESULT waveInStop(HWAVEIN){return winmm_test::fail(winmm_test::Failure::Stop)?1u:0u;}
inline MMRESULT waveOutWrite(HWAVEOUT,WAVEHDR* h,UINT){return winmm_test::queue(h,false);}
inline MMRESULT waveInAddBuffer(HWAVEIN,WAVEHDR* h,UINT){return winmm_test::queue(h,true);}
