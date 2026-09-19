#include "Adapters/android/audio/AndroidAudioDriver.h"
#include <cassert>
#include <cmath>
#include <iostream>
struct AAudioStream {
 int direction=0, rate=44100, format=2, channels=2;
 AAudioStream_dataCallback data=nullptr;
 AAudioStream_errorCallback error=nullptr;
 void *owner=nullptr;
 bool started=false;
};
struct AAudioStreamBuilder : AAudioStream {};
static AAudioStream *input=nullptr, *output=nullptr;
static bool wrongRate=false, failStart=false;
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b) { *b=new AAudioStreamBuilder; return 0; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b,int32_t v) { b->direction=v; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b,int32_t v) { b->format=v; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b,int32_t v) { b->rate=v; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b,int32_t v) { b->channels=v; }
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *,int32_t) {}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *,int32_t) {}
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b,AAudioStream_dataCallback f,void *o) { b->data=f;b->owner=o; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b,AAudioStream_errorCallback f,void *) { b->error=f; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b,AAudioStream **s) {
 *s=new AAudioStream(*b); if(wrongRate)(*s)->rate=48000;
 (b->direction==AAUDIO_DIRECTION_INPUT?input:output)=*s; return 0;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { delete b; return 0; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) { if(failStart)return -1;s->started=true;return 0; }
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) { s->started=false;return 0; }
aaudio_result_t AAudioStream_close(AAudioStream *s) { if(s==input)input=nullptr;if(s==output)output=nullptr;delete s;return 0; }
int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->rate; }
int32_t AAudioStream_getFormat(AAudioStream *s) { return s->format; }
int32_t AAudioStream_getChannelCount(AAudioStream *s) { return s->channels; }
int main() {
 AudioSettings settings{}; AndroidAudioDriver driver(settings);
 assert(driver.InitDriver()); assert(!input); assert(driver.StartDriver());
 short pcm[]={16384,-16384,32767,-32768}; driver.AddBuffer(pcm,2);
 std::array<float,600> out{}; output->data(output,output->owner,out.data(),300);
 assert(out[0]==0.5f && out[1]==-0.5f && out[3]==-1.0f && out[4]==0);
 assert(std::abs(driver.GetStreamTime()-300.0/44100)<1e-9);
 std::array<int16_t,5> capture{}; assert(driver.BeginInputCapture(capture));
 float block[]={0.25f,-0.25f}; input->data(input,input->owner,block,2);
 assert(driver.CapturedInputFrames()==2);
 input->data(input,input->owner,block,2); assert(driver.CapturedInputFrames()==4);
 assert(input->data(input,input->owner,block,2)==AAUDIO_CALLBACK_RESULT_STOP);
 assert(driver.CapturedInputFrames()==5 && !driver.IsInputCapturing());
 assert(capture[0]==capture[2] && capture[2]==capture[4] && capture[1]==capture[3]);
 driver.EndInputCapture(); assert(!input);
 assert(driver.BeginInputCapture(capture));
 input->error(input,input->owner,-1); assert(!driver.IsInputCapturing());
 driver.SetSuspended(true); assert(!input && !output);
 assert(!driver.BeginInputCapture(capture));
 driver.SetSuspended(false); assert(output && !input);
 driver.SetSuspended(true); failStart=true;
 driver.SetSuspended(false); assert(!output);
 failStart=false; driver.PumpProducer(); assert(output && output->started);
 output->error(output,output->owner,-1); driver.PumpProducer(); assert(output);
 driver.StopDriver(); wrongRate=true;
 assert(!driver.StartDriver() && !output); assert(!driver.BeginInputCapture(capture) && !input);
 std::cout << "Android audio driver: output, contiguous recording, capacity, suspend, disconnect, format checks passed\n";
}
