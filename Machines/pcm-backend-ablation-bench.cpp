// Table11 / Figure7: SAME Local+PCM plan, DCF vs PPC vs layer-width Hybrid.
// No distance protocol. Fixed optimized DCF vote in ALL three modes.
// No chunk, warmup, common-clock start, algorithm changes, or key generation.
// Build via existing generic target: make pcm-backend-ablation-bench.x
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "Tools/ezOptionParser.h"
#include "Networking/Server.h"
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-reduced-depth-cong-topk.hpp"
#include "Machines/kona-mask-vote.hpp"
#include "ConstantPP/data-loader.hpp"
#include "ConstantPP/data-loader.cpp"
namespace PcmBackendAblation {
using namespace std;
using Ring=Z2<64>;using Pair=array<Ring,2>;using Plain=array<uint64_t,2>;
using Clock=chrono::steady_clock;
using Backend=KonaReducedDepthHybrid::Backend;
struct Metric {double ms=0;uint64_t bytes=0,actions=0,calls=0;};
template<class F> Metric measure(RealTwoPartyPlayer* p,KonaDcfCompare::Compare64<64>& dcf,F f){
    auto c0=p->total_comm();auto e0=dcf.get_stats().evaluate_calls;auto t0=Clock::now();
    f();auto t1=Clock::now();auto c1=p->total_comm();auto e1=dcf.get_stats().evaluate_calls;
    Metric m;m.ms=chrono::duration<double,milli>(t1-t0).count();auto d=c1-c0;
    m.bytes=d.sent;m.calls=e1-e0;for(const auto& x:d)m.actions+=x.second.rounds;
    return m;
}
Metric add(Metric a,const Metric& b){a.ms+=b.ms;a.bytes+=b.bytes;a.actions+=b.actions;a.calls+=b.calls;return a;}
Metric subtract(Metric a,const Metric& b){a.ms-=b.ms;a.bytes-=b.bytes;a.actions-=b.actions;a.calls-=b.calls;return a;}
struct Shape {
    uint64_t dcf=0,ppc=0,dcf_batches=0,ppc_batches=0,swaps=0,bytes=0,rounds=0,max_width=0;
    void record(size_t width,bool swap,Backend backend){
        bool is_ppc=backend==Backend::PCR;
        (is_ppc?ppc:dcf)+=width;++(is_ppc?ppc_batches:dcf_batches);
        swaps+=swap?width:0;bytes+=(is_ppc?70:8)*width+(swap?64*width:0);
        rounds+=(is_ppc?8:1)+(swap?1:0);max_width=max<uint64_t>(max_width,width);
    }
};
struct Layer {const char* phase;size_t index,width;bool swap;Backend backend;Metric metric;};
void print_metric(const char* phase,const string& mode,const string& profile,int trial,int party,const Metric& m,uint64_t rounds){
    cout<<"BACKEND_PHASE phase="<<phase<<" mode="<<mode<<" profile="<<profile<<" trial="<<trial<<" party="<<party
        <<" online_ms="<<m.ms<<" sent_bytes="<<m.bytes<<" transport_actions="<<m.actions
        <<" theoretical_rounds="<<rounds<<" exchange_rounds="<<m.actions/2<<" dcf_evaluate_calls="<<m.calls<<'\n';
}
int run(int argc,const char** argv){
    ez::ezOptionParser opt;
    opt.add("0",0,1,0,"Party","-p");opt.add("37000",0,1,0,"Port","-pn");opt.add("localhost",0,1,0,"Host","-h");
    opt.add("wan",0,1,0,"Actual five-profile network","--profile");
    opt.add("hybrid",0,1,0,"dcf, ppc, or hybrid; vote always DCF","--mode");
    opt.add("1",0,1,0,"Trial identity","--trial");
    opt.add("0",0,1,0,"Optional threshold assertion; must match shared profile config","--threshold");
    opt.add("/usr/src/Garnet/ConstantPP/data/16384-32",0,1,0,"Fixed input fixture","--dataset");
    opt.add("0",0,1,0,"Regression only: first N rows, zero=16384","--limit");
    opt.add("",0,0,0,"Read-only network-plan description, no communication","--describe");
    opt.add("",0,0,0,"Print usage, no communication","--help");
    opt.parse(argc,argv);
    for(auto* x:opt.unknownArgs)throw runtime_error("unknown argument: "+*x);
    for(auto* list:{&opt.firstArgs,&opt.lastArgs})for(auto* x:*list)
        if(x->size()>1&&(*x)[0]=='-'&&!isdigit(static_cast<unsigned char>((*x)[1])))throw runtime_error("unknown option: "+*x);
    if(opt.isSet("--help")){cout<<"pcm-backend-ablation-bench.x -p 0|1 --profile super-dc|lan|mid-lan|mid-wan|wan --mode dcf|ppc|hybrid [--describe]\n";return 0;}
    int party,port,trial,assert_tau,limit;string profile,mode,path,host;
    opt.get("-p")->getInt(party);opt.get("-pn")->getInt(port);opt.get("--trial")->getInt(trial);
    opt.get("--threshold")->getInt(assert_tau);opt.get("--limit")->getInt(limit);
    opt.get("--profile")->getString(profile);opt.get("--mode")->getString(mode);
    opt.get("--dataset")->getString(path);opt.get("-h")->getString(host);
    KonaReducedDepthHybrid::Policy policy;const size_t tau=policy.threshold_for(profile);
    if(port<1024||port>64000||trial<1||limit<0||(limit&& (limit<20||limit>16384))||(mode!="dcf"&&mode!="ppc"&&mode!="hybrid")||assert_tau<0)
        throw runtime_error("invalid trial/limit/mode/threshold");
    if(assert_tau&&size_t(assert_tau)!=tau)throw runtime_error("threshold assertion differs from kona-bench-config.hpp");
    policy.comparator_override=mode=="dcf"?Backend::DCF:mode=="ppc"?Backend::PCR:Backend::AUTO;
    // Ranking and threshold selection follow the same selected backend policy.
    // The voter is called directly below and ALWAYS uses optimized DCF.
    policy.matrix_backend_override=Backend::AUTO;
    KonaBenchAffinity::bind_fixed_cpu(opt.isSet("--describe")?0:party);
    KonaBenchConfig::ScopedChunking chunk_scope(false);
    const int n=limit?limit:16384,k=20,classes=4;
    auto plan=KonaReducedDepthCongTopK::prepare_reduced_plan(n,k);
    auto terminal=KonaReducedDepthCongTopK::prepare_terminal(plan,KonaReducedDepthCongTopK::Terminal::Pruned);
    Shape planned;
    for(const auto& idx:plan.fused_block_levels)if(!idx.empty())planned.record(idx.size()/2,true,policy.choose(idx.size()/2,profile));
    if(!terminal.matrix_idx.empty())planned.record(terminal.matrix_comparators,false,policy.choose_matrix(terminal.matrix_comparators,profile));
    if(terminal.input_count&&!terminal.direct&&!terminal.network)planned.record(terminal.input_count,false,policy.choose(terminal.input_count,profile));
    cout<<fixed<<setprecision(9)<<"profile="<<profile<<"\nmode="<<mode<<"\nthreshold="<<tau
        <<"\nthreshold_source=Machines/kona-bench-config.hpp\nchunking=off\ntrain_count="<<n
        <<"\nk="<<k<<"\nfeatures=32\npublic_label_domain=4\nblock_count="<<plan.plan.blocks.size()
        <<"\ncandidate_count="<<terminal.input_count<<"\nselection_comparators="<<planned.dcf+planned.ppc
        <<"\nselection_dcf_comparators="<<planned.dcf<<"\nselection_ppc_comparators="<<planned.ppc
        <<"\nselection_dcf_batches="<<planned.dcf_batches<<"\nselection_ppc_batches="<<planned.ppc_batches
        <<"\nselection_max_width="<<planned.max_width<<"\nselection_theoretical_rounds="<<planned.rounds
        <<"\nselection_expected_bytes="<<planned.bytes
        <<"\nvote_backend=DCF_OPTIMIZED\ncomparator_scope=block-cong-pcm-ranking-and-rank-mask"
        <<"\ncomparison_count_unit=logical-comparison-items-vote-excluded\nmaterial_mode=existing-L2"
        <<"\nwarmup_trials=0\ncommon_future_start=0\n";
    if(opt.isSet("--describe")){cout<<"description_only=1\n";return 0;}
    const auto setup_start=Clock::now();
    const auto data=ConstantPP::load_constantpp_dataset(path);
    if(data.database.size()!=16384||data.num_features!=32)throw runtime_error("requires stored 16384-32 fixture");
    vector<Pair> fixture(n);vector<Plain> clear(n);uint64_t checksum=1469598103934665603ULL;
    auto share=[&](uint64_t value,size_t i){Ring mask(static_cast<mp_limb_t>(7001+37*i));return party==0?mask:Ring(static_cast<mp_limb_t>(value))-mask;};
    for(int i=0;i<n;++i){
        unsigned __int128 distance=0;
        for(size_t j=0;j<data.num_features;++j){__int128 d=__int128(data.database[i][j])-__int128(data.query[j]);distance+=d*d;}
        if(distance>=(static_cast<unsigned __int128>(1)<<63)||data.labels[i]>=classes)throw runtime_error("fixture outside comparator/label domain");
        clear[i]={uint64_t(distance),data.labels[i]};fixture[i]={share(clear[i][0],2*i),share(clear[i][1],2*i+1)};
        for(auto v:clear[i])checksum=(checksum^v)*1099511628211ULL;
    }
    auto reference=clear;sort(reference.begin(),reference.end());
    if(k<n&&reference[k-1][0]==reference[k][0])throw runtime_error("tied kth boundary; refuse inconsistent selection reference");
    array<size_t,classes> counts{};for(int i=0;i<k;++i)++counts[reference[i][1]];
    const uint64_t expected_winner=distance(counts.begin(),max_element(counts.begin(),counts.end()));
    Names names;Server::start_networking(names,party,2,host,port,Names::DEFAULT_PORT);
    unique_ptr<RealTwoPartyPlayer> owner(new RealTwoPartyPlayer(names,1-party,0));auto* p=owner.get();
    KonaDcfCompare::Compare64<64> dcf(p,party);
    auto work=fixture;KonaReducedDepthCongTopK::Output<64> output;Ring winner;
    vector<Layer> layers;layers.reserve(plan.fused_block_levels.size()+3);
    auto observer=[&](const char* phase,size_t index,size_t width,auto operation){
        Backend backend;auto metric=measure(p,dcf,[&]{backend=operation();});
        bool swap=string(phase)=="block"||string(phase)=="single_cong";
        layers.push_back({phase,index,width,swap,backend,metric});
    };
    KonaMaskVote::DcfVoteStats vote_stats;Metric block,matrix,selection,vote;
    cout<<"party="<<party<<"\ntrial="<<trial<<"\nfixture_checksum="<<checksum
        <<"\nsetup_ms="<<chrono::duration<double,milli>(Clock::now()-setup_start).count()
        <<"\ndistance_execution_count=0\nmeasurement_scope=selection-through-winning-label-share\n";
    octetStream ready,peer;Ring(0).pack(ready);p->exchange_concurrent(ready,peer);
    auto total=measure(p,dcf,[&]{
        selection=measure(p,dcf,[&]{
            block=measure(p,dcf,[&]{KonaReducedDepthCongTopK::execute_blocks<64>(work,plan,profile,policy,dcf,p,observer);});
            matrix=measure(p,dcf,[&]{
                auto candidates=KonaReducedDepthCongTopK::collect_block_candidates<64>(work,plan.plan,plan.block_offsets);
                output=KonaReducedDepthCongTopK::execute_terminal<64>(move(candidates),terminal,k,profile,policy,dcf,p,observer);
            });
        });
        vote=measure(p,dcf,[&]{winner=KonaMaskVote::vote_dcf_optimized<64>(output.candidates,output.mask,classes,p,dcf,output.mask.empty(),&vote_stats,k);});
    });
    // Accounting, reconstruction and printing happen AFTER the independent end.
    Shape observed,block_shape,matrix_shape;bool accounting=true;
    for(const auto& layer:layers){
        Shape s;s.record(layer.width,layer.swap,layer.backend);
        accounting=accounting&&layer.metric.bytes==s.bytes&&layer.metric.actions==2*s.rounds&&layer.metric.calls==2*s.dcf;
        observed.record(layer.width,layer.swap,layer.backend);
        (layer.swap?block_shape:matrix_shape).record(layer.width,layer.swap,layer.backend);
    }
    accounting=accounting&&observed.dcf==planned.dcf&&observed.ppc==planned.ppc&&observed.swaps==planned.swaps;
    const uint64_t vote_bytes=8*vote_stats.comparators()+16*vote_stats.mul_items;
    accounting=accounting&&selection.bytes==planned.bytes&&selection.actions==2*planned.rounds&&selection.calls==2*planned.dcf
        &&vote.bytes==vote_bytes&&vote.actions==2*vote_stats.exchanges()&&vote_stats.logical_rounds()==vote_stats.exchanges()
        &&vote.calls==2*vote_stats.comparators()&&total.bytes==selection.bytes+vote.bytes&&total.actions==selection.actions+vote.actions;
    octetStream check_out,check_in;
    for(size_t i=0;i<output.candidates.size();++i){output.candidates[i][0].pack(check_out);output.candidates[i][1].pack(check_out);
        (output.mask.empty()?Ring(static_cast<mp_limb_t>(party)):output.mask[i]).pack(check_out);}
    winner.pack(check_out);p->exchange_concurrent(check_out,check_in);
    vector<Plain> selected;bool valid=true;
    for(size_t i=0;i<output.candidates.size();++i){Ring v,l,b;v.unpack(check_in);l.unpack(check_in);b.unpack(check_in);
        uint64_t bit=(b+(output.mask.empty()?Ring(static_cast<mp_limb_t>(party)):output.mask[i])).get_limb(0);
        valid=valid&&bit<=1;if(bit==1)selected.push_back({(v+output.candidates[i][0]).get_limb(0),(l+output.candidates[i][1]).get_limb(0)});}
    Ring other_winner;other_winner.unpack(check_in);sort(selected.begin(),selected.end());
    vector<Plain> expected(reference.begin(),reference.begin()+k);
    valid=valid&&selected==expected&&(winner+other_winner).get_limb(0)==expected_winner;
    print_metric("block_cong",mode,profile,trial,party,block,block_shape.rounds);
    print_metric("pcm_terminal",mode,profile,trial,party,matrix,matrix_shape.rounds);
    print_metric("selection_other",mode,profile,trial,party,subtract(selection,add(block,matrix)),0);
    print_metric("selection",mode,profile,trial,party,selection,planned.rounds);
    print_metric("vote",mode,profile,trial,party,vote,vote_stats.logical_rounds());
    print_metric("online_other",mode,profile,trial,party,subtract(total,add(selection,vote)),0);
    print_metric("selection_vote_online",mode,profile,trial,party,total,planned.rounds+vote_stats.logical_rounds());
    for(const auto& l:layers)cout<<"BACKEND_LAYER phase="<<l.phase<<" layer="<<l.index<<" width="<<l.width
        <<" backend="<<(l.backend==Backend::PCR?"ppc":"dcf")<<" swaps="<<(l.swap?l.width:0)
        <<" online_ms="<<l.metric.ms<<" sent_bytes="<<l.metric.bytes
        <<" theoretical_rounds="<<((l.backend==Backend::PCR?8:1)+(l.swap?1:0))<<" exchange_rounds="<<l.metric.actions/2<<'\n';
    cout<<"vote_dcf_comparators="<<vote_stats.comparators()<<"\nvote_mul_items="<<vote_stats.mul_items
        <<"\naccounting_check="<<(accounting?"PASS":"FAIL")<<"\ncorrectness="<<(valid?"PASS":"FAIL")
        <<"\noutput_scope=winning_label_secret_share\ntest_status="<<(accounting&&valid?"COMPLETED":"FAILED")<<endl;
    return accounting&&valid?0:1;
}
}
int main(int argc,const char** argv){try{return PcmBackendAblation::run(argc,argv);}catch(const exception& e){cerr<<"test_status=FAILED\nerror="<<e.what()<<endl;return 1;}}
