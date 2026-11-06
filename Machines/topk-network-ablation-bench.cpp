// Independent Figure-5 prototype. Existing knn-bench.cpp, launchers, Makefile,
// and protocol modules are unchanged. Not a production-security experiment.
// Build with the existing generic Makefile rule:
//   make -j8 topk-network-ablation-bench.x
// A launcher must configure/record the actual WAN; --network-profile is a label.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Tools/ezOptionParser.h"
#include "Networking/Server.h"
#include "Networking/Player.h"
#include "Machines/kona-bench-affinity.hpp"
#include "Machines/kona-dcf-compare.hpp"
#include "Machines/kona-reduced-depth-cong-topk.hpp"
#include "Machines/kona-mask-vote.hpp"
#include "ConstantPP/data-loader.hpp"
// Generic standalone target links no ConstantPP object files. Embed only this
// existing data component; never include or dispatch through another bench.
#include "ConstantPP/data-loader.cpp"

namespace ablation_helpers {
using namespace std;
using Ring=Z2<64>;
using Pair=array<Ring,2>;
using Clock=chrono::steady_clock;
RealTwoPartyPlayer* player=nullptr;
KonaDcfCompare::Compare64<64>* cached_dcf=nullptr;
int playerno=-1;
struct PhaseStats {
    double total_seconds=0;
    uint64_t sent_bytes=0,transport_events=0,transport_actions=0;
};
struct SharedMetric {PhaseStats comm;uint64_t dcf_calls=0;};
PhaseStats phase_delta(Clock::time_point a,Clock::time_point b,
        const NamedCommStats& c0,const NamedCommStats& c1) {
    const auto delta=c1-c0;PhaseStats m;
    m.total_seconds=chrono::duration<double>(b-a).count();m.sent_bytes=delta.sent;
    for(const auto& e:delta)m.transport_events+=e.second.rounds;
    if(m.transport_events%2)throw runtime_error("unpaired transport actions");
    m.transport_actions=m.transport_events/2;return m;
}
template<class F>
SharedMetric measure_shared(RealTwoPartyPlayer* p,KonaDcfCompare::Compare64<64>& dcf,F op) {
    const auto c0=p->total_comm();const auto calls0=dcf.get_stats().evaluate_calls;const auto t0=Clock::now();
    op();
    const auto t1=Clock::now();const auto c1=p->total_comm();const auto calls1=dcf.get_stats().evaluate_calls;
    SharedMetric m;m.comm=phase_delta(t0,t1,c0,c1);m.dcf_calls=calls1-calls0;return m;
}
void sync_outside_timer(RealTwoPartyPlayer* p){octetStream a,b;Ring(0).pack(a);p->exchange_concurrent(a,b);}
vector<uint64_t> reveal_values(const vector<Ring>& v,RealTwoPartyPlayer* p){
    octetStream a,b;for(const auto& x:v)x.pack(a);p->exchange_concurrent(a,b);
    vector<uint64_t> out;for(const auto& x:v){Ring peer;peer.unpack(b);out.push_back((x+peer).get_limb(0));}return out;
}
struct RevealedPair {uint64_t value,label;};
vector<RevealedPair> reveal_pairs(const vector<Pair>& v,RealTwoPartyPlayer* p){
    vector<Ring> flat;for(const auto& x:v){flat.push_back(x[0]);flat.push_back(x[1]);}
    const auto opened=reveal_values(flat,p);vector<RevealedPair> out;
    for(size_t i=0;i<opened.size();i+=2)out.push_back({opened[i],opened[i+1]});
    return out;
}
vector<int> parse_ks(const string& text){
    istringstream in(text);string token;vector<int> out;
    while(getline(in,token,',')){size_t used=0;int k=stoi(token,&used);
        if(used!=token.size()||k<1||find(out.begin(),out.end(),k)!=out.end())throw runtime_error("invalid k list");
        out.push_back(k);}
    if(out.empty())throw runtime_error("empty k list");
    return out;
}

// Same graph/leftover and batched vote logic as the existing Kona
// cached baseline in knn-bench.cpp. Only the benchmark glue is local; DCF,
// swap and multiplication still call the shared original protocol modules.
struct CachedLevel {vector<int> idx;vector<Ring> bits;};
using CachedPlan=vector<CachedLevel>;
CachedPlan prepare_cached_top_one(int size){
    vector<int> idx(size);iota(idx.begin(),idx.end(),0);int leftover=-1;CachedPlan plan;
    while(idx.size()+(leftover!=-1)>1){
        if(idx.size()%2){if(leftover==-1){leftover=idx.back();idx.pop_back();}else{idx.push_back(leftover);leftover=-1;}}
        plan.push_back({idx,vector<Ring>(idx.size())});vector<int> next;
        for(size_t i=1;i<idx.size();i+=2)next.push_back(idx[i]);
        idx.swap(next);
    }return plan;
}
uint64_t cached_rounds(const CachedPlan& plan){uint64_t n=0;for(const auto& l:plan){
    size_t pairs=l.idx.size()/2;n+=KonaBenchConfig::chunks(pairs,KonaBenchConfig::DCF_CHUNK)+KonaBenchConfig::chunks(2*pairs,KonaBenchConfig::MUL_CHUNK);}return n;}
void cached_top_one(vector<Pair>& rows,CachedPlan& plan,bool minimum){
    for(auto& l:plan){cached_dcf->compare_in_vec(rows,l.idx,l.bits,!minimum);
        KonaCongKonaAdapter::ss_vec_kona_l2<64>(rows,l.idx,l.bits,player);}
}
void cached_label_counts(const vector<Ring>& labels,vector<Pair>& counts){
    const size_t k=labels.size();vector<int> idx;idx.reserve(2*k*k);
    for(size_t i=0;i<k;++i)for(size_t j=0;j<k;++j){idx.push_back(i);idx.push_back(j);}
    vector<Ring> bits(idx.size()),left(k*k),right(k*k),equal;
    cached_dcf->compare_in_vec(labels,idx,bits,true);const Ring one(static_cast<mp_limb_t>(playerno));
    for(size_t i=0;i<k;++i)for(size_t j=0;j<k;++j){left[i*k+j]=one-bits[2*(i*k+j)];right[i*k+j]=one-bits[2*(j*k+i)];}
    KonaShareConversion::mul_vector_additive_kona_l2_chunked<64>(left,right,equal,player);
    counts.assign(k,Pair{Ring(0),Ring(0)});
    for(size_t i=0;i<k;++i){for(size_t j=0;j<k;++j)counts[i][0]+=equal[i*k+j];counts[i][1]=labels[i];}
}
void plain_top_one(vector<array<uint64_t,2>>& rows,int size,bool minimum){
    vector<int> idx(size);iota(idx.begin(),idx.end(),0);int leftover=-1;
    while(idx.size()+(leftover!=-1)>1){
        if(idx.size()%2){if(leftover==-1){leftover=idx.back();idx.pop_back();}else{idx.push_back(leftover);leftover=-1;}}
        vector<int> next;for(size_t i=0;i<idx.size();i+=2){auto& a=rows[idx[i]];auto& b=rows[idx[i+1]];
            if(minimum?a[0]<b[0]:a[0]>b[0])swap(a,b);
            next.push_back(idx[i+1]);}idx.swap(next);
    }
}
uint64_t plain_winner(const vector<array<uint64_t,2>>& rows,int k){
    vector<array<uint64_t,2>> counts(k);
    for(int i=0;i<k;++i){auto label=rows[rows.size()-1-i][1];uint64_t n=0;
        for(int j=0;j<k;++j)n+=label==rows[rows.size()-1-j][1];
        counts[i]={n,label};}
    plain_top_one(counts,k,false);return counts[k-1][1];
}
}

// Figure-5 prototype: selection+vote only, one prepared distance fixture shared
// by all four structures. No distance execution, thresholds, or new primitives.
// FullMat deliberately reuses Terminal::Full (ALL candidate pairs, including
// same-block pairs), as confirmed by the user; PCM uses Terminal::Pruned.
namespace network_ablation_engine {
using namespace std;
using Ring=Z2<64>;
using Pair=array<Ring,2>;
using ablation_helpers::SharedMetric;
using ablation_helpers::measure_shared;
struct Shape {
    uint64_t comparisons=0,swaps=0,bytes=0,exchanges=0,logical=0;
    void add(size_t pairs,bool swap) {
        comparisons+=pairs;if(swap)swaps+=pairs;
        bytes+=8*pairs+(swap?64*pairs:0);
        exchanges+=KonaBenchConfig::chunks(pairs,KonaBenchConfig::DCF_CHUNK)+(swap?KonaBenchConfig::chunks(2*pairs,KonaBenchConfig::MUL_CHUNK):0);
        logical+=1+(swap?1:0);
    }
};
void print_phase(const char* phase,const string& structure,int trial,int k,int party,
        const SharedMetric& m,uint64_t logical,uint64_t compares,uint64_t swaps,bool composed=false) {
    cout<<"ABLATION_PHASE phase="<<phase<<" structure="<<structure<<" trial="<<trial
        <<" k="<<k<<" party="<<party<<" online_ms="<<m.comm.total_seconds*1000
        <<" sent_bytes="<<m.comm.sent_bytes<<" transport_actions="<<m.comm.transport_events
        <<" exchange_rounds="<<m.comm.transport_actions<<" theoretical_rounds="<<logical
        <<" dcf_evaluate_calls="<<m.dcf_calls<<" comparators="<<compares<<" swaps="<<swaps
        <<" timing_kind="<<(composed?"composed_shared_stages":"measured")<<'\n';
}
SharedMetric add_metric(SharedMetric a,const SharedMetric& b){
    a.comm.total_seconds+=b.comm.total_seconds;a.comm.sent_bytes+=b.comm.sent_bytes;
    a.comm.transport_events+=b.comm.transport_events;a.comm.transport_actions+=b.comm.transport_actions;
    a.dcf_calls+=b.dcf_calls;return a;
}
Shape add_shape(Shape a,const Shape& b){
    a.comparisons+=b.comparisons;a.swaps+=b.swaps;a.bytes+=b.bytes;
    a.exchanges+=b.exchanges;a.logical+=b.logical;return a;
}
uint64_t vote_logical(const KonaMaskVote::DcfVoteStats& s,int /*classes*/){
    return s.logical_rounds();
}

// Reuse only actual shared execution. DQ checkpoints accumulate measured
// Top-1 steps and exclude snapshot copies/logs/votes from later prefixes.
// Matrix branches share one executed block stage per k. Branch input copies
// and public plans are outside timers; terminal+vote is continuously timed.
// Composed headlines are explicitly distinct from continuous Merge timing.
void run_shared(const vector<Pair>& fixture,const vector<array<uint64_t,2>>& clear,
        const vector<array<uint64_t,2>>& sorted,const vector<int>& ks,int classes,
        int repeats,int party,uint64_t checksum,const string& profile,
        RealTwoPartyPlayer* player,KonaDcfCompare::Compare64<64>& dcf,
        const KonaReducedDepthHybrid::Policy& policy){
    using namespace ablation_helpers;
    const int n=fixture.size(),max_k=*max_element(ks.begin(),ks.end());
    auto ordered=ks;sort(ordered.begin(),ordered.end());
    vector<CachedPlan> dq_plans;vector<Shape> dq_shapes;
    for(int i=0;i<max_k;++i){
        dq_plans.push_back(prepare_cached_top_one(n-i));Shape s;
        for(const auto& l:dq_plans.back())if(!l.idx.empty())s.add(l.idx.size()/2,true);
        dq_shapes.push_back(s);
    }
    struct Point {
        KonaReducedDepthCongTopK::PreparedCongPlan merge;
        KonaReducedDepthCongTopK::PreparedReducedPlan blocks;
        KonaReducedDepthCongTopK::TerminalPlan full,pcm;
        CachedPlan dq_vote;uint64_t dq_winner=0,winner=0;
    };
    map<int,Point> points;auto reference=clear;
    for(int i=1;i<=max_k;++i){
        plain_top_one(reference,n-i+1,true);
        if(find(ks.begin(),ks.end(),i)!=ks.end()){
            auto& p=points[i];p.dq_winner=plain_winner(reference,i);p.dq_vote=prepare_cached_top_one(i);
            p.merge=KonaReducedDepthCongTopK::prepare_cong(n,i);
            p.blocks=KonaReducedDepthCongTopK::prepare_reduced_plan(n,i);
            p.full=KonaReducedDepthCongTopK::prepare_terminal(p.blocks,KonaReducedDepthCongTopK::Terminal::Full);
            p.pcm=KonaReducedDepthCongTopK::prepare_terminal(p.blocks,KonaReducedDepthCongTopK::Terminal::Pruned);
            vector<size_t> counts(classes,0);for(int j=0;j<i;++j)++counts.at(sorted[j][1]);
            p.winner=distance(counts.begin(),max_element(counts.begin(),counts.end()));
        }
    }
    auto observer_for=[&](Shape& shape){
        return [&shape](const char* phase,size_t,size_t pairs,auto operation){
            if(operation()!=KonaReducedDepthHybrid::Backend::DCF)throw runtime_error("unexpected PCR in ablation");
            const string p=phase;shape.add(pairs,p!="matrix_rank"&&p!="rank_mask");
        };
    };
    using Output=KonaReducedDepthCongTopK::Output<64>;
    struct Prefix {SharedMetric metric;Shape shape;Output output;};
    for(int trial=1;trial<=repeats;++trial){
        map<int,Prefix> prefixes;bool dq_executed=false;
        auto prepare_dq=[&]{
            auto work=fixture;SharedMetric cumulative;Shape shape;
            sync_outside_timer(player);
            for(int i=1;i<=max_k;++i){
                const auto step=measure_shared(player,dcf,[&]{cached_top_one(work,dq_plans[i-1],true);});
                cumulative=add_metric(cumulative,step);shape=add_shape(shape,dq_shapes[i-1]);
                if(find(ks.begin(),ks.end(),i)!=ks.end()){
                    auto& p=prefixes[i];p.metric=cumulative;p.shape=shape;
                    p.output.candidates.assign(work.end()-i,work.end());
                    // Copying checkpoints is outside every step timer.
                }
            }
            cout<<"SHARED_EXECUTION group=DQ trial="<<trial<<" max_k="<<max_k<<" execution_count=1\n";
            dq_executed=true;
        };
        auto emit=[&](const string& name,int k,const SharedMetric& selection,const Shape& s,
                const SharedMetric& vote,uint64_t vote_comp,uint64_t vote_bytes,uint64_t vote_ex,
                uint64_t vote_rounds,const SharedMetric& total,const Output& out,Ring winner,bool composed){
            bool accounting=selection.comm.sent_bytes==s.bytes&&selection.comm.transport_actions==s.exchanges&&
                selection.dcf_calls==2*s.comparisons&&vote.comm.sent_bytes==vote_bytes&&
                vote.comm.transport_actions==vote_ex&&vote.dcf_calls==2*vote_comp&&
                total.comm.sent_bytes==selection.comm.sent_bytes+vote.comm.sent_bytes&&
                total.comm.transport_events==selection.comm.transport_events+vote.comm.transport_events;
            const auto opened=reveal_pairs(out.candidates,player);
            const auto mask=out.mask.empty()?vector<uint64_t>(opened.size(),1):reveal_values(out.mask,player);
            vector<array<uint64_t,2>> chosen;bool valid=true;
            for(size_t i=0;i<mask.size();++i){if(mask[i]==1)chosen.push_back({opened[i].value,opened[i].label});else if(mask[i]!=0)valid=false;}
            sort(chosen.begin(),chosen.end());
            const auto actual_winner=reveal_values({winner},player)[0];
            const auto expected=name=="DQ"?points.at(k).dq_winner:points.at(k).winner;
            valid=valid&&chosen==vector<array<uint64_t,2>>(sorted.begin(),sorted.begin()+k)&&actual_winner==expected;
            print_phase("selection",name,trial,k,party,selection,s.logical,s.comparisons,s.swaps,composed);
            print_phase("vote",name,trial,k,party,vote,vote_rounds,vote_comp,0);
            print_phase("selection_vote_online",name,trial,k,party,total,s.logical+vote_rounds,s.comparisons+vote_comp,s.swaps,composed);
            // Preserve tiny local residuals instead of pretending stage clocks
            // sum exactly to a continuous outer clock.
            SharedMetric other;other.comm.total_seconds=total.comm.total_seconds-selection.comm.total_seconds-vote.comm.total_seconds;
            if(other.comm.total_seconds<0&&other.comm.total_seconds> -1e-12)other.comm.total_seconds=0;
            print_phase("online_other",name,trial,k,party,other,0,0,0,composed);
            cout<<"ABLATION_CASE structure="<<name<<" trial="<<trial<<" k="<<k<<" party="<<party
                <<" block_count="<<((name=="DQ"||name=="Merge")?1:points.at(k).blocks.plan.blocks.size())
                <<" vote_candidates="<<out.candidates.size()<<" selected="<<chosen.size()
                <<" vote_backend="<<(name=="DQ"?"KONA_BATCH_DCF_PAIRWISE":"DCF_OPTIMIZED")
                <<" vote_public_all_selected="<<(name!="DQ"&&out.mask.empty())
                <<" vote_tie_policy="<<(name=="DQ"?"native-top1-order":"smallest-public-class")
                <<" fixture_checksum="<<checksum<<" winner="<<actual_winner
                <<" accounting_check="<<(accounting?"PASS":"FAIL")<<" correctness="<<(valid?"PASS":"FAIL")<<endl;
            if(!accounting||!valid)throw runtime_error("shared ablation accounting/correctness failed");
        };
        for(size_t ki=0;ki<ks.size();++ki){
            const int k=ordered[(ki+trial-1)%ordered.size()];auto& p=points.at(k);
            // Rotate group order, k start and FullMat/PCM order. Never run all
            // repetitions of one method back-to-back.
            for(int gi=0;gi<3;++gi){const int group=(gi+ki+trial-1)%3;
                if(group==0){
                    if(!dq_executed)prepare_dq();
                    const auto& prefix=prefixes.at(k);Ring winner;
                    const auto vote=measure_shared(player,dcf,[&]{
                        vector<Ring> labels;for(auto it=prefix.output.candidates.rbegin();it!=prefix.output.candidates.rend();++it)labels.push_back((*it)[1]);
                        vector<Pair> counts;cached_label_counts(labels,counts);cached_top_one(counts,p.dq_vote,false);winner=counts[k-1][1];
                    });
                    emit("DQ",k,prefix.metric,prefix.shape,vote,uint64_t(k)*k+k-1,24ULL*k*k+72ULL*(k-1),
                        (KonaBenchConfig::chunks(uint64_t(k)*k,KonaBenchConfig::DCF_CHUNK)+KonaBenchConfig::chunks(uint64_t(k)*k,KonaBenchConfig::MUL_CHUNK))+cached_rounds(p.dq_vote),2+2*p.dq_vote.size(),
                        add_metric(prefix.metric,vote),prefix.output,winner,true);
                }else if(group==1){
                    auto work=fixture;Output out;Shape shape;Ring winner;KonaMaskVote::DcfVoteStats vs;SharedMetric selection,vote;
                    sync_outside_timer(player);
                    const auto total=measure_shared(player,dcf,[&]{
                        selection=measure_shared(player,dcf,[&]{KonaReducedDepthCongTopK::execute_cong<64>(work,p.merge,profile,policy,dcf,player,observer_for(shape));out.candidates.assign(work.end()-k,work.end());});
                        vote=measure_shared(player,dcf,[&]{winner=KonaMaskVote::vote_dcf_optimized<64>(out.candidates,out.mask,classes,player,dcf,true,&vs,k);});
                    });
                    emit("Merge",k,selection,shape,vote,vs.comparators(),8*vs.comparators()+16*vs.mul_items,vs.exchanges(),vote_logical(vs,classes),total,out,winner,false);
                }else{
                    auto work=fixture;vector<Pair> candidates;Shape block_shape;
                    sync_outside_timer(player);
                    const auto block=measure_shared(player,dcf,[&]{
                        KonaReducedDepthCongTopK::execute_blocks<64>(work,p.blocks,profile,policy,dcf,player,observer_for(block_shape));
                        candidates=KonaReducedDepthCongTopK::collect_block_candidates<64>(work,p.blocks.plan,p.blocks.block_offsets);
                    });
                    print_phase("block_cong","MatrixShared",trial,k,party,block,block_shape.logical,block_shape.comparisons,block_shape.swaps);
                    cout<<"SHARED_EXECUTION group=Matrix trial="<<trial<<" k="<<k<<" execution_count=1\n";
                    for(int bi=0;bi<2;++bi){
                        const bool full=(bi+ki+trial)%2;const string name=full?"FullMat":"PCM";
                        auto branch=candidates;Output out;Shape tail_shape;Ring winner;KonaMaskVote::DcfVoteStats vs;SharedMetric tail,vote;
                        sync_outside_timer(player);
                        const auto branch_total=measure_shared(player,dcf,[&]{
                            tail=measure_shared(player,dcf,[&]{out=KonaReducedDepthCongTopK::execute_terminal<64>(std::move(branch),full?p.full:p.pcm,k,profile,policy,dcf,player,observer_for(tail_shape));});
                            vote=measure_shared(player,dcf,[&]{winner=KonaMaskVote::vote_dcf_optimized<64>(out.candidates,out.mask,classes,player,dcf,out.mask.empty(),&vs,k);});
                        });
                        print_phase("terminal",name,trial,k,party,tail,tail_shape.logical,tail_shape.comparisons,tail_shape.swaps);
                        print_phase("terminal_vote_online",name,trial,k,party,branch_total,tail_shape.logical+vote_logical(vs,classes),tail_shape.comparisons+vs.comparators(),tail_shape.swaps);
                        emit(name,k,add_metric(block,tail),add_shape(block_shape,tail_shape),vote,vs.comparators(),
                            8*vs.comparators()+16*vs.mul_items,vs.exchanges(),vote_logical(vs,classes),add_metric(block,branch_total),out,winner,true);
                    }
                }
            }
        }
    }
}
int run(int argc,const char** argv) {
    ez::ezOptionParser opt;
    opt.add("0",0,1,0,"Party","-p","--player");
    opt.add("35000",0,1,0,"Port","-pn","--portnumbase");
    opt.add("localhost",0,1,0,"Host","-h","--hostname");
    opt.add("/usr/src/Garnet/ConstantPP/data/16384-32",0,1,0,"Shared input fixture","--dataset");
    opt.add("3,5,10,20,50",0,1,0,"Comma-separated k points","--ks");
    opt.add("DQ,Merge,FullMat,PCM",0,1,0,"Comma-separated structures","--structures");
    opt.add("1",0,1,0,"Interleaved repetitions; prototype defaults to one","--repeats");
    opt.add("0",0,1,0,"Regression only: first N rows; zero uses all","--limit");
    opt.add("wan",0,1,0,"Profile label; launcher must configure actual network","--network-profile");
    opt.add("",0,0,0,"One DQ max-k chain and one shared matrix block stage per k/repetition","--reuse-common");
    opt.parse(argc,argv);
    int party,port,repeats,limit;string host,path,ks_text,structures_text,profile;
    opt.get("-p")->getInt(party);opt.get("-pn")->getInt(port);opt.get("-h")->getString(host);
    opt.get("--dataset")->getString(path);opt.get("--ks")->getString(ks_text);
    opt.get("--structures")->getString(structures_text);opt.get("--repeats")->getInt(repeats);
    opt.get("--limit")->getInt(limit);opt.get("--network-profile")->getString(profile);
    const auto ks=ablation_helpers::parse_ks(ks_text);
    vector<string> structures;istringstream names_in(structures_text);string token;
    while(getline(names_in,token,',')) {
        if((token!="DQ"&&token!="Merge"&&token!="FullMat"&&token!="PCM")||
                find(structures.begin(),structures.end(),token)!=structures.end())
            throw runtime_error("invalid/duplicate ablation structure");
        structures.push_back(token);
    }
    if(structures.empty()||repeats<1||limit<0||(profile!="lan"&&profile!="wan"))
        throw runtime_error("invalid ablation options");
    const auto data=ConstantPP::load_constantpp_dataset(path);
    const int n=limit?min<size_t>(limit,data.database.size()):data.database.size();
    const int classes=*max_element(data.labels.begin(),data.labels.end())+1;
    vector<array<uint64_t,2>> clear(n);vector<Pair> fixture(n);
    auto share=[&](uint64_t v,size_t i){Ring m(static_cast<mp_limb_t>(7001+37*i));
        return party==0?m:Ring(static_cast<mp_limb_t>(v))-m;};
    // Full plaintext distance preparation is outside every selection timer.
    // Reject overflow/sign-domain violations rather than alter comparison keys.
    uint64_t checksum=1469598103934665603ULL;
    for(int i=0;i<n;++i){
        unsigned __int128 sum=0;
        for(size_t j=0;j<data.num_features;++j){
            __int128 delta=__int128(data.database[i][j])-__int128(data.query[j]);sum+=delta*delta;
        }
        if(sum>=(static_cast<unsigned __int128>(1)<<63))throw runtime_error("ablation distance exceeds signed DCF domain");
        clear[i]={uint64_t(sum),data.labels[i]};fixture[i]={share(clear[i][0],2*i),share(clear[i][1],2*i+1)};
        for(auto v:clear[i])checksum=(checksum^v)*1099511628211ULL;
    }
    auto sorted=clear;sort(sorted.begin(),sorted.end());
    for(int k:ks){
        if(k<1||k>n)throw runtime_error("ablation k exceeds input");
        // Internal equal distances are allowed. A tied kth boundary would make
        // original networks' different tie policies a confound, so fail clearly.
        if(k<n&&sorted[k-1][0]==sorted[k][0])throw runtime_error("tied kth distance: specify a common tie policy before this ablation");
    }
    Names names;Server::start_networking(names,party,2,host,port,Names::DEFAULT_PORT);
    unique_ptr<RealTwoPartyPlayer> connection(new RealTwoPartyPlayer(names,1-party,0));
    auto* player=connection.get();ablation_helpers::playerno=party;ablation_helpers::player=player;
    KonaDcfCompare::Compare64<64> dcf(player,party);ablation_helpers::cached_dcf=&dcf;
    KonaReducedDepthHybrid::Policy policy;
    policy.comparator_override=KonaReducedDepthHybrid::Backend::DCF;
    policy.matrix_backend_override=KonaReducedDepthHybrid::Backend::DCF;
    cout<<fixed<<setprecision(9)<<"train_count="<<n<<"\nfeatures="<<data.num_features
        <<"\nquery_count=1\npublic_label_domain="<<classes<<"\nfixture_checksum="<<checksum
        <<"\ninput_preparation=common-local-distance-and-label-test-shares"
        <<"\ndistance_execution_count=0\nbackend=DCF_ONLY\npcr_comparators=0"
        <<"\nvote_policy=DQ-native-batched-DCF;Merge-optimized-all-selected;matrices-optimized-mask"
        <<"\nfullmat_pairs=all-unordered-including-same-block"
        <<"\ndq_implementation=historical-repeated-top1-leftover"
        <<"\nmaterial_mode=existing-L2-test-material\nnetwork_profile="<<profile
        <<"\nnetwork_configuration=EXTERNAL_REQUIRED\nmeasurement_scope=selection-through-winning-label-share\n";
    if(opt.isSet("--reuse-common")){
        auto required=structures;sort(required.begin(),required.end());
        if(required!=vector<string>({"DQ","FullMat","Merge","PCM"}))throw runtime_error("reuse-common requires all four structures");
        run_shared(fixture,clear,sorted,ks,classes,repeats,party,checksum,profile,player,dcf,policy);
        ablation_helpers::cached_dcf=nullptr;
        cout<<"reuse_common=1\naccounting_check=PASS\ncorrectness=PASS\ntest_status=COMPLETED\n";
        return 0;
    }
    for(int trial=1;trial<=repeats;++trial)for(size_t ki=0;ki<ks.size();++ki){
        const int k=ks[(ki+trial-1)%ks.size()];
        // Public plans and reference counts are prepared before any timer.
        auto block_plan=KonaReducedDepthCongTopK::prepare_reduced_plan(n,k);
        vector<size_t> counts(classes,0);for(int i=0;i<k;++i)++counts.at(sorted[i][1]);
        const uint64_t expected_winner=distance(counts.begin(),max_element(counts.begin(),counts.end()));
        for(size_t si=0;si<structures.size();++si){
            const auto structure=structures[(si+trial-1+ki)%structures.size()];
            vector<ablation_helpers::CachedPlan> dq;
            ablation_helpers::CachedPlan dq_vote_plan;
            uint64_t case_expected_winner=expected_winner;
            KonaReducedDepthCongTopK::PreparedCongPlan merge;
            KonaReducedDepthCongTopK::TerminalPlan terminal;
            Shape predicted;
            if(structure=="DQ")for(int i=0;i<k;++i){
                dq.push_back(ablation_helpers::prepare_cached_top_one(n-i));
                for(const auto& l:dq.back())if(!l.idx.empty())predicted.add(l.idx.size()/2,true);
            }else if(structure=="Merge")merge=KonaReducedDepthCongTopK::prepare_cong(n,k);
            else terminal=KonaReducedDepthCongTopK::prepare_terminal(block_plan,structure=="FullMat"?
                    KonaReducedDepthCongTopK::Terminal::Full:KonaReducedDepthCongTopK::Terminal::Pruned);
            if(structure=="DQ"){
                dq_vote_plan=ablation_helpers::prepare_cached_top_one(k);
                auto reference=clear;
                for(int i=0;i<k;++i)ablation_helpers::plain_top_one(reference,n-i,true);
                case_expected_winner=ablation_helpers::plain_winner(reference,k);
            }
            vector<Pair> work=fixture;KonaReducedDepthCongTopK::Output<64> output;
            KonaMaskVote::DcfVoteStats vote_shape;Ring winner;SharedMetric selection,vote,total;
            auto observer=[&](const char* phase,size_t,size_t pairs,auto operation){
                const auto backend=operation();
                if(backend!=KonaReducedDepthHybrid::Backend::DCF)throw runtime_error("ablation unexpectedly used PCR");
                const string name=phase;predicted.add(pairs,name!="matrix_rank"&&name!="rank_mask");
            };
            ablation_helpers::sync_outside_timer(player);
            const auto c0=player->total_comm();const auto t0=chrono::steady_clock::now();
            selection=measure_shared(player,dcf,[&]{
                if(structure=="DQ"){
                    for(auto& plan:dq)ablation_helpers::cached_top_one(work,plan,true);
                    output.candidates.assign(work.end()-k,work.end());
                }else if(structure=="Merge"){
                    KonaReducedDepthCongTopK::execute_cong<64>(work,merge,profile,policy,dcf,player,observer);
                    output.candidates.assign(work.end()-k,work.end());
                }else output=KonaReducedDepthCongTopK::execute_topk<64>(work,block_plan,terminal,profile,policy,dcf,player,observer);
            });
            // Preserve each structure's vote as requested. DQ uses the original
            // k-candidate pairwise frequency and native Top-1 max/tie policy.
            // Merge and cancelled matrices use optimized all-selected voting;
            // nondegenerate matrices share the same optimized mask voter.
            vote=measure_shared(player,dcf,[&]{
                if(structure=="DQ"){
                    vector<Ring> labels;labels.reserve(k);
                    for(int i=0;i<k;++i)labels.push_back(work[n-1-i][1]);
                    vector<Pair> counts;
                    ablation_helpers::cached_label_counts(labels,counts);
                    ablation_helpers::cached_top_one(counts,dq_vote_plan,false);
                    winner=counts[k-1][1];
                }else winner=KonaMaskVote::vote_dcf_optimized<64>(output.candidates,
                        output.mask,classes,player,dcf,output.mask.empty(),&vote_shape,k);
            });
            const auto t1=chrono::steady_clock::now();const auto c1=player->total_comm();
            total.comm=ablation_helpers::phase_delta(t0,t1,c0,c1);total.dcf_calls=selection.dcf_calls+vote.dcf_calls;
            const uint64_t vote_comparisons=structure=="DQ"?uint64_t(k)*k+k-1:vote_shape.comparators();
            const uint64_t vote_bytes=structure=="DQ"?24ULL*k*k+72ULL*(k-1):8*vote_shape.comparators()+16*vote_shape.mul_items;
            const uint64_t vote_exchanges=structure=="DQ"?(KonaBenchConfig::chunks(uint64_t(k)*k,KonaBenchConfig::DCF_CHUNK)+KonaBenchConfig::chunks(uint64_t(k)*k,KonaBenchConfig::MUL_CHUNK))+ablation_helpers::cached_rounds(dq_vote_plan):vote_shape.exchanges();
            const bool accounting=selection.comm.sent_bytes==predicted.bytes&&
                selection.comm.transport_actions==predicted.exchanges&&selection.dcf_calls==2*predicted.comparisons&&
                vote.comm.sent_bytes==vote_bytes&&vote.comm.transport_actions==vote_exchanges&&
                vote.dcf_calls==2*vote_comparisons&&
                total.comm.sent_bytes==selection.comm.sent_bytes+vote.comm.sent_bytes&&
                total.comm.transport_events==selection.comm.transport_events+vote.comm.transport_events;
            // All reveals, reference sorting and diagnostic output are post-timer.
            const auto candidates=ablation_helpers::reveal_pairs(output.candidates,player);
            const auto mask=output.mask.empty()?vector<uint64_t>(candidates.size(),1):
                    ablation_helpers::reveal_values(output.mask,player);
            vector<array<uint64_t,2>> chosen;
            bool valid=true;for(size_t i=0;i<mask.size();++i){
                if(mask[i]==1)chosen.push_back({candidates[i].value,candidates[i].label});
                else if(mask[i]!=0)valid=false;
            }
            sort(chosen.begin(),chosen.end());
            valid=valid&&chosen==vector<array<uint64_t,2>>(sorted.begin(),sorted.begin()+k)&&
                ablation_helpers::reveal_values({winner},player)[0]==case_expected_winner;
            uint64_t vote_logical=vote_shape.logical_rounds();
            if(structure=="DQ")vote_logical=2+2*dq_vote_plan.size();
            print_phase("selection",structure,trial,k,party,selection,predicted.logical,predicted.comparisons,predicted.swaps);
            print_phase("vote",structure,trial,k,party,vote,vote_logical,vote_comparisons,0);
            print_phase("selection_vote_online",structure,trial,k,party,total,predicted.logical+vote_logical,
                    predicted.comparisons+vote_comparisons,predicted.swaps);
            cout<<"ABLATION_CASE structure="<<structure<<" k="<<k<<" trial="<<trial<<" party="<<party
                <<" block_count="<<((structure=="DQ"||structure=="Merge")?1:block_plan.plan.blocks.size())
                <<" candidate_lmax="<<block_plan.plan.lmax<<" outer_partition_cancelled="<<block_plan.plan.outer_partition_cancelled
                <<" vote_candidates="<<output.candidates.size()<<" selected="<<chosen.size()
                <<" vote_backend="<<(structure=="DQ"?"KONA_BATCH_DCF_PAIRWISE":"DCF_OPTIMIZED")
                <<" vote_public_all_selected="<<(structure!="DQ"&&output.mask.empty())
                <<" vote_tie_policy="<<(structure=="DQ"?"native-top1-order":"smallest-public-class")
                <<" winner="<<case_expected_winner<<" fixture_checksum="<<checksum
                <<" accounting_check="<<(accounting?"PASS":"FAIL")<<" correctness="<<(valid?"PASS":"FAIL")<<endl;
            if(!valid||!accounting)throw runtime_error("ablation correctness/accounting failed");
        }
    }
    ablation_helpers::cached_dcf=nullptr;
    cout<<"accounting_check=PASS\ncorrectness=PASS\ntest_status=COMPLETED\n";
    return 0;
}
}

int main(int argc,const char** argv) {
    try {
        int party=-1;for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="-p"||std::string(argv[i])=="--player")party=std::stoi(argv[i+1]);
        KonaBenchAffinity::bind_fixed_cpu(party);
        return network_ablation_engine::run(argc,argv);
    } catch(const std::exception& e) {std::cerr<<"test_status=FAILED\nerror="<<e.what()<<std::endl;return 1;}
}
