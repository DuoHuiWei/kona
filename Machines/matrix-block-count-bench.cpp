// Independent matrix-only B sweep. No secure distance or block-Cong execution.
// Synthetic Nxd data and sorted local Top-k candidates are prepared in memory
// before timers; both methods consume identical candidate shares for each B.
// FullMat: existing full ALL-pairs matrix, forced DCF including rank-mask.
// PCM: existing pruned matrix, also fixed DCF for rank AND rank-mask.
// 2026-11-06: only block-local Top-k may use hybrid comparators; it is outside
// this matrix-only experiment. No hybrid backend is used in ranking or vote.
// Both retain their existing optimized batched-DCF mask voter. Existing L2
// auxiliary material/DCF keys are reused; not a real offline-security benchmark.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Tools/ezOptionParser.h"
#include "Networking/Server.h"
#include "Networking/Player.h"
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-reduced-depth-cong-topk.hpp"
#include "Machines/kona-mask-vote.hpp"

namespace MatrixB {
using namespace std;
using Ring=Z2<64>;using Pair=array<Ring,2>;using Plain=array<uint64_t,2>;
using Clock=chrono::steady_clock;
struct Metric {double ms=0;uint64_t bytes=0,actions=0,calls=0,comparisons=0,dcf=0,pcr=0,logical=0;};
Metric add(Metric a,const Metric& b){
    a.ms+=b.ms;a.bytes+=b.bytes;a.actions+=b.actions;a.calls+=b.calls;
    a.comparisons+=b.comparisons;a.dcf+=b.dcf;a.pcr+=b.pcr;a.logical+=b.logical;return a;
}
Metric subtract(Metric a,const Metric& b){
    a.ms-=b.ms;a.bytes-=b.bytes;a.actions-=b.actions;a.calls-=b.calls;
    a.comparisons-=b.comparisons;a.dcf-=b.dcf;a.pcr-=b.pcr;a.logical-=b.logical;
    if(a.ms<0&&a.ms> -1e-8)a.ms=0;
    return a;
}
template<class F>Metric measure(RealTwoPartyPlayer* p,KonaDcfCompare::Compare64<64>& dcf,F op){
    auto c0=p->total_comm();auto d0=dcf.get_stats().evaluate_calls;auto t0=Clock::now();
    op();auto t1=Clock::now();auto c1=p->total_comm();auto d1=dcf.get_stats().evaluate_calls;
    auto delta=c1-c0;Metric m;m.ms=chrono::duration<double,milli>(t1-t0).count();m.bytes=delta.sent;m.calls=d1-d0;
    for(const auto& e:delta)m.actions+=e.second.rounds;
    if(m.actions%2)throw runtime_error("unpaired transport actions");
    return m;
}
vector<uint64_t> reveal(const vector<Ring>& shares,RealTwoPartyPlayer* p){
    octetStream a,b;for(const auto& x:shares)x.pack(a);p->exchange_concurrent(a,b);
    vector<uint64_t> out;for(const auto& x:shares){Ring peer;peer.unpack(b);out.push_back((x+peer).get_limb(0));}return out;
}
uint64_t random_word(uint64_t& state){
    uint64_t z=(state+=0x9e3779b97f4a7c15ULL);z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
    z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);
}
uint64_t checksum(const vector<Plain>& rows){uint64_t h=1469598103934665603ULL;
    for(const auto& r:rows)for(auto v:r)h=(h^v)*1099511628211ULL;
    return h;}
void print(const string& phase,const string& method,int b,int trial,int party,const string& profile,const Metric& m){
    cout<<"MATRIX_PHASE phase="<<phase<<" method="<<method<<" B="<<b<<" trial="<<trial<<" party="<<party
        <<" profile="<<profile<<" online_ms="<<m.ms<<" sent_bytes="<<m.bytes<<" transport_rounds="<<m.actions
        <<" exchange_rounds="<<m.actions/2<<" theoretical_rounds="<<m.logical<<" dcf_evaluate_calls="<<m.calls
        <<" comparators="<<m.comparisons<<" dcf_comparators="<<m.dcf<<" pcr_comparators="<<m.pcr<<" timing_kind=measured\n";
}
int run(int argc,const char** argv){
    ez::ezOptionParser opt;
    opt.add("0",0,1,0,"Party","-p");opt.add("36000",0,1,0,"Port","-pn");opt.add("localhost",0,1,0,"Host","-h");
    opt.add("2",0,1,0,"Explicit equal block count","--blocks");opt.add("1",0,1,0,"Trial","--trial");
    opt.add("wan",0,1,0,"Actual profile configured by launcher","--network-profile");
    opt.add("FullMat,PCM",0,1,0,"Method order, or FullMat/PCM for missing-point reruns","--order");opt.parse(argc,argv);
    int party,port,b,trial;string host,profile,order;
    opt.get("-p")->getInt(party);opt.get("-pn")->getInt(port);opt.get("-h")->getString(host);
    opt.get("--blocks")->getInt(b);opt.get("--trial")->getInt(trial);opt.get("--network-profile")->getString(profile);opt.get("--order")->getString(order);
    if((b!=2&&b!=4&&b!=8&&b!=16&&b!=32)||trial<1||(profile!="lan"&&profile!="wan")||
        (order!="FullMat,PCM"&&order!="PCM,FullMat"&&order!="FullMat"&&order!="PCM"))throw runtime_error("invalid B/trial/profile/order");
    constexpr int n=32768,d=32,k=5,classes=4;const int block_size=n/b;
    const auto setup0=Clock::now();uint64_t rng=20261105ULL;array<uint64_t,d> query;
    for(auto& x:query)x=random_word(rng)&1023;
    vector<Plain> all(n);
    for(auto& row:all){
        uint64_t distance=0;
        for(auto y:query){int64_t delta=int64_t(random_word(rng)&1023)-int64_t(y);distance+=uint64_t(delta*delta);}
        row={distance,random_word(rng)%classes};
    }
    auto reference=all;sort(reference.begin(),reference.end());
    if(reference[k-1][0]==reference[k][0])throw runtime_error("tied kth boundary in synthetic fixture");
    vector<size_t> counts(classes,0);for(int i=0;i<k;++i)++counts[reference[i][1]];
    const uint64_t expected_winner=distance(counts.begin(),max_element(counts.begin(),counts.end()));
    vector<Plain> candidates;for(int block=0;block<b;++block){
        auto first=all.begin()+block*block_size,last=first+block_size;
        vector<Plain> local(first,last);partial_sort(local.begin(),local.begin()+k,local.end());
        candidates.insert(candidates.end(),local.begin(),local.begin()+k);
    }
    // Same raw values AND same additive shares are reused by both branches.
    vector<Pair> shared;for(size_t i=0;i<candidates.size();++i){
        Ring mv(static_cast<mp_limb_t>(7001+74*i)),ml(static_cast<mp_limb_t>(7038+74*i));
        shared.push_back({party==0?mv:Ring(candidates[i][0])-mv,party==0?ml:Ring(candidates[i][1])-ml});
    }
    auto full=KonaReducedDepthCongTopK::prepare_matrix(vector<int>(b,k),k,KonaReducedDepthCongTopK::Terminal::Full);
    auto pcm=KonaReducedDepthCongTopK::prepare_matrix(vector<int>(b,k),k,KonaReducedDepthCongTopK::Terminal::Pruned);
    if(full.matrix_comparators!=size_t(b*k)*(b*k-1)/2||pcm.matrix_comparators!=size_t(b)*(b-1)/2*k*(k+1)/2)
        throw runtime_error("matrix plan shape mismatch");
    Names names;Server::start_networking(names,party,2,host,port,Names::DEFAULT_PORT);
    unique_ptr<RealTwoPartyPlayer> player(new RealTwoPartyPlayer(names,1-party,0));auto* p=player.get();
    KonaDcfCompare::Compare64<64> dcf(p,party);
    cout<<fixed<<setprecision(9)<<"party="<<party<<"\ntrain_count="<<n<<"\nfeatures="<<d<<"\nk="<<k
        <<"\npublic_label_domain="<<classes<<"\nblock_count="<<b<<"\nblock_size="<<block_size
        <<"\ncandidate_count="<<shared.size()<<"\nseed=20261105\nfixture_checksum="<<checksum(all)
        <<"\ncandidate_checksum="<<checksum(candidates)<<"\nquery_count=1\nnetwork_profile="<<profile
        <<"\ninput_preparation=in-memory-synthetic-Nxd-plaintext-block-topk"
        <<"\ndistance_execution_count=0\ncong_execution_count=0\nouter_partition_rule=explicit-B-equal-blocks"
        <<"\nvote_backend=DCF_OPTIMIZED\nfullmat_backend=DCF\npcm_backend=DCF\nmatrix_backend_policy=fixed-dcf-rank-and-mask"
        <<"\nfullmat_pairs=all-unordered-including-same-block\nmaterial_mode=existing-L2-test-material"
        <<"\nindependent_exchange_transport=framed-full-duplex-v1\nmeasurement_scope=matrix-terminal-and-vote-only"
        <<"\nfixture_and_setup_ms="<<chrono::duration<double,milli>(Clock::now()-setup0).count()<<'\n';
    istringstream in(order);string method;
    while(getline(in,method,',')){
        const bool is_full=method=="FullMat";KonaReducedDepthHybrid::Policy policy;
        policy.comparator_override=KonaReducedDepthHybrid::Backend::DCF;
        policy.matrix_backend_override=KonaReducedDepthHybrid::Backend::DCF;
        auto branch=shared;KonaReducedDepthCongTopK::Output<64> output;Ring winner;
        Metric rank,mask,matrix,vote;KonaMaskVote::DcfVoteStats vs;
        uint64_t expected_bytes=0,expected_exchanges=0,expected_dcf=0;
        string rank_backend,mask_backend;
        auto observer=[&](const char* phase,size_t,size_t pairs,auto op){
            KonaReducedDepthHybrid::Backend backend;
            auto metric=measure(p,dcf,[&]{backend=op();});const bool pcr=backend==KonaReducedDepthHybrid::Backend::PCR;
            const auto expected_backend=KonaReducedDepthHybrid::Backend::DCF;
            if(backend!=expected_backend)throw runtime_error("unexpected comparison backend");
            metric.comparisons=pairs;metric.pcr=pcr?pairs:0;metric.dcf=pcr?0:pairs;metric.logical=pcr?8:1;
            expected_bytes+=(pcr?70:8)*pairs;expected_exchanges+=(pcr?7:0)+(pairs+16383)/16384;
            expected_dcf+=metric.dcf;
            if(string(phase)=="matrix_rank"){rank=metric;rank_backend=KonaReducedDepthHybrid::backend_name(backend);}
            else if(string(phase)=="rank_mask"){mask=metric;mask_backend=KonaReducedDepthHybrid::backend_name(backend);}
            else throw runtime_error("unexpected non-matrix stage");
        };
        octetStream a,z;Ring(0).pack(a);p->exchange_concurrent(a,z);
        auto total=measure(p,dcf,[&]{
            matrix=measure(p,dcf,[&]{output=KonaReducedDepthCongTopK::execute_terminal<64>(std::move(branch),is_full?full:pcm,k,profile,policy,dcf,p,observer);});
            vote=measure(p,dcf,[&]{winner=KonaMaskVote::vote_dcf_optimized<64>(output.candidates,output.mask,classes,p,dcf,false,&vs,k);});
        });
        const auto shape=add(rank,mask);matrix.comparisons=shape.comparisons;matrix.dcf=shape.dcf;matrix.pcr=shape.pcr;matrix.logical=shape.logical;
        vote.comparisons=vs.comparators();vote.dcf=vote.comparisons;vote.logical=6; // C=4, nonempty secret mask.
        total.comparisons=matrix.comparisons+vote.comparisons;total.dcf=matrix.dcf+vote.dcf;total.pcr=matrix.pcr;total.logical=matrix.logical+vote.logical;
        auto matrix_other=subtract(subtract(matrix,rank),mask);auto online_other=subtract(subtract(total,matrix),vote);
        bool accounting=matrix.bytes==expected_bytes&&matrix.actions==2*expected_exchanges&&matrix.calls==2*expected_dcf&&
            vote.bytes==8*vs.comparators()+16*vs.mul_items&&vote.actions==2*vs.exchanges()&&vote.calls==2*vs.comparators()&&
            matrix_other.bytes==0&&matrix_other.actions==0&&matrix_other.calls==0&&online_other.bytes==0&&online_other.actions==0&&online_other.calls==0;
        vector<Ring> flat;for(const auto& row:output.candidates){flat.push_back(row[0]);flat.push_back(row[1]);}
        const auto opened=reveal(flat,p),opened_mask=reveal(output.mask,p);vector<Plain> chosen;bool valid=true;
        for(size_t i=0;i<opened_mask.size();++i){if(opened_mask[i]==1)chosen.push_back({opened[2*i],opened[2*i+1]});else if(opened_mask[i]!=0)valid=false;}
        sort(chosen.begin(),chosen.end());const auto revealed_winner=reveal({winner},p)[0];
        valid=valid&&chosen==vector<Plain>(reference.begin(),reference.begin()+k)&&revealed_winner==expected_winner;
        print("rank_matrix",method,b,trial,party,profile,rank);print("rank_mask",method,b,trial,party,profile,mask);
        print("matrix_other",method,b,trial,party,profile,matrix_other);print("matrix",method,b,trial,party,profile,matrix);
        print("vote",method,b,trial,party,profile,vote);print("online_other",method,b,trial,party,profile,online_other);
        print("matrix_vote_online",method,b,trial,party,profile,total);
        cout<<"MATRIX_CASE method="<<method<<" B="<<b<<" trial="<<trial<<" profile="<<profile<<" party="<<party
            <<" rank_backend="<<rank_backend<<" mask_backend="<<mask_backend<<" rank_pairs="<<rank.comparisons
            <<" mask_pairs="<<mask.comparisons<<" candidate_checksum="<<checksum(candidates)<<" selected="<<chosen.size()
            <<" winner="<<revealed_winner<<" accounting_check="<<(accounting?"PASS":"FAIL")<<" correctness="<<(valid?"PASS":"FAIL")<<endl;
        if(!accounting||!valid)throw runtime_error("matrix sweep validation failed");
    }
    cout<<"accounting_check=PASS\ncorrectness=PASS\ntest_status=COMPLETED\n";return 0;
}
}
int main(int argc,const char** argv){try{
    int party=-1;for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="-p")party=std::stoi(argv[i+1]);
    KonaBenchAffinity::bind_fixed_cpu(party);return MatrixB::run(argc,argv);
}catch(const std::exception& e){std::cerr<<"test_status=FAILED\nerror="<<e.what()<<std::endl;return 1;}}
