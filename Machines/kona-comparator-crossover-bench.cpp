// Four comparator calibration workloads; no KNN/distance/vote. Existing L2
// material only. All preparation, sync, validation and printing are untimed.
// Benchmark-only one-shot batches: DCF=m, PPC B2A=m, swap selectors=2m.
// Global protocol chunk constants and formal Hybrid thresholds stay unchanged.
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <chrono>
#include "Tools/ezOptionParser.h"
#include "Networking/Server.h"
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-cong-kona-adapter.hpp"
using namespace std;
using Ring=Z2<64>;using Pair=array<Ring,2>;using Plain=array<uint64_t,2>;
using Clock=chrono::steady_clock;
struct Snap {Clock::time_point time;NamedCommStats comm;};
Snap snap(RealTwoPartyPlayer* p){return {Clock::now(),p->total_comm()};}
uint64_t actions(const NamedCommStats& c){uint64_t v=0;for(const auto& e:c)v+=e.second.rounds;return v;}
// Existing full-duplex TimeScope charges its overlapping send/receive ONCE.
// Residual includes local computation, packing/allocation and scheduling.
double communication_ms(NamedCommStats c){double v=0;for(auto& e:c)v+=e.second.timer.elapsed();return v*1000;}
vector<uint64_t> open_values(const vector<Ring>& v,RealTwoPartyPlayer* p){
 octetStream a,b;for(const auto& x:v)x.pack(a);p->exchange_concurrent(a,b);
 vector<uint64_t> out;for(const auto& x:v){Ring y;y.unpack(b);out.push_back((x+y).get_limb(0));}return out;
}
int main(int argc,const char** argv){try{
 ez::ezOptionParser opt;
 opt.add("0",0,1,0,"Party","-p");opt.add("39000",0,1,0,"Port","-pn");
 opt.add("localhost",0,1,0,"Host","-h");opt.add("64",0,1,0,"Comparisons per layer","--width");
 opt.add("1",0,1,0,"Trial","--trial");opt.add("lan",0,1,0,"Profile label","--profile");
 opt.add("dcf,pcr",0,1,0,"Backend order","--order");
 opt.add("0",0,1,0,"Buffer per-exchange wall timestamps; print after timing","--trace-exchanges");
 opt.add("compare1,compare3",0,1,0,"Selected workloads; pure comparison by default","--tests");opt.parse(argc,argv);
 if(!opt.unknownArgs.empty())throw runtime_error("unknown option");
 for(auto list:{&opt.firstArgs,&opt.lastArgs})for(auto v:*list)if(v->size()>1&&(*v)[0]=='-')throw runtime_error("unknown option: "+*v);
 int party,width,port,trial,trace_exchanges;string host,profile,order,test_names;
 opt.get("--trace-exchanges")->getInt(trace_exchanges);
 if(trace_exchanges!=0&&trace_exchanges!=1)throw runtime_error("invalid trace flag");
 opt.get("-p")->getInt(party);opt.get("--width")->getInt(width);opt.get("-pn")->getInt(port);opt.get("--trial")->getInt(trial);
 opt.get("--tests")->getString(test_names);
 vector<string> tests;string token;istringstream selected(test_names);
 while(getline(selected,token,',')){
  if((token!="compare1"&&token!="compare3"&&token!="swap1"&&token!="swap3")||find(tests.begin(),tests.end(),token)!=tests.end())throw runtime_error("invalid/duplicate test");
  tests.push_back(token);
 }
 if(tests.empty())throw runtime_error("empty test selection");
 opt.get("-h")->getString(host);opt.get("--profile")->getString(profile);opt.get("--order")->getString(order);
 if(width<1||width>1000000||trial<1||(order!="dcf,pcr"&&order!="pcr,dcf"))throw runtime_error("invalid width/trial/order");
 if(profile!="lan"&&profile!="wan"&&profile!="super-dc"&&profile!="mid-lan"&&profile!="mid-wan"&&profile!="native")throw runtime_error("invalid profile");
 KonaBenchAffinity::bind_fixed_cpu(party);
 Names names;Server::start_networking(names,party,2,host,port,Names::DEFAULT_PORT);
 unique_ptr<RealTwoPartyPlayer> owner(new RealTwoPartyPlayer(names,1-party,0));auto* p=owner.get();
 vector<RealTwoPartyPlayer::ExchangeTraceEvent> exchange_events;exchange_events.reserve(32);
 KonaDcfCompare::Compare64<64> dcf(p,party);
 vector<Plain> plain(2*width);vector<Pair> fixture(plain.size());
 mt19937_64 rng(20261107);
 for(size_t i=0;i<plain.size();++i){plain[i]={rng()%1000000,i};if(i%13==1)plain[i][0]=plain[i-1][0];
  for(int j=0;j<2;++j){Ring mask(static_cast<mp_limb_t>(7001+37*(2*i+j)));fixture[i][j]=party==0?mask:Ring(plain[i][j])-mask;}}
 array<vector<int>,3> indices;
 for(auto& idx:indices){idx.resize(plain.size());iota(idx.begin(),idx.end(),0);}
 rotate(indices[1].begin(),indices[1].begin()+1,indices[1].end());
 shuffle(indices[2].begin(),indices[2].end(),rng);
 cout<<fixed<<setprecision(9)<<"global_dcf_chunk="<<KonaBenchConfig::DCF_CHUNK<<"\nglobal_mul_chunk="<<KonaBenchConfig::MUL_CHUNK
     <<"\nchunking=disabled_for_this_benchmark\ndcf_batch_items="<<width<<"\nppc_b2a_batch_items="<<width<<"\nswap_selector_batch_items="<<2ULL*width
     <<"\nmaterial_mode=existing-L2\nthree_layer_pairing=fixed_disjoint_permutations\n";
 vector<string> backends=order=="dcf,pcr"?vector<string>{"dcf","pcr"}:vector<string>{"pcr","dcf"};
 cout<<"selected_tests="<<test_names<<"\nswap_enabled="<<(find(tests.begin(),tests.end(),"swap1")!=tests.end()||find(tests.begin(),tests.end(),"swap3")!=tests.end())<<'\n';
 rotate(tests.begin(),tests.begin()+(trial-1)%tests.size(),tests.end());
 for(const auto& test:tests)for(const auto& backend:backends){
  bool swap_records=test.find("swap")==0;int layers=test.back()=='3'?3:1;
  auto work=fixture;auto expected=plain;vector<Ring> bits(plain.size());
  vector<uint64_t> expected_bits(plain.size());
  // Plain simulation also verifies label routing through all dependent layers.
  for(int layer=0;layer<layers;++layer){const auto& idx=indices[swap_records?layer:0];
   for(int j=0;j<width;++j){int l=idx[2*j],r=idx[2*j+1];bool b=expected[l][0]>expected[r][0];
    expected_bits[2*j]=expected_bits[2*j+1]=b;if(swap_records&&b)std::swap(expected[l],expected[r]);}}
  octetStream sync_a,sync_b;sync_a.store_int(1,1);p->exchange_concurrent(sync_a,sync_b);
  exchange_events.clear();
  p->exchange_trace=trace_exchanges?&exchange_events:nullptr;
  dcf.reset_stats();array<Snap,7> marks;marks[0]=snap(p);int count=0;
  for(int layer=0;layer<layers;++layer){const auto& idx=indices[swap_records?layer:0];
   if(backend=="dcf")dcf.compare_in_vec(work,idx,bits,true,static_cast<size_t>(width));
   else KonaPcrCompare::pcr_compare_in_vec_l2(work,idx,bits,true,p,static_cast<size_t>(width));
   marks[++count]=snap(p);
   if(swap_records){KonaCongKonaAdapter::ss_vec_kona_l2(work,idx,bits,p,idx.size());marks[++count]=snap(p);}}
  p->exchange_trace=nullptr;
  // No detailed counter traversal inside online timing.
  auto delta=marks[count].comm-marks[0].comm;
  uint64_t exchanges_per_compare=backend=="dcf"?1:8;
  uint64_t predicted=layers*(exchanges_per_compare+(swap_records?1:0));
  uint64_t bytes=uint64_t(layers)*width*((backend=="dcf"?8:70)+(swap_records?64:0));
  bool accounting=delta.sent==bytes&&actions(delta)==2*predicted&&dcf.get_stats().evaluate_calls==(backend=="dcf"?2LL*layers*width:0);
  bool valid=open_values(bits,p)==expected_bits;
  vector<Ring> flat;for(const auto& row:work){flat.push_back(row[0]);flat.push_back(row[1]);}
  auto opened=open_values(flat,p);for(size_t i=0;i<expected.size();++i)valid=valid&&opened[2*i]==expected[i][0]&&opened[2*i+1]==expected[i][1];
  auto emit=[&](string phase,int layer,const Snap& a,const Snap& b,uint64_t logical){auto c=b.comm-a.comm;
   const double elapsed=chrono::duration<double,milli>(b.time-a.time).count();
   const double comm=communication_ms(c),compute=elapsed-comm;
   if(compute < -0.000001)throw runtime_error("communication timer exceeds online interval");
   cout<<"CROSSOVER phase="<<phase<<" layer="<<layer<<" test="<<test<<" backend="<<backend<<" profile="<<profile<<" width="<<width<<" trial="<<trial<<" party="<<party
       <<" online_ms="<<elapsed<<" communication_ms="<<comm<<" compute_ms="<<compute<<" sent_bytes="<<c.sent
       <<" theoretical_rounds="<<logical<<" exchange_rounds="<<actions(c)/2<<" transport_actions="<<actions(c)<<'\n';};
  for(int layer=0,i=0;layer<layers;++layer){emit("compare",layer,marks[i],marks[i+1],backend=="dcf"?1:8);++i;
   if(swap_records){emit("swap",layer,marks[i],marks[i+1],1);++i;}}
  emit("total",-1,marks[0],marks[count],layers*((backend=="dcf"?1:8)+(swap_records?1:0)));
  for(size_t i=0;i<exchange_events.size();++i){const auto& e=exchange_events[i];
   cout<<"EXCHANGE_TRACE test="<<test<<" backend="<<backend<<" party="<<party<<" round="<<i+1
       <<" sent_bytes="<<e.bytes<<" begin_steady_ns="<<e.begin_ns<<" end_steady_ns="<<e.end_ns
       <<" communication_ms="<<double(e.end_ns-e.begin_ns)/1000000.0<<'\n';}
  cout<<"CHECK test="<<test<<" backend="<<backend<<" correctness="<<(valid?"PASS":"FAIL")<<" accounting="<<(accounting?"PASS":"FAIL")<<" dcf_evaluate_calls="<<dcf.get_stats().evaluate_calls<<endl;
  if(!valid||!accounting)throw runtime_error("crossover validation failed");
 }
 return 0;
}catch(const exception& e){cerr<<"error="<<e.what()<<endl;return 1;}}
